"""Regression checks for the SQL-to-production-catalog boundary."""
import collections
import importlib.util
import pathlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('crafting_catalog', pathlib.Path(__file__).with_name('Generate-CraftingCatalog.py'))
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


class CraftingCatalogTest(unittest.TestCase):
    def test_sql_names_preserve_punctuation_and_escapes(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / 'world.sql'
            path.write_text(
                "INSERT INTO `weenie_properties_string` VALUES (1,100,1,'Carol\\'s (Hearty), Soup'),(2,101,1,'Chef''s Knife'),(3,102,1,'Hot\\nSoup'),(4,103,16,'Ignore me');\n"
                "INSERT INTO `weenie_properties_int` VALUES (1,100,1,32),(2,100,12,100),(3,100,99,3);\n"
                "INSERT INTO `weenie_properties_bool` VALUES (1,100,81,True),(2,101,81,False);\n".replace('True', '1').replace('False', '0'), encoding='utf8')
            tables = catalog.read_tables(path)
        self.assertEqual([r[3] for r in tables['weenie_properties_string']], ["Carol's (Hearty), Soup", "Chef's Knife", 'Hot\nSoup'])
        self.assertEqual(len(tables['weenie_properties_int']), 2)

    def test_production_direction_tools_yields_and_modification_boundary(self):
        tables = collections.defaultdict(list)
        tables['weenie_properties_string'] = [[1, 100, 1, 'Cooking tool'], [2, 200, 1, 'Raw ingredients'], [3, 300, 1, 'Soup']]
        tables['weenie_properties_int'] = [[1, 300, 1, 32], [2, 300, 12, 100]]
        tables['weenie_properties_bool'] = [[1, 300, 81, 1]]
        # Zero source consumption makes a reusable tool; two raw ingredients
        # produce six outputs. Failure may consume one ingredient or none.
        recipe = [10, 0, 39, 50, 0, 300, 6, 'Cooked', 0, 0, 'Failed', 0, 0, '', 1, 2, '', 0, 0, '', .5, 1, '', 0]
        tables['recipe'] = [recipe, [20] + recipe[1:], [30] + recipe[1:]]
        tables['recipe'][2][5] = 0  # Existing-item modification, no created output.
        tables['cook_book'] = [[1, 10, 100, 200], [2, 20, 100, 200], [3, 30, 100, 200]]
        tables['recipe_mod'] = [[1, 20]]
        result = catalog.generate(tables, 'test', 'sha')
        ordinary, modified = result['recipes']
        self.assertEqual((ordinary['source'], ordinary['target'], ordinary['output'], ordinary['yield_count']), (100, 200, 300, 6))
        self.assertEqual((ordinary['source_count'], ordinary['target_count']), (0, 2))
        self.assertTrue(ordinary['chain'])
        self.assertTrue(ordinary['optional_fail_target'])
        self.assertEqual(ordinary['fail_target'], 1)
        self.assertFalse(modified['chain'])
        self.assertTrue(modified['modified'])
        self.assertEqual(result['coverage']['production_pairs'], 2)
        self.assertEqual(result['coverage']['nonproduction_pairs'], 1)
        self.assertTrue(next(i for i in result['items'] if i['id'] == 300)['pack_slot'])


if __name__ == '__main__':
    unittest.main()
