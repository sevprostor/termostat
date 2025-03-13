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
  uint8_t fanDuration = 0;
  uint8_t ventPeriod = 0;
  uint8_t heatPower = 100;
  uint8_t tempDelta = 10;

  uint32_t probeTimer = 0;
  uint32_t screenTimer = 0;
  uint32_t ledTimer = 0;
  uint32_t dimmerTimer = 0;
  uint32_t fanDurTimer = 0;
  uint32_t threeHourTimer = 0;

  uint32_t clockTimer = 0;
  uint32_t uptimeMins = 0;
  uint32_t longTimer = 0;
  uint32_t hourTimer = 0;
  uint32_t min = 0;
  uint32_t hour = 0;
  int day = 0;

  bool heater = 0;
  bool vent = 0;

} sysSets;

struct SysLog{

  uint8_t month = 0;
  uint8_t length = 0;
  uint8_t threeHourPower = 0;
  int pwr;
  
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
  else {
    delay(50);
    digitalWrite(8, LOW);
  }

}

void controlHeater(){
  
  //если заданное значение на градус больше фактического
  // 27 * 10 > 258 + 10 (25.8 + 1)
  if(!sysSets.heater && sysSets.holdingTemp * 10 > globalClimate.temp + sysSets.tempDelta){
    
    //обогреватель и вентилятор включить
    sysSets.heater = 1;
    sysSets.vent = 1;

  } else if (sysSets.heater && sysSets.holdingTemp * 10 < globalClimate.temp){
    
    sysSets.heater = 0;
    sysSets.vent = 0;
    sysSets.longTimer = sysSets.uptimeMins;

  }

  return;

}

void controlVent(){

  uint32_t fandur = sysSets.fanDuration * 1000;
  uint32_t fp = sysSets.ventPeriod;
  
  if(!sysSets.vent && sysSets.uptimeMins - sysSets.longTimer >= fp){
      
    sysSets.longTimer = sysSets.uptimeMins;
    sysSets.fanDurTimer = millis();
    sysSets.vent = 1;
  
  } else if(!sysSets.heater && sysSets.vent && millis() - sysSets.fanDurTimer  > fandur){

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

  //раз в 3 часа средние показания записываются
  if(sysSets.uptimeMins - sysSets.threeHourTimer == 180){

    //kwtH * 3h = threeHourPower / 60 
    //Именно так! Иначе не лезет целым числом в 1 байт
    sysLog.threeHourPower = sysLog.pwr / 100;
    
    sysSets.threeHourTimer = sysSets.uptimeMins;

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
    EEPROM.write(8, sysLog.month);
    EEPROM.write(9, sysLog.length);
    EEPROM.write(9 + sysLog.length + (240 * sysLog.month), sysLog.threeHourPower);

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
    r = EEPROM.read(i);
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
    //if(sysSets.heater) sysLog.threeHourPower = (sysLog.threeHourPower + sysSets.heatPower) / sysSets.uptimeMins;
    if(sysSets.heater) sysLog.pwr += sysSets.heatPower;

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
    if (page.txt.length() < i * 20)
      break;
    lcd.print(page.txt.substring((i * 21), (i * 21) + 20));
  }

  //ограничение настройки
  sysSets.menuEditVal = (sysSets.menuEditVal > page.initialV[2]) ? page.initialV[2] : sysSets.menuEditVal;
  sysSets.menuEditVal = (sysSets.menuEditVal < page.initialV[0]) ? page.initialV[0] : sysSets.menuEditVal;

  lcd.setCursor(0, 3);
  lcd.print("~");
  lcd.print(sysSets.menuEditVal);
  lcd.setCursor(12, 3); // столбец, строка
  lcd.print("[<][Ok]");

  
  return output;
}

void mainScreen(){

  if(millis() - sysSets.ledTimer > 300000) lcd.noBacklight();
  
  if(millis() - sysSets.screenTimer > 1000){
    
    sysSets.screenTimer = millis();

    //if(globalClimate.temp < 100)
    
    String scrstr;
    scrstr += "t";
    scrstr += (globalClimate.temp < 100)? String(globalClimate.temp).substring(0, 1) : String(globalClimate.temp).substring(0, 2);
    scrstr += ".";
    scrstr += (globalClimate.temp < 100)? String(globalClimate.temp).substring(1, 2) : String(globalClimate.temp).substring(2, 3);
    scrstr += "/";
    scrstr += sysSets.holdingTemp;
    scrstr += "C ";

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
      scrstr = "fan up ~ ";
      scrstr += m;
      scrstr += "min  ";
      lcd.print(scrstr);

    } else if (sysSets.vent && !sysSets.heater){
      uint8_t m = ((sysSets.fanDurTimer + sysSets.fanDuration * 1000) - millis()) / 1000;
      scrstr = "fan down ~ ";
      scrstr += m;
      scrstr += "sec  ";
      lcd.print(scrstr);

    } else if (sysSets.heater){
      scrstr = "Heating...        ";
      lcd.print(scrstr);
    }

    lcd.setCursor(0, 3);
    scrstr = "log ";
   
    scrstr += sysSets.day;
    scrstr += "d ";
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
    sysSets.menuItem = 0;
    return output;
  }
  if (buttOk.pressed()){
    lcd.clear();
    output = sysSets.menuItem;
    //sysSets.menuItem = 0;
    return output + 10; // сохранить результат
  }

  if (sysSets.menuItem > cnt - 1)
    sysSets.menuItem = 0;
  if (sysSets.menuItem < 0)
    sysSets.menuItem = cnt - 1;

  int mstart = sysSets.menuItem;

  // с какого места показывать меню
  lcd.setCursor(0, 0); // столбец, строка для курсора
  lcd.print("~");
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
    String menu[] = {"Temp", "Heater power", "Temp delta", "Fan up duration", "Blow period"};
    int cnt = sizeof(menu) / sizeof(menu[0]);
    int enteredMenu = menuList(menu, cnt);
    if (enteredMenu != -1)
      sysSets.inmenu = enteredMenu;
  
  } else if (sysSets.inmenu == 10){
  
    // Температура - страница сетапа
    MenuPage temp;
    temp.txt = "Set temp to hold(C)";
    temp.nextInmenu = 0;
    temp.prevInmenu = 1;
    temp.initialV[0] = 0;
    temp.initialV[1] = sysSets.holdingTemp;
    temp.initialV[2] = 50;
    int settedVal = menuPageSetup(temp);
  
    if (settedVal != -1){
      sysSets.holdingTemp = settedVal;
      EEPROM.write(0, sysSets.holdingTemp);  
    }
  
  } else if (sysSets.inmenu == 11)
  {
    // Длит вентилятора
    MenuPage temp;
    temp.txt = "Power of heating(%)";
    temp.nextInmenu = 1;
    temp.prevInmenu = 1;
    temp.initialV[0] = 5;
    temp.initialV[1] = sysSets.heatPower;
    temp.initialV[2] = 100;
    int settedVal = menuPageSetup(temp);
    
    if (settedVal != -1){
      sysSets.heatPower = settedVal;
      EEPROM.write(1, sysSets.heatPower);  
    }

  } else if (sysSets.inmenu == 12)
  {
    // Длит вентилятора
    MenuPage temp;
    temp.txt = "Temperature delta, C(x0.1)";
    temp.nextInmenu = 1;
    temp.prevInmenu = 1;
    temp.initialV[0] = 1;
    temp.initialV[1] = sysSets.tempDelta;
    temp.initialV[2] = 100;
    int settedVal = menuPageSetup(temp);
    
    if (settedVal != -1){
      sysSets.tempDelta = settedVal;
      EEPROM.write(2, sysSets.tempDelta);  
    }

  }
  else if (sysSets.inmenu == 13)
  {
    // Длит вентилятора
    MenuPage temp;
    temp.txt = "Fan up duration(sec)";
    temp.nextInmenu = 1;
    temp.prevInmenu = 1;
    temp.initialV[0] = 0;
    temp.initialV[1] = sysSets.fanDuration;
    temp.initialV[2] = 180;
    int settedVal = menuPageSetup(temp);
    
    if (settedVal != -1){
      sysSets.fanDuration = settedVal;
      EEPROM.write(3, sysSets.fanDuration);  
    }
  }
  else if (sysSets.inmenu == 14)
  {
    // Период вентилятора
    MenuPage temp;
    temp.txt = "Period of blowings(min)";
    temp.nextInmenu = 1;
    temp.prevInmenu = 1;
    temp.initialV[0] = 0;
    temp.initialV[1] = sysSets.ventPeriod;
    temp.initialV[2] = 60;
    int settedVal = menuPageSetup(temp);
    
    if (settedVal != -1){
      sysSets.ventPeriod = settedVal;
      EEPROM.write(4, sysSets.ventPeriod);  
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
  sysSets.holdingTemp = EEPROM.read(0);
  sysSets.heatPower = EEPROM.read(1);
  sysSets.tempDelta = EEPROM.read(2);
  sysSets.fanDuration = EEPROM.read(3);
  sysSets.ventPeriod = EEPROM.read(4);

  //загрузка отработанного времени

  //сделать пункт меню со сбросом лога
  //EEPROM.put(8, 0);
  //EEPROM.put(9, 0);
  sysLog.month = EEPROM.read(8);
  sysLog.length = EEPROM.read(9);
  sysSets.uptimeMins = (sysLog.length * 180) + (sysLog.month * 43200);
  sysSets.threeHourTimer = sysSets.uptimeMins;

  delay(1000);
  logToSerial();

}

void loop()
{
  
  sysClock();
  GUIBase();
  watchPowerTriggers();
  sysLogger();
  

  /* if(Serial.available() > 0){
    String command = Serial.readString();
    if(command == "log") logToSerial();
  } */

  if(millis() - sysSets.probeTimer > 2000){
    sysSets.probeTimer = millis();
    globalClimate = getClimate();
  }

}
