import unittest

from native_eruption_runtime import _proof, _PASS


class EruptionProofTests(unittest.TestCase):
    def text(self, marker=_PASS, event_id=4, map_p="0.35", system_p="0.35",
             live_system="0.657435", live_map="0.660888"):
        return (f"noise\nstellar_eruptions={marker} id={event_id} map={map_p} "
                f"system={system_p} live_system={live_system} live_map={live_map}\n")

    def test_accepts_continuity_pass(self):
        proof = _proof(self.text())
        self.assertEqual(proof["event_id"], 4)
        self.assertAlmostEqual(proof["live_map"], 0.660888)

    def test_rejects_failed_or_partial_contract(self):
        for marker in ("failed", "live_map_passed", "timeline_continuity_passed"):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                _proof(self.text(marker=marker))

    def test_rejects_out_of_range_progress(self):
        for kwargs in ({"event_id": -1}, {"map_p": "0"}, {"map_p": "1.5"},
                       {"system_p": "-0.2"}, {"live_system": "0"},
                       {"live_map": "1.01"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(RuntimeError):
                _proof(self.text(**kwargs))

    def test_rejects_missing_duplicate_or_malformed(self):
        for text in ("", self.text() + self.text(),
                     self.text(map_p="abc"),
                     self.text().replace("id=4", "id=x"),
                     self.text().replace("stellar_eruptions=", "other=")):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                _proof(text)


if __name__ == "__main__":
    unittest.main()
