# ShelfWatch

Інтегрований прототип спостереження: MPU6050, PIR, аналоговий LDR. Режими та трактування подій описано в [PRD](docs/PRD.md); жоден сигнал не встановлює особу чи намір людини.

[Звіт ЛР2](docs/lab2/report.pdf) · [Pinout](docs/pinout.md) · [Архітектура](docs/architecture.md) · [Власні захвати](docs/captures/README.md)

Середовище: Ubuntu 24.04, Docker, STM32 HAL, Wokwi. Відкрити корінь проєкту у VS Code, зібрати та виконати Wokwi: Start Simulator. Схема diagram.json; firmware.hex/elf у firmware/build. Базовий CubeMX .ioc описує Core; додаткові ADC/GPIO налаштовані в Platform/board.c.

```sh
make build
make test
make test-linux
make coverage
make sanitize
```

UART 115200 8N1, команди `selftest`, `status`, `calibrate`, `mode`, `ack`, `imuaddr 0x68`, `imuaddr 0x69`. Після початкового фону очікуються IMU valid, Z 1000 mg, ADC valid, PIR 0. Калібрування робити у спокої. Відмова адреси не зупиняє інші сенсори.

На власних записах 15.10.2026: SCL 100 кГц, період IMU 10.000 мс, період ADC 49.994 мс. Coverage App+Devices 95.06% рядків, 77.01% гілок; Board/HAL не входять. [CI інтеграційного коміту](https://github.com/Th1sMaxOn/ShelfWatch/actions/runs/38726351840).

VCD/SR/PVS і SHA256 зберігаються у docs/captures; окремий `.origin.json` фіксує абсолютний offset кожного вікна. Повні VCD стиснені без втрат. Скриншоти у docs/lab2/figures походять із власного виконання Wokwi/PulseView. Для UART є також текстовий декод wire-uart.txt. Розрахунковий бюджет живлення та межі моделі відокремлено від вимірювань.

ЛР2 передається гілкою feature/lab-2 і тегом lab2; pull request у main створюється окремо.
