# Власні захвати ShelfWatch

Форма B, Wokwi, виконано 15.10.2026. Джерело `acceptance.vcd`, SHA256 `ecae31f95488934f2b9338a12dfd136a53f0a3ef14071a06e438aa710769adb5`. Розрізнення часових шкал: журнал `time_ms` походить із MCU; `origin.json` містить абсолютний час VCD; у SR/PulseView вісь починається з нуля відповідного window. Жодних згенерованих замінних сигналів не використано.

Для перегляду відкрити `.sr` у PulseView, поруч лежить `.pvs`. Якщо програма не підхопила setup, застосувати Open → Restore Session Setup до однойменного `.pvs` один раз у чистому сеансі. Декодер I2C: SCL/SDA, 7-bit address; UART: UART_TX як RX, 115200 8N1. GPIO-сесії показують PIR/ADC_MARK/LED. Вимірювання: `../lab2/measurements.json`.

| Запис | Сенс | SR samplerate |
|---|---|---|
| i2c-whoami | 0x68 W, 0x75, STOP, 0x68 R, 0x68, master NACK, STOP |20 МГц|
| i2c-manual-address | сирий SCL/SDA, без декодера, D0=11010000, slave ACK=0 |20 МГц|
| i2c-burst | pointer0x3B і 14 байтів accel/temp/gyro |20 МГц|
| i2c-nack | неправильна адреса0x69, адресний байт D2, slave NACK=1 |20 МГц|
| uart-startup | перший рядок події; ASCII, видимий початок, далі pan |20 МГц|
| adc-marker | обгортка Start/Poll/Get/Stop, не aperture ADC |20 МГц|
| adc-period | кілька послідовних опитувань |1 МГц|
| simultaneous-events | PIR HIGH + IMU step + LDR step; підсумок у wire-uart.txt |1 МГц|
| pir-pulse | HIGH PIR і повернення LOW через5с |1 МГц|

I2C standard mode: median SCL=10 мкс; IMU burst period=10.000050 мс. ADC wrapper median=65.225 мкс; ADC period=49.993700 мс. В VCD часова одиниця1нс; квантування імпорту20МГц=50нс,1МГц=1мкс. Надійність фронтів/живлення на залізі не досліджувалась. Wokwi logic analyzer має обмежену глибину; source ends at 54.909302с, тож пізніший UART за межами VCD не доводить існування відповідної осцилограми.

Ручний розбір адреси: START є спадом SDA при SCL=1; вісім рівнів SDA на фронтах SCL:1,1,0,1,0,0,0,0 =0xD0. Перші7біт1101000=0x68, R/W=0. Дев'ятий рівень0 є ACK від slave. У pointer байті0x75=01110101, далі ACK. Наступна операція читає0xD1 та ID0x68; останній NACK дає master навмисно. У i2c-nack дев'ятий рівень1 означає відсутність пристрою0x69.

Повний спокійний burst: `00 00 00 00 40 00 EF 5C 00 00 00 00 00 00`. AccelZ=0x4000=16384→1000mg; temp=0xEF5C=-4260→24001m°C, gyro=0. За стимулу X=2g із Z=1g ADC MPU насичується на32767→1999mg; це програмний step моделі, не каліброване механічне навантаження.
