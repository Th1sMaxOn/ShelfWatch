#!/usr/bin/env python3
"""Inspect real scalar captures and retain the source hash and absolute times."""
import argparse
from array import array
from bisect import bisect_right
import gzip
import hashlib
import heapq
import json
from pathlib import Path
import re
import statistics


def read_vcd(path):
    traces, symbols, header, now = {}, {}, [], 0
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as source:
        for line in source:
            line = line.strip()
            header.append(line)
            match = re.match(r'\$var\s+\w+\s+1\s+(\S+)\s+(\S+)', line)
            if match:
                symbol, name = match.groups()
                symbols[symbol] = name
                traces[name] = (array('Q'), array('B'))
            if '$enddefinitions' in line:
                break
        if not any(re.search(r'\$timescale\s+1\s*ns\s+\$end', line) for line in header):
            raise ValueError('Only observed 1 ns scalar VCD captures are supported')
        for line in source:
            line = line.strip()
            if line.startswith('#'):
                now = int(line[1:])
            elif line and line[0] in '01' and line[1:] in symbols:
                times, values = traces[symbols[line[1:]]]
                value = int(line[0])
                if times and times[-1] == now:
                    values[-1] = value
                elif not times or values[-1] != value:
                    times.append(now)
                    values.append(value)
    return traces, now


def at(trace, time):
    times, values = trace
    index = bisect_right(times, time) - 1
    if index < 0:
        raise ValueError('Capture has no observed initial state for requested sample')
    return values[index]


def uart(trace, baud=115200):
    bit = 1e9 / baud
    result, until, previous = [], -1, 1
    for time, value in zip(*trace):
        if previous and not value and time >= until and time + 10 * bit <= trace[0][-1]:
            if at(trace, time + .5 * bit) == 0 and at(trace, time + 9.5 * bit) == 1:
                byte = sum(at(trace, time + (1.5 + index) * bit) << index for index in range(8))
                result.append({'time_ns': time, 'byte': byte})
                until = time + 9.75 * bit
        previous = value
    lines, buffer, started = [], bytearray(), None
    for item in result:
        if started is None:
            started = item['time_ns']
        if item['byte'] == 10:
            lines.append({'time_ns': started, 'text': buffer.decode('ascii', 'replace').rstrip('\r')})
            buffer.clear()
            started = None
        else:
            buffer.append(item['byte'])
    return lines


def i2c(scl, sda):
    edges = heapq.merge(((t, 0, v) for t, v in zip(*scl)), ((t, 1, v) for t, v in zip(*sda)))
    clock, data, active, bits, transactions, current = 1, 1, False, [], [], None
    for time, channel, value in edges:
        if channel:
            if clock and value != data:
                if not value:
                    if current is not None:
                        current['end_ns'] = time
                        current['end'] = 'RESTART'
                        transactions.append(current)
                    current = {'start_ns': time, 'bytes': [], 'acks': [], 'byte_end_ns': []}
                    active, bits = True, []
                else:
                    if current is not None:
                        current['end_ns'], current['end'] = time, 'STOP'
                        transactions.append(current)
                    current, active, bits = None, False, []
            data = value
        else:
            if active and not clock and value:
                bits.append(data)
                if len(bits) == 9:
                    current['bytes'].append(sum(bits[index] << (7-index) for index in range(8)))
                    current['acks'].append(not bool(bits[8]))
                    current['byte_end_ns'].append(time)
                    bits = []
            clock = value
    return transactions


def pulses(trace):
    rising, result = None, []
    for time, value in zip(*trace):
        if value:
            rising = time
        elif rising is not None:
            result.append({'start_ns': rising, 'width_ns': time-rising})
            rising = None
    return result


def clip(traces, source, output, start, end):
    if not 0 <= start < end:
        raise ValueError('Invalid interval')
    symbols = {name: chr(33+index) for index, name in enumerate(traces)}
    lines = ['$version Observed VCD window $end', '$timescale 1ns $end', '$scope module logic $end']
    lines += [f'$var wire 1 {symbols[name]} {name} $end' for name in traces]
    lines += ['$upscope $end', '$enddefinitions $end', '#0']
    lines += [f'{at(trace, start)}{symbols[name]}' for name, trace in traces.items()]
    events = sorted((time-start, symbols[name], value) for name, trace in traces.items()
                    for time, value in zip(*trace) if start < time <= end)
    previous = None
    for time, symbol, value in events:
        if time != previous:
            lines.append(f'#{time}')
            previous = time
        lines.append(f'{value}{symbol}')
    lines.append(f'#{end-start}')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines)+'\n')
    output.with_suffix('.origin.json').write_text(json.dumps({
        'source': source.name, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'start_ns': start, 'end_ns': end,
        'output_sha256': hashlib.sha256(output.read_bytes()).hexdigest()}, indent=2)+'\n')


def analyze(source, output, windows):
    traces, end = read_vcd(source)
    transactions = i2c(traces['SCL'], traces['SDA'])
    data = {'source': source.name, 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'end_ns': end, 'transitions': {name: len(trace[0]) for name, trace in traces.items()},
            'transactions': transactions}
    for name in ('UART_TX', 'UART_RX'):
        data[name] = uart(traces[name])
    for name in ('PIR', 'ADC_MARK', 'LED'):
        data[name+'_pulses'] = pulses(traces[name])
    rising = [t for t, v in zip(*traces['SCL']) if v]
    periods = [b-a for a, b in zip(rising, rising[1:]) if 1000 < b-a < 20000]
    if periods:
        data['scl_period_median_ns'] = statistics.median(periods)
    rising = [item['start_ns'] for item in data['ADC_MARK_pulses']]
    data['adc_period_median_ns'] = statistics.median(b-a for a, b in zip(rising, rising[1:]))
    data['adc_marker_width_median_ns'] = statistics.median(item['width_ns'] for item in data['ADC_MARK_pulses'])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.with_suffix('.json').write_text(json.dumps(data, indent=2)+'\n')
    output.with_suffix('.uart.txt').write_text('\n'.join(f"{item['time_ns']/1e6:.6f} ms {item['text']}" for item in data['UART_TX'])+'\n')
    for window in windows:
        name, start_ms, end_ms = window.split(':')
        clip(traces, source, output.parent/(name+'.vcd'), round(float(start_ms)*1e6), round(float(end_ms)*1e6))
    print(json.dumps({key: value for key, value in data.items() if not isinstance(value, list)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--window', action='append', default=[], help='name:start_ms:end_ms')
    args = parser.parse_args()
    analyze(args.source, args.output, args.window)
