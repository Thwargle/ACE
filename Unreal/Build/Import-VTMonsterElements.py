"""Convert VT GameInfoDB facts to UCM's bundled element preference catalog.

Usage: python Import-VTMonsterElements.py gameinfodb.ugd output.json
Source: http://auth.virindi.net/plugins/gamedb/get2.php?date=0&dbver=9
No plugin code or unrelated database tables are included in the output.
"""
import json
import sys
from pathlib import Path


def convert(source):
    lines = iter(source.splitlines())
    def value():
        kind = next(lines)
        if kind == 'n':
            return None
        text = next(lines)
        if kind == 's':
            return text
        if kind in ('i', 'u'):
            return int(text)
        if kind in ('d', 'f'):
            return float(text)
        if kind == 'b':
            return text.lower() == 'true'
        raise ValueError(f'Unsupported database value: {kind}')
    tables = {}
    for _ in range(int(next(lines))):
        name = next(lines)
        count = int(next(lines))
        columns = [next(lines) for _ in range(count)]
        for _ in range(count):
            next(lines)
        tables[name] = [dict(zip(columns, (value() for _ in columns)))
                        for _ in range(int(next(lines)))]
    protocol = {0: 2, 1: 4, 2: 1, 3: 32, 4: 64, 5: 8, 6: 16, 9: 1024}
    def elements(text):
        return list(dict.fromkeys(protocol[int(v)] for v in text.split(';') if int(v) in protocol))
    # Column positions are the public VT schema (e0.d): monster overrides win
    # over species defaults; preserve its ordered element preferences.
    species = {list(r.values())[0]: elements(list(r.values())[1]) for r in tables['SpeciesDamages']}
    monsters = {list(r.values())[0]: species.get(list(r.values())[1], []) for r in tables['SpeciesMembers']}
    for row in tables['MonsterDamageOverrides']:
        name, damage = list(row.values())[:2]
        monsters[name] = elements(damage)
    return {'source': 'http://auth.virindi.net/plugins/gamedb/get2.php?date=0&dbver=9',
            'monsters': dict(sorted((name, values) for name, values in monsters.items() if values))}


if __name__ == '__main__':
    catalog = convert(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
    Path(sys.argv[2]).write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"Converted {len(catalog['monsters'])} named monsters")
