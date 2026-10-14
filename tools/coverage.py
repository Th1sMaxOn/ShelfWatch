#!/usr/bin/env python3
"""Instrument portable modules once while running the independent test suites."""
from pathlib import Path
import json
import os
import platform
import re
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / 'tests/build' / ('coverage-' + platform.system() + '-' + platform.machine())
output.mkdir(parents=True, exist_ok=True)
for pattern in ['*.gcda', '*.gcno', '*.gcov', '*.o']:
    for item in output.glob(pattern):
        item.unlink()
compiler = os.environ.get('CC', 'cc')
gcov = os.environ.get('GCOV', 'gcov')
flags = ['-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Werror']
includes = [f'-I{root / part}' for part in ['firmware/Devices', 'firmware/App', 'firmware/Platform', 'tests/fakes']]
portable = ['firmware/Devices/mpu6050.c', 'firmware/App/app.c', 'firmware/App/events.c', 'firmware/App/commands.c']
suites = ['test_mpu', 'test_calibration', 'test_events', 'test_acceptance', 'test_runtime']
objects = []
instrumented = []
for relative in portable + ['firmware/Platform/runtime.c']:
    source = root / relative
    target = output / (source.stem + '.o')
    coverage = ['--coverage'] if relative in portable else []
    hooks = ['-DRUNTIME_TEST_PRINTF'] if relative == 'firmware/Platform/runtime.c' else []
    subprocess.run([compiler, *flags, *includes, *coverage, *hooks, '-c', str(source), '-o', str(target)], check=True)
    objects.append(str(target))
    if coverage:
        instrumented.append(target.with_suffix('.gcno'))
for suite in suites:
    target = output / (suite + '.o')
    subprocess.run([compiler, *flags, *includes, '-Dmain=unit_' + suite, '-c', str(root / 'tests' / (suite + '.c')), '-o', str(target)], check=True)
    objects.append(str(target))
runner = output / 'runner.c'
runner.write_text('\n'.join(f'int unit_{suite}(void);' for suite in suites) + '\nint main(void) {\n' +
                  '\n'.join(f'  if (unit_{suite}()) return 1;' for suite in suites) + '\n  return 0;\n}\n')
subprocess.run([compiler, *flags, str(runner), *objects, '--coverage', '-lm', '-o', str(output / 'suite')], check=True)
subprocess.run([str(output / 'suite')], check=True, cwd=output)
for gcno in instrumented:
    subprocess.run([gcov, '-b', '-c', str(gcno)], check=True, cwd=output, stdout=subprocess.DEVNULL)
results = []
for annotation in sorted(output.glob('*.gcov')):
    text = annotation.read_text()
    source = next(line.split('Source:', 1)[1] for line in text.splitlines() if 'Source:' in line)
    if source not in [str(root / item) for item in portable]:
        continue
    lines = executed = branches = taken = 0
    uncovered = []
    for line in text.splitlines():
        if line.startswith('branch '):
            branches += 1
            match = re.search(r'taken (\d+)', line)
            taken += bool(match and int(match.group(1)) > 0)
        elif ':' in line:
            fields = line.split(':', 2)
            count = fields[0].strip()
            if count == '-' or len(fields) != 3:
                continue
            lines += 1
            hit = count not in {'#####', '====='} and count.rstrip('*').isdigit() and int(count.rstrip('*')) > 0
            executed += hit
            if not hit:
                uncovered.append(int(fields[1]))
    results.append({'source': str(Path(source).relative_to(root)), 'lines': lines, 'executed': executed,
                    'branches': branches, 'taken': taken, 'uncovered_lines': uncovered})
totals = {key: sum(item[key] for item in results) for key in ['lines', 'executed', 'branches', 'taken']}
totals['line_percent'] = round(100 * totals['executed'] / totals['lines'], 2)
totals['branch_taken_percent'] = round(100 * totals['taken'] / totals['branches'], 2)
report = {'scope': 'Portable App and Devices only; Board/HAL are excluded', 'files': results, 'totals': totals,
          'gate': {'line_percent': 85, 'branch_taken_percent': 70}}
(output / 'coverage.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
if totals['line_percent'] < 85 or totals['branch_taken_percent'] < 70:
    raise SystemExit('Coverage gate failed')
