"""Record actual RFC2217 UART bytes; optionally send a timed command schedule."""
from pathlib import Path
import argparse
import datetime
import json
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=float, default=65)
parser.add_argument('--schedule', type=Path)
args = parser.parse_args()
schedule = json.loads(args.schedule.read_text()) if args.schedule else []
assert schedule == sorted(schedule, key=lambda item: item['after_seconds'])
args.output.parent.mkdir(parents=True, exist_ok=True)
events = []
started = datetime.datetime.now().astimezone().isoformat()
reason = 'duration_elapsed'
session = args.output.with_suffix('.session.json')
session.write_text(json.dumps({'started_at': started, 'port': args.port, 'state': 'recording'}, indent=2)+'\n')
with serial.serial_for_url(f'rfc2217://localhost:{args.port}', baudrate=115200, timeout=0.05) as connection:
    epoch = time.monotonic()
    with args.output.open('wb') as stream:
        while (elapsed := time.monotonic() - epoch) < args.seconds:
            while schedule and elapsed >= schedule[0]['after_seconds']:
                item = schedule.pop(0)
                connection.write((item['text'] + '\n').encode())
                events.append({'wall_seconds': elapsed, 'command': item['text']})
                print(f'{elapsed:.3f}s sent {item["text"]}', flush=True)
            try:
                received = connection.read(connection.in_waiting or 1)
            except serial.SerialException as error:
                reason = str(error)
                break
            if received:
                stream.write(received)
                stream.flush()
    duration = time.monotonic() - epoch
session.write_text(json.dumps({
    'started_at': started, 'duration_seconds': duration, 'port': args.port,
    'end_reason': reason,
    'commands': events, 'uart_bytes': args.output.stat().st_size,
    'clock': 'Host monotonic schedule; UART time_ms comes from the simulated MCU'
}, indent=2) + '\n')
print(f'Captured {args.output.stat().st_size} bytes', flush=True)
