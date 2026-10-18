"""Build the client production catalog from an ACE world SQL export (no DB needed).

Usage: python Generate-CraftingCatalog.py world.sql output.json --version v0.9.296
The source/target direction and yields come from recipes, never item-name guesses.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re

QUOTED = r"'(?:\\.|''|[^'\\])*'"
ROW = re.compile(r"\((?:" + QUOTED + r"|[^'()])*\)")
TOKEN = re.compile(QUOTED + r"|[^,]+")


def value(text):
    text = text.strip()
    if text == 'NULL':
        return None
    if text.startswith("'"):
        return re.sub(r"\\(.)", lambda m: {'n': '\n', 'r': '\r', 't': '\t', '0': '\0'}.get(m[1], m[1]), text[1:-1].replace("''", "'"))
    return float(text) if '.' in text or 'e' in text.lower() else int(text)


def read_tables(path):
    tables = collections.defaultdict(list)
    needed = {'cook_book', 'recipe', 'recipe_mod', 'weenie_properties_string', 'weenie_properties_int', 'weenie_properties_bool', 'weenie_properties_d_i_d'}
    with path.open(encoding='utf-8-sig') as sql:
        for line in sql:
            match = re.match(r'INSERT INTO `(\w+)` VALUES ', line)
            if not match or not (match[1] in needed or match[1].startswith('recipe_requirements_')):
                continue
            for row in ROW.finditer(line, match.end()):
                fields = [value(t[0]) for t in TOKEN.finditer(row[0][1:-1])]
                table = match[1]
                if table == 'weenie_properties_string' and fields[2] != 1:
                    continue
                if table == 'weenie_properties_int' and fields[2] not in (1, 12):
                    continue
                if table == 'weenie_properties_d_i_d' and fields[2] != 8:
                    continue
                if table == 'weenie_properties_bool' and fields[2] != 81:
                    continue
                tables[table].append(fields)
    return tables


def generate(tables, version, digest):
    names = {r[1]: r[3] for r in tables['weenie_properties_string']}
    props = {(r[1], r[2]): r[3] for r in tables['weenie_properties_int']}
    icons = {r[1]: r[3] for r in tables['weenie_properties_d_i_d']}
    pack_slots = {r[1] for r in tables['weenie_properties_bool'] if r[3]}
    recipes = {r[0]: r for r in tables['recipe']}
    conditional = {r[1] for k, rows in tables.items() if k.startswith('recipe_requirements_') for r in rows}
    modified = {r[1] for r in tables['recipe_mod']}
    skills = {37: 'Fletching', 38: 'Alchemy', 39: 'Cooking', 18: 'Item Tinkering', 28: 'Weapon Tinkering', 29: 'Armor Tinkering', 30: 'Magic Item Tinkering', 48: 'Salvaging'}
    result, used = [], set()
    for book in tables['cook_book']:
        book_id, recipe_id, source, target = book[:4]
        r = recipes.get(recipe_id)
        if not r or not r[5] or not r[6] or not all(w in names for w in (source, target, r[5])):
            continue
        output = r[5]
        category = skills.get(r[2])
        if category is None:
            kind = props.get((output, 1), 0)
            category = 'Weapons & ammunition' if kind & (1 | 256 | 32768) else 'Armor & clothing' if kind & (2 | 4) else 'Food & drink' if kind & 32 else 'Quest items & assembly'
        row = dict(id=str(book_id), recipe=recipe_id, category=category, source=source, target=target, output=output, yield_count=r[6], skill=r[2], difficulty=r[3], source_count=r[12] if r[11] > 0 else 0, target_count=r[15] if r[14] > 0 else 0, fail_source=r[18] if r[17] > 0 else 0, fail_target=r[21] if r[20] > 0 else 0, fail_output=r[8], fail_yield=r[9], conditional=recipe_id in conditional, modified=recipe_id in modified)
        # Retain all creating recipes in the browser. Only ordinary production
        # recipes are eligible as automatically manufactured ingredients.
        row['chain'] = r[2] in (0, 37, 38, 39) and recipe_id not in modified and output not in (source, target) and (row['source_count'] > 0 or row['target_count'] > 0)
        row['fail_message'] = r[10] or ''
        row['optional_source'] = 0 < r[11] < 1
        row['optional_target'] = 0 < r[14] < 1
        row['optional_fail_source'] = 0 < r[17] < 1
        row['optional_fail_target'] = 0 < r[20] < 1
        result.append(row)
        used.update((source, target, output))
        if r[8] in names:
            used.add(r[8])
    result.sort(key=lambda r: (r['category'], names[r['output']].casefold(), r['id']))
    items = [dict(id=w, name=names[w], icon=icons.get(w, 0), type=props.get((w, 1), 0), max_stack=max(1, props.get((w, 12), 1)), pack_slot=w in pack_slots) for w in sorted(used)]
    return dict(schema=1, source='ACEmulator/ACE-World-16PY-Patches', version=version, source_sha256=digest, recipes=result, items=items, coverage=dict(cookbook_pairs=len(tables['cook_book']), production_pairs=len(result), unique_outputs=len({r['output'] for r in result}), nonproduction_pairs=len(tables['cook_book'])-len(result)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sql', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    catalog = generate(read_tables(args.sql), args.version, hashlib.sha256(args.sql.read_bytes()).hexdigest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalog, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf8')
    print(json.dumps(catalog['coverage']))
    print(dict(collections.Counter(r['category'] for r in catalog['recipes'])))
