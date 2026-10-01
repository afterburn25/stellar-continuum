import json
import unittest

from native_quick_find_runtime import _proof


class QuickFindProofTests(unittest.TestCase):
    def text(self, **changes):
        value = {"opened": True, "entries": 17, "matches": 6,
                 "query_length": 3, "highlighted": 1}
        value.update(changes)
        return "native-map smoke ok: quick_find=" + json.dumps(value) + "\n"

    def test_accepts_filtered_highlighted_palette(self):
        proof = _proof(self.text())
        self.assertTrue(proof["opened"])
        self.assertEqual(proof["matches"], 6)

    def test_rejects_wrong_schema_or_types(self):
        for changes in ({"opened": False}, {"opened": 1},
                        {"entries": 0}, {"entries": "17"},
                        {"matches": 0}, {"matches": 18}, {"matches": True},
                        {"query_length": 2}, {"query_length": "3"},
                        {"highlighted": -1}, {"highlighted": 6},
                        {"unexpected": True}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                _proof(self.text(**changes))

    def test_rejects_missing_duplicate_or_malformed(self):
        for text in ("", self.text() + self.text(),
                     self.text().replace("{", "not-json"),
                     self.text().replace("quick_find=", "other=")):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                _proof(text)


if __name__ == "__main__":
    unittest.main()
