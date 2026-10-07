# Підключення ShelfWatch, ЛР2

STM32C031C6, Wokwi. Рівень сигналів 3,3 В, спільний GND.

| Пін | Роль | Підключення |
|---|---|---|
| PB8 | SCL, open-drain GPIO HAL | MPU6050 SCL, pull-up 4,7 кОм до 3,3 В |
| PB9 | SDA, open-drain GPIO HAL | MPU6050 SDA, pull-up 4,7 кОм до 3,3 В |
| PA0 | ADC_IN0 | AO LDR |
| PA1 | GPIO input | OUT PIR |
| PA2/PA3 | USART2 AF1 | TX/RX термінала |
| PA4 | GPIO output | цифровий маркер вимірювання ADC |
| PA5 | GPIO output | вбудований LED |

MPU6050 AD0=GND: 7-бітна адреса 0x68, WHO_AM_I 0x75 повертає 0x68. UART 115200, 8N1; MPU6050 ±2g/±250°/с.

Джерела: ST DS13867 Rev4, таблиці pin definitions/alternate functions; RM0490; InvenSense RM-MPU-6000A-00, регістри 0x19–0x1C, 0x3B–0x48, 0x6B, 0x75; документація моделей Wokwi. Аналізатор: D0=SCL,D1=SDA,D2=TX,D3=PIR,D4=ADC_MARK,D5=LED, GND=GND.

Частота шини й часові межі будуть виміряні на власному записі; фізичні фронти й струми у Wokwi не перевіряються.
