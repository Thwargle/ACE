"""Exercise VT enum -> retail protocol conversion through the database reader."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "vt_elements", Path(__file__).resolve().parents[1] / "Import-VTMonsterElements.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MonsterElements(unittest.TestCase):
    def test_species_and_monster_overrides_use_retail_bits(self):
        tables = [
            ("SpeciesDamages", ["Species", "Elements"], [[1, "0;1;2;3;4;5;6;9"]]),
            ("SpeciesMembers", ["Name", "Species"], [["Default", 1], ["Snow Tusker", 1], ["Cold target", 1]]),
            ("MonsterDamageOverrides", ["Name", "Elements"], [["Snow Tusker", "6;5"], ["Cold target", "5;6"]]),
        ]
        lines = [str(len(tables))]
        for name, columns, rows in tables:
            lines += [name, str(len(columns)), *columns, *(["y"] * len(columns)), str(len(rows))]
            for row in rows:
                for value in row:
                    lines += ["i" if isinstance(value, int) else "s", str(value)]
        result = module.convert("\n".join(lines))["monsters"]
        self.assertEqual(result["Default"], [2, 4, 1, 32, 64, 8, 16, 1024])
        self.assertEqual(result["Snow Tusker"], [16, 8])
        self.assertEqual(result["Cold target"], [8, 16])
        self.assertNotIn("Unknown", result)


if __name__ == "__main__":
    unittest.main()
