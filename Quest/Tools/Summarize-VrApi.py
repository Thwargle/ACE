"""Summarize app VrApi samples, keeping compositor records out of game FPS."""
import argparse
import json
import math
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('log', type=Path)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--start', default='00:00:00')
p.add_argument('--end', default='23:59:59.999')
p.add_argument('--min-samples', type=int, default=1,
               help='Reject interrupted/empty captures instead of reporting a partial average.')
a = p.parse_args()
rows = []
for line in a.log.read_text(encoding='utf-8-sig', errors='replace').splitlines():
    m = re.match(r'\S+ (\S+)\s+(\d+)\s+\d+ I VrApi\s+: (.*)', line)
    if not m or int(m[2]) != a.pid or not a.start <= m[1] < a.end:
        continue
    fields = dict(re.findall(r'([\w%&]+)=([^,]+)', m[3]))
    if 'FPS' not in fields:
        continue
    fps, hz = map(int, fields['FPS'].split('/'))
    rows.append(dict(time=m[1], fps=fps, refreshHz=hz,
                     appMs=float(fields['App'].removesuffix('ms')),
                     gpuBusy=float(fields['GPU%']), scale=float(fields['SF']),
                     stale=int(fields['Stale'])))
if len(rows) < max(1, a.min_samples):
    raise SystemExit(f'Only {len(rows)} matching app samples; need {max(1, a.min_samples)}. '
                     'Check headset visibility, PID and capture interval.')
means = {}
invalid = []
for key in rows[0]:
    if key == 'time':
        continue
    values = []
    for row in rows:
        value = row[key]
        # The runtime sometimes emits a corrupt timestamp delta (trillions of
        # milliseconds) while FPS and utilization remain valid. Keep those
        # samples for other fields, but never average the invalid App duration.
        if key == 'appMs' and (not math.isfinite(value) or not 0 < value <= 1000):
            invalid.append(dict(time=row['time'], field=key,
                                value=value if math.isfinite(value) else str(value)))
        else:
            values.append(value)
    means[key] = statistics.mean(values) if values else None
print(json.dumps(dict(samples=len(rows), first=rows[0]['time'], last=rows[-1]['time'],
    mean=means, invalidTimingSamples=invalid, validAppSamples=len(rows)-len(invalid),
    note='One-second runtime samples; App is not a frame-time percentile. Unavailable/invalid App durations (<=0 or >1000 ms) are excluded only from that field.'),indent=2))
