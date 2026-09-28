"""Require an explicit client policy for every server failure code (no dependencies)."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[4]
enum_root = root / "Source/ACE.Entity/Enum"
catalog = Path(__file__).resolve().parents[1] / "Source/ACEClient/Private/Protocol/ACEWeenieErrorStrings.inl"
expected = {}
string_codes = set()
for enum in ("WeenieError", "WeenieErrorWithString"):
    for name, value in re.findall(r"^\s*(\w+)\s*=\s*(0x[\da-fA-F]+)",
                                (enum_root / (enum + ".cs")).read_text(encoding="utf-8-sig"), re.M):
        code = int(value, 16)
        expected.setdefault(code, set()).add(name)
        if enum == "WeenieErrorWithString":
            string_codes.add(code)

seen = {}
for value, text, channel, name in re.findall(
        r'\{(0x[\da-fA-F]+), (nullptr|TEXT\("(?:\\.|[^"\\])*"\)), ACEChatMessageType::(\w+)\}, // ([\w |]+)',
        catalog.read_text(encoding="utf-8")):
    code = int(value, 16)
    assert code not in seen, f"Duplicate client code: {value}"
    assert expected.get(code) == set(name.split(" | ")), f"Mismatched enum entry: {value} {name}"
    assert channel in ("System", "ChatError", "Magic"), f"Wrong destination: {value}"
    message = None if text == "nullptr" else json.loads(text[5:-1])
    if code in string_codes:
        assert message and ("%s" in message or code == 0x0528), f"Missing argument template: {value}"
    elif message:
        assert "%s" not in message, f"Plain error requires an argument: {value}"
    if message:
        assert "$s" not in message and "{message}" not in message, f"Unresolved placeholder: {value}"
        assert not message.startswith("Error 0x"), f"Untranslated error: {value}"
    seen[code] = message
assert set(seen) == set(expected), f"Missing codes: {set(expected) - set(seen)}"
assert {c for c, t in seen.items() if t == ""} == {0, 0x003B, 0x003C, 0x0436, 0x0511}
assert seen[0x04FF] == "%s is already at full health!"
print(f"PASS: {len(seen)} codes accounted for; {len(string_codes)} argument-bearing messages covered.")
print(f"{sum(bool(t) for t in seen.values())} readable templates, "
      f"{sum(t == '' for t in seen.values())} silent statuses, "
      f"{sum(t is None for t in seen.values())} internal failures with logged generic fallback.")
