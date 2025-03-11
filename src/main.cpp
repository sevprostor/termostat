#include <Arduino.h>
#include <Button.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

#define LCD_ROWS 4
#define LCD_COLS 20

Button buttUp(12);   // Connect your button between pin 2 and GND
Button buttDown(13); // Connect your button between pin 3 and GND
Button buttBack(10); // Connect your button between pin 4 and GND
Button buttOk(11);

LiquidCrystal_I2C lcd(0x27, 20, 4);

struct SysSets
{
  int menuItem = 0;
  int inmenu = 0;
  int menuEditVal = 0;
  bool editing = 0;
  bool keyPressed[4] = {0, 0, 0, 0};

  uint16_t climate = 0;
  bool menuRunning = 0;
  uint8_t holdingTemp = 0;
  uint8_t ventDuration = 0;
  uint8_t ventPeriod = 0;
  uint8_t heatPower = 100;

  uint32_t probeTimer = 0;
  uint32_t screenTimer = 0;
  uint32_t ledTimer = 0;
  uint32_t dimmerTimer = 0;
  
  uint32_t clockTimer = 0;
  uint32_t uptimeMins = 0;
  uint8_t longTimer = 0;
  uint8_t hourTimer = 0;
  uint8_t min = 0;
  uint8_t hour = 0;
  int day = 0;

  bool heater = 0;
  bool vent = 0;

} sysSets;

struct SysLog{

  uint8_t month = 0;
  uint8_t length = 0;
  uint8_t threeHourPower = 0;
  
} sysLog;

struct MenuPage
{
  String txt;
  String *list;
  uint8_t listcount;
  int initialV[4] = {0, 0, 0, 1}; // min val max step
  int nextInmenu = 0;
  int prevInmenu = 0;
};

DHT dht(4, DHT11);

struct Climate
{
  int temp;
  int hmd;
  //char screenStr[20];
} globalClimate;

/////////////////////////////////////////////////////////

//System
Climate getClimate();
void sysClock();
void sysLogger();
void logToSerial();

//Power
void watchPowerTriggers();
void controlHeater();
void controlVent();
void powerDimmer(int);

/////////////////////////////////////////////////////////

void powerDimmer(int pin){ //диммирование
  
  if(millis() - sysSets.dimmerTimer > 10){
  
  sysSets.dimmerTimer = millis();
  int pwr = map(sysSets.heatPower, 0, 100, 0, 255);
  static byte count, last, lastVal;
  int val = ((uint16_t)++count * pwr) >> 8;
  if (lastVal != (val != last)) digitalWrite(pin, val != last);
  lastVal = (val != last);
  last = val;
  
  }

}


void watchPowerTriggers(){

  controlHeater();
  controlVent();
  
  //обогреватель работает с диммером
  if(sysSets.heater){
    powerDimmer(9);
  } else digitalWrite(9, LOW);
  
  //вентилятор просто вкл/выкл
  if(sysSets.vent) digitalWrite(8, HIGH);
  else digitalWrite(8, LOW);

}

void controlHeater(){
  
  //если заданное значение на градус больше фактического
  // 27 * 10 > 258 + 10 (25.8 + 1)
  if(!sysSets.heater && sysSets.holdingTemp * 10 > globalClimate.temp + 10){
    
    //обогреватель и вентилятор включить
    sysSets.heater = 1;
    sysSets.vent = 1;

  } else if (sysSets.heater && sysSets.holdingTemp * 10 < globalClimate.temp){
    
    sysSets.heater = 0;
    sysSets.vent = 0;

  }

  return;

}

void controlVent(){
  
  if(!sysSets.vent && sysSets.uptimeMins - sysSets.longTimer == sysSets.ventPeriod){
    
    sysSets.longTimer = sysSets.uptimeMins;
    sysSets.vent = 1;
  
  } else if(!sysSets.heater && sysSets.vent && sysSets.uptimeMins - sysSets.longTimer == sysSets.ventDuration){

    sysSets.vent = 0;
    sysSets.longTimer = sysSets.uptimeMins;

  }

  return;

}

Climate getClimate()
{
  // считываем температуру (t) и влажность (h)
  Climate tempHum;

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  tempHum.hmd = (h * 100) / 10;
  tempHum.temp = (t * 100) / 10;

  return tempHum;
}

void sysLogger(){

  //раз в 2 часа средние показания записываются
  if(sysSets.hour - sysSets.hourTimer == 2){
    
    sysSets.hourTimer = sysSets.hour;

    //30 дней, 8 записей в день
    if(sysLog.length == 240){
      sysLog.month++;

      if (sysLog.month > 3){
        sysLog.month = 0;
        sysSets.uptimeMins = 0;  
      }

      sysLog.length = 0;
    }

    //записать в еепром. месяц, колво 3-часовых записей, [записи]
    sysLog.length++;
    EEPROM.put(8, sysLog.month);
    EEPROM.put(9, sysLog.length);
    EEPROM.put(9 + sysLog.length + (240 * sysLog.month), sysLog.threeHourPower);

  }

  return;

}

void logToSerial(){
  int bytesToRead;
  bytesToRead = 10 + (sysLog.length + (240 * sysLog.month));
  String scrstr = "Logged ";
  scrstr += sysLog.month;
  scrstr += " month, ";
  scrstr += sysLog.length;
  scrstr += " 3-hour records stored: ";

  for(int i = 10; i < bytesToRead; i++){
    uint8_t r;
    EEPROM.get(i, r);
    scrstr += r;
    scrstr += " ";
  }

  Serial.println(scrstr);


}

void sysClock(){

  if(millis() - sysSets.clockTimer > 60000){

    sysSets.uptimeMins++;
    sysSets.clockTimer = millis();

    //счет часов и минут
    sysSets.hour = sysSets.uptimeMins / 60;
    sysSets.min = sysSets.uptimeMins - (60 * sysSets.hour);
    sysSets.day = sysSets.hour / 24;
    sysSets.hour = sysSets.hour - (24 * sysSets.day);

    //подсчет средних затрат ээ
    if(sysSets.heater) sysLog.threeHourPower = (sysLog.threeHourPower + sysSets.heatPower) / sysSets.uptimeMins;

  }


  return;
}

int menuPageSetup(MenuPage page)
{
  int output = -1;

  if (buttUp.pressed()){
    lcd.clear();
    sysSets.menuEditVal++;
  
  } else if (buttDown.pressed()){
    lcd.clear();
    sysSets.menuEditVal--;
  
  } else if (buttBack.pressed()){
    lcd.clear();
    sysSets.editing = 0;
    sysSets.inmenu = page.prevInmenu;
    output = -1;
    return output;

  } else if (buttOk.pressed()){
    lcd.clear();
    sysSets.inmenu = page.nextInmenu;
    sysSets.editing = 0;
    output = sysSets.menuEditVal;
    sysSets.menuEditVal = 0;
    return output; // сохранить результат
  }

  if(!sysSets.editing) sysSets.menuEditVal = page.initialV[1];
  sysSets.editing = 1;

  //печать сообщения
  for (uint8_t i = 0; i < 3; i++)
  {
    lcd.setCursor(0, i);
    if (page.txt.length() < i * 19)
      break;
    lcd.print(page.txt.substring(i * 19, (i * 20) + 19));
  }

  //ограничение настройки
  sysSets.menuEditVal = (sysSets.menuEditVal > page.initialV[2]) ? page.initialV[2] : sysSets.menuEditVal;
  sysSets.menuEditVal = (sysSets.menuEditVal < page.initialV[0]) ? page.initialV[0] : sysSets.menuEditVal;

  lcd.print(sysSets.menuEditVal);
  lcd.setCursor(12, 3); // столбец, строка
  lcd.print("[<][Ok]");

  
  return output;
}

void mainScreen(){

  if(millis() - sysSets.ledTimer > 300000) lcd.noBacklight();
  
  if(millis() - sysSets.screenTimer > 2000){
    
    sysSets.screenTimer = millis();

    //if(globalClimate.temp < 100)
    
    String scrstr;
    scrstr += "t";
    scrstr += (globalClimate.temp < 100)? String(globalClimate.temp).substring(0, 1) : String(globalClimate.temp).substring(0, 2);
    scrstr += ".";
    scrstr += (globalClimate.temp < 100)? String(globalClimate.temp).substring(1, 2) : String(globalClimate.temp).substring(2, 3);
    scrstr += "(";
    scrstr += sysSets.holdingTemp;
    scrstr += ")C ";

    scrstr += "h";
    scrstr += (globalClimate.hmd < 100)? String(globalClimate.hmd).substring(0, 1) : String(globalClimate.hmd).substring(0, 2);
    scrstr += "% ";

    //if(sysSets.heater) scrstr += "";  
    
    //lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(scrstr);
    lcd.setCursor(0, 1);

    scrstr = "heat power ";
    scrstr += sysSets.heatPower;
    scrstr += "%";

    lcd.print(scrstr);
    
    lcd.setCursor(0, 2);
    
    if(!sysSets.vent && !sysSets.heater){
      uint8_t m = (sysSets.longTimer + sysSets.ventPeriod) - sysSets.uptimeMins;
      scrstr = "vent on ~ ";
      scrstr += m;
      scrstr += "min  ";
      lcd.print(scrstr);

    } else if (sysSets.vent && !sysSets.heater){
      uint8_t m = (sysSets.longTimer + sysSets.ventDuration) - sysSets.uptimeMins;
      scrstr = "vent off ~ ";
      scrstr += m;
      scrstr += "min  ";
      lcd.print(scrstr);

    } else if (sysSets.heater){
      scrstr = "Heating...        ";
      lcd.print(scrstr);
    }

    lcd.setCursor(0, 3);
    scrstr = "log: ";
   
    scrstr += sysSets.day;
    scrstr += " ";
    scrstr += (sysSets.hour < 10)? "0" : "";
    scrstr += sysSets.hour;
    scrstr += ":";
    scrstr += (sysSets.min < 10)? "0" : "";
    scrstr += sysSets.min;

    scrstr += "(";
    scrstr += sysLog.length;
    scrstr += ")";

    lcd.print(scrstr);
  
  }
  
  return;

}

int menuList(String *menu, int cnt){
  //При нажатии Ок функция возвращает номер выбранного пункта

  sysSets.editing = 0;
  int output = -1;

  if (buttUp.pressed()){
    sysSets.menuItem--;
    lcd.clear();
  }
  if (buttDown.pressed()){
    sysSets.menuItem++;
    lcd.clear();
  }
  if (buttBack.pressed()){
    sysSets.inmenu = 0;
    lcd.clear();
    return output;
  }
  if (buttOk.pressed()){
    lcd.clear();
    output = sysSets.menuItem;
    sysSets.menuItem = 0;
    Serial.println(output + 10);
    return output + 10; // сохранить результат
  }

  if (sysSets.menuItem > cnt - 1)
    sysSets.menuItem = 0;
  if (sysSets.menuItem < 0)
    sysSets.menuItem = cnt - 1;

  int mstart = sysSets.menuItem;

  // с какого места показывать меню
  lcd.setCursor(0, 0); // столбец, строка для курсора
  lcd.print(">");
  for (int i = 0; i < 4; i++)
  {
    lcd.setCursor(1, i); // столбец, строка
    
    lcd.print(menu[mstart]);
    if (mstart < cnt - 1)
      mstart++;
    else
      break;
  }

  return output;
}

void GUIBase(){

  if(sysSets.inmenu > 0 || buttUp.pressed()){
    lcd.backlight();
    sysSets.ledTimer = millis();
  }

  // нажатия кнопок и рендеринг в зависимости от положения
  if (sysSets.inmenu == 0){
    mainScreen();
    if (buttOk.pressed()){
      sysSets.inmenu = 1;
      lcd.clear();
    }

  } else if (sysSets.inmenu == 1){
    // главное меню - лист пунктов
    String menu[] = {"Temp", "Heater power", "Vent duration", "Vent period"};
    int cnt = sizeof(menu) / sizeof(menu[0]);
    int enteredMenu = menuList(menu, cnt);
    if (enteredMenu != -1)
      sysSets.inmenu = enteredMenu;
  
  } else if (sysSets.inmenu == 10){
  
    // Температура - страница сетапа
    MenuPage temp;
    temp.txt = "Set temp to carry";
    temp.nextInmenu = 0;
    temp.prevInmenu = 1;
    temp.initialV[0] = 0;
    temp.initialV[1] = sysSets.holdingTemp;
    temp.initialV[2] = 50;
    int settedVal = menuPageSetup(temp);
  
    if (settedVal != -1){
      sysSets.holdingTemp = settedVal;
      EEPROM.put(0, sysSets.holdingTemp);  
    }
  
  } else if (sysSets.inmenu == 11)
  {
    // Длит вентилятора
    MenuPage temp;
    temp.txt = "Power of heating (%)";
    temp.nextInmenu = 1;
    temp.prevInmenu = 1;
    temp.initialV[0] = 5;
    temp.initialV[1] = sysSets.heatPower;
    temp.initialV[2] = 100;
    int settedVal = menuPageSetup(temp);
    
    if (settedVal != -1){
      sysSets.heatPower = settedVal;
      EEPROM.put(2, sysSets.heatPower);  
    }

  }
  else if (sysSets.inmenu == 12)
  {
    // Длит вентилятора
    MenuPage temp;
    temp.txt = "Duration of periodic ventilations (min)";
    temp.nextInmenu = 1;
    temp.prevInmenu = 1;
    temp.initialV[0] = 0;
    temp.initialV[1] = sysSets.ventDuration;
    temp.initialV[2] = 60;
    int settedVal = menuPageSetup(temp);
    
    if (settedVal != -1){
      sysSets.ventDuration = settedVal;
      EEPROM.put(4, sysSets.ventDuration);  
    }
  }
  else if (sysSets.inmenu == 13)
  {
    // Период вентилятора
    MenuPage temp;
    temp.txt = "Period of regular ventilations (min)";
    temp.nextInmenu = 1;
    temp.prevInmenu = 1;
    temp.initialV[0] = 0;
    temp.initialV[1] = sysSets.ventPeriod;
    temp.initialV[2] = 60;
    int settedVal = menuPageSetup(temp);
    
    if (settedVal != -1){
      sysSets.ventPeriod = settedVal;
      EEPROM.put(6, sysSets.ventPeriod);  
    }

  }
  else sysSets.inmenu = 1;
}

void setup()
{
  sysSets.probeTimer = millis();
  sysSets.clockTimer = millis();

  pinMode(13, INPUT);
  pinMode(12, INPUT);
  pinMode(11, INPUT);
  pinMode(10, INPUT);

  pinMode(9, OUTPUT);
  pinMode(8, OUTPUT);

  pinMode(4, INPUT);
  pinMode(5, OUTPUT);
  pinMode(3, OUTPUT);

  // подать питание на датчик темпы. Он висит на цифровых ногах
  digitalWrite(3, LOW);
  digitalWrite(5, HIGH);

  // digitalWrite(8, HIGH);

  dht.begin();
  Serial.begin(9600);
  lcd.init();      // инициализация
  lcd.backlight(); // включить подсветку

  buttUp.begin();
  buttDown.begin();
  buttBack.begin();
  buttOk.begin();

  buttOk.toggled();
  buttBack.toggled();
  buttUp.toggled();
  buttDown.toggled();

  //загрузка сохраненных настроек
  EEPROM.get(0, sysSets.holdingTemp);
  EEPROM.get(2, sysSets.heatPower);
  EEPROM.get(4, sysSets.ventDuration);
  EEPROM.get(6, sysSets.ventPeriod);

  //загрузка отработанного времени

  //сделать пункт меню со сбросом лога
  //EEPROM.put(8, 0);
  //EEPROM.put(9, 0);
  EEPROM.get(8, sysLog.month);
  EEPROM.get(9, sysLog.length);
  sysSets.uptimeMins = (sysLog.length * 180) + (sysLog.month * 43200);

  delay(1000);
  logToSerial();

}

void loop()
{
  // readKeyboard();
  // Serial.println(sysSets.inmenu);

  GUIBase();
  watchPowerTriggers();
  sysClock();


  /* if(Serial.available() > 0){
    String command = Serial.readString();
    if(command == "log") logToSerial();
  } */

  if(millis() - sysSets.probeTimer > 2000){
    sysSets.probeTimer = millis();
    globalClimate = getClimate();
  }
  // выводим температуру (t) и влажность (h) на монитор порта
}
