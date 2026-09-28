"""Rejection tests for the maintained tactical runtime evidence gate."""
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest

from native_battle_runtime import battle_proof, battle_art_proof, _same_paused_payload, _visible_owned_token, _validate_art_difference


class BattleEvidenceTests(unittest.TestCase):
    def proof(self):
        return {"reload": False, "paused_speed": True, "menu_pause": True,
                "canonical_unchanged": True, "day_unchanged": True, "order_accepted": True,
                "own": 1, "foreign": 1, "foreign_exact": 0, "hidden_formations": 1,
                "tick": 80, "tokens": 4, "sample_x": 640, "sample_y": 360}

    def art(self):
        return {"sprites": 1, "foreign_sprites": 0, "source_width": 1254,
                "source_height": 1254, "source_bytes": 6290064,
                "alpha_pixels": 1188325, "center_x": 640.0, "center_y": 360.0,
                "size": 80.0, "heading_degrees": 15.0, "moving": True,
                "paused_unchanged": True, "ship_selected": True}

    def test_battle_art_contract(self):
        self.assertEqual(battle_art_proof("battle_art=" + json.dumps(self.art())), self.art())
        for key, value in (("sprites", 0), ("foreign_sprites", 1), ("source_bytes", 1),
                           ("alpha_pixels", 1), ("ship_selected", False), ("paused_unchanged", False), ("center_x", float("nan"))):
            value_state = self.art(); value_state[key] = value
            with self.assertRaises(RuntimeError): battle_art_proof("battle_art=" + json.dumps(value_state))

    def test_battle_art_pixel_difference(self):
        width = height = 64; stride = width * 4; header = b"BM" + struct.pack("<IHHI", 54 + stride * height, 0, 0, 54) + struct.pack("<IiiHHIIiiII", 40, width, height, 1, 32, 0, stride * height, 0, 0, 0, 0)
        with tempfile.TemporaryDirectory() as directory:
            base = bytearray(header + b"\0" * (stride * height)); base[54:57] = b"\x80\x80\x80"; suppressed = bytearray(base)
            art = self.art(); art.update(center_x=32.0, center_y=32.0, size=20.0)
            p = Path(directory); (p / "base.bmp").write_bytes(base); (p / "suppressed.bmp").write_bytes(suppressed)
            with self.assertRaises(RuntimeError): _validate_art_difference(p / "base.bmp", p / "suppressed.bmp", width, height, art)
            for y in range(25, 40):
                for x in range(25, 40): suppressed[54 + y * stride + x * 4] = 255
            (p / "base.bmp").write_bytes(base); (p / "suppressed.bmp").write_bytes(suppressed)
            base[54 + 1 * stride + 1 * 4] = 1; (p / "base.bmp").write_bytes(base)
            with self.assertRaises(RuntimeError): _validate_art_difference(p / "base.bmp", p / "suppressed.bmp", width, height, art)
            base = bytearray(header + b"\0" * (stride * height)); base[54:57] = b"\x80\x80\x80"; suppressed = bytearray(base)
            for y in range(25, 40):
                for x in range(25, 40): base[54 + y * stride + x * 4] = 2
            (p / "base.bmp").write_bytes(base); (p / "suppressed.bmp").write_bytes(suppressed)
            _validate_art_difference(p / "base.bmp", p / "suppressed.bmp", width, height, art)

    def test_authored_corvette_identity(self):
        from native_battle_runtime import _source_row, _author_battle_source
        fixture = Path(__file__).resolve().parents[2] / "native-tests/fixtures/player-campaign-json.json"
        source = _source_row(fixture)
        before = copy.deepcopy(source)
        battle = _author_battle_source(source)
        self.assertEqual(source, before)
        fleet = next(f for f in battle["Galaxy"]["Fleets"] if f["Id"] == 0)
        self.assertEqual((fleet["Role"], fleet["DesignId"]), (3, "patrol_corvette"))
        formation = battle["Galaxy"]["ActiveCombatEncounter"]["Battle"]["Formations"][0]
        self.assertEqual(formation["Cohorts"], [])
        self.assertEqual({v["Id"]: v["DesignId"] for v in formation["ImportantVessels"]},
                         {4294967296: "patrol_corvette", 1: "colony_ship"})

    def test_art_missing_duplicate_malformed(self):
        valid = "battle_art=" + json.dumps(self.art())
        for data in ("", "battle_art={", valid + "\n" + valid,
                     valid[:-1] + ', "sprites":1}'):
            with self.subTest(data=data), self.assertRaises(RuntimeError):
                battle_art_proof(data)
        for field, value in (("moving", 1), ("size", 513), ("center_y", float("inf")),
                             ("source_width", True), ("extra", 2)):
            art = self.art(); art[field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                battle_art_proof("battle_art=" + json.dumps(art))

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
