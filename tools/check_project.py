from pathlib import Path
import hashlib
import json
import re

root = Path(__file__).resolve().parents[1]
allowed = {'app.h', 'mpu6050.h', 'stdbool.h', 'stdint.h', 'stddef.h', 'stdio.h', 'string.h', 'limits.h', 'math.h', 'stdlib.h'}
for folder in ['App', 'Devices']:
    for source in (root / 'firmware' / folder).glob('*.[ch]'):
        text = source.read_text()
        headers = re.findall(r'#include\s*[<"]([^>"]+)', text)
        assert all(name in allowed for name in headers), (source, headers)
        assert not re.search(r'\bHAL_|\bGPIO[A-F]\b|stm32c0xx', text), source
assert 'HAL_Delay(1000)' not in (root / 'firmware/Core/Src/main.c').read_text()
assert (root / 'firmware/Drivers/STM32C0xx_HAL_Driver/Inc/stm32c0xx_hal_adc.h').exists()
manifest = json.loads((root / 'docs/lab2/adc-vendor-manifest.json').read_text())
for relative, expected in manifest['files'].items():
    path = root / 'firmware/Drivers/STM32C0xx_HAL_Driver' / relative
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, path
print('Portable dependency boundaries, scheduler structure and unchanged ADC vendor sources passed')
