"""Rejection tests for the maintained tactical runtime evidence gate."""
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest

from native_battle_runtime import battle_proof, _same_paused_payload, _visible_owned_token


class BattleEvidenceTests(unittest.TestCase):
    def proof(self):
        return {"reload": False, "paused_speed": True, "menu_pause": True,
                "canonical_unchanged": True, "day_unchanged": True, "order_accepted": True,
                "own": 1, "foreign": 1, "foreign_exact": 0, "hidden_formations": 1,
                "tick": 80, "tokens": 4, "sample_x": 640, "sample_y": 360}

    def test_fresh_and_paused_reload(self):
        proof = self.proof()
        self.assertEqual(battle_proof("native battle=" + json.dumps(proof), False, 1280, 720), proof)
        proof.update(reload=True, order_accepted=False)
        self.assertEqual(battle_proof("battle=" + json.dumps(proof), True, 1920, 1080), proof)

    def test_false_missing_malformed_duplicate(self):
        valid = "battle=" + json.dumps(self.proof())
        for text in ("", "battle={", valid + "\n" + valid,
                     valid[:-1] + ', "tick":80}'):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                battle_proof(text, False, 1280, 720)
        for key in ("paused_speed", "menu_pause", "canonical_unchanged", "day_unchanged", "order_accepted"):
            proof = self.proof(); proof[key] = False
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                battle_proof("battle=" + json.dumps(proof), False, 1280, 720)

    def test_schema_and_observer_leaks(self):
        for key, value in (("own", True), ("tokens", 5000), ("foreign", 0), ("tick", 0),
                           ("foreign_exact", 1), ("hidden_formations", 0), ("sample_x", float("nan")),
                           ("sample_y", 4), ("unexpected", True), ("reload", True)):
            proof = self.proof(); proof[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                battle_proof("battle=" + json.dumps(proof), False, 1280, 720)

    def test_entire_campaign_comparison(self):
        before = {"Galaxy": {"ActiveCombatEncounter": {"Battle": {"Tick": 80, "PendingSeconds": .03}}},
                  "Research": {"Points": 4}, "SimulationDays": 12}
        _same_paused_payload(before, copy.deepcopy(before))
        for section in ("Galaxy", "Research", "SimulationDays"):
            after = copy.deepcopy(before); after[section] = None
            with self.subTest(section=section), self.assertRaises(RuntimeError):
                _same_paused_payload(before, after)

    def test_visible_token_pixels_not_just_counters(self):
        width, height = 1280, 720
        pixels = bytearray(b"\x13\x09\x05" * width * height)
        header = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
        header += struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "battle.bmp"
            # Variation outside the token patch cannot pass the rendering gate.
            pixels[:3] = b"\xff\xff\xff"
            path.write_bytes(header + pixels)
            with self.assertRaises(RuntimeError):
                _visible_owned_token(path, width, height, self.proof())
            for y in range(356, 364):
                for x in range(636, 644):
                    at = ((height - y - 1) * width + x) * 3
                    pixels[at:at + 3] = bytes((157, 229, 94))
            path.write_bytes(header + pixels)
            _visible_owned_token(path, width, height, self.proof())


if __name__ == "__main__":
    unittest.main()
