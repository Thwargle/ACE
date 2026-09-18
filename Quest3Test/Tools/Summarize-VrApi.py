"""Summarize app VrApi samples, keeping compositor records out of game FPS."""
import argparse
import json
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('log', type=Path)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--start', default='00:00:00')
p.add_argument('--end', default='23:59:59.999')
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
if not rows:
    raise SystemExit('No matching app samples; check PID and capture interval.')
print(json.dumps(dict(samples=len(rows), first=rows[0]['time'], last=rows[-1]['time'],
    mean={k:statistics.mean(r[k] for r in rows) for k in rows[0] if k!='time'},
    note='One-second runtime samples; App is the runtime timing field, not a frame-time percentile.'),indent=2))
