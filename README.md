# termostat

Термостат для всевозможных гринхаусов. Меряет температуру и влажность, имеет простейший диммер для обогревателя (ключ SSR40DA), дует пока 1 вентилятором. В будущем необходимо сделать канал генератора тумана и вытяжки, всего 4, пока их только 2.

![Общий вид мозгов без экрана](./photo_2025-03-10_21-59-26.jpg)

Программа максимально проста. Имеется менюшка. Устройство каждые 3 часа записывает приблизительные данные о среднем расходе ЭЭ на обогреватель за этот период, потом делается следующая запись. В EEPROM байт 8 - месяц, байт 9 - сколько 3-часовых записей в этом месяце. Когда записей становится 240, месяц прибавляется, и так, пока не накопится 720 записей. (до 730 адреса EEPROM включительно) В дальнейшем запись начинается сначала. Данные о расходе берутся исходя из установленной мощности диммера и общего времени работы печки.