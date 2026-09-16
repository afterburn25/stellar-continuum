import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_surface_runtime import _validate_surface_art_pixels, validate_native_surface_export


def base_payload():
    systems = [{"Id": i, "Name": f"System {i}", "X": float(i), "Y": 0.0}
               for i in range(500)]
    bodies = [{"Id": i, "SystemId": i, "Name": "Earth" if i == 0 else f"Body {i}"}
              for i in range(500)]
    return {"FormatVersion": 17, "SavedAtUtc": "base", "SimulationDays": 0.0,
            "Galaxy": {"PlayerCivilizationId": 0, "Systems": systems,
                       "PlanetaryBodies": bodies,
                       "Civilizations": [{"Id": 0, "Name": "Player", "HomeSystemId": 0,
                                             "SpeciesId": "terran_baseline", "IsPlayer": True}],
                       "Colonies": [{"Id": 4, "CivilizationId": 0, "SystemId": 0,
                                      "PlanetaryBodyId": 0, "SurfaceBuildings": []}],
                       "Economies": [{"CivilizationId": 0, "Credits": 500.0}],
                       "Knowledge": [{"CivilizationId": 0, "KnownSystemIds": [0],
                                      "SystemSurveys": [{"SystemId": 0, "Level": 3,
                                                         "Progress": 1.0}]}]}}


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    length = stride * height
    pixels = (bytes(range(251)) * (length // 251 + 1))[:length]
    size = 54 + length
    return (b"BM" + struct.pack("<IHHI", size, 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        length, 2835, 2835, 0, 0) + pixels)


def logical_bmp(width, height, *, bits, top_down, offset, changed=frozenset()):
    channels = bits // 8
    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for stored_y in range(height):
        y = stored_y if top_down else height - stored_y - 1
        row = bytearray(stride)
        for x in range(width):
            color = [17 + x, 31 + y, 47 + (x + y) % 100]
            if (x, y) in changed:
                color[0] ^= 7
            at = x * channels
            row[at:at + 3] = bytes(color)
            if channels == 4:
                row[at + 3] = 255
        rows.append(row)
    pixels = b"".join(rows)
    size = offset + len(pixels)
    header = (b"BM" + struct.pack("<IHHI", size, 0, 0, offset) +
              struct.pack("<IiiHHIIiiII", 40, width, -height if top_down else height,
                          1, bits, 0, len(pixels), 2835, 2835, 0, 0))
    return header + bytes(offset - len(header)) + pixels


class NativeSurfaceRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); package = root / "package"; package.mkdir()
            calls = []

            def run(args, *, cwd, env, **unused):
                self.assertNotEqual(Path(cwd), package)
                self.assertNotIn(str(package), env.get("PATH", ""))
                if fault == "failed":
                    return subprocess.CompletedProcess(args, 7, "caught-output", "terminal-error")
                save = Path(args[args.index("--save-path") + 1])
                width = int(args[args.index("--width") + 1]); height = int(args[args.index("--height") + 1])
                if "--smoke" in args:
                    payload = base_payload()
                    if fault == "fresh_format": payload["FormatVersion"] = 16
                    if fault == "fresh_systems": payload["Galaxy"]["Systems"].pop()
                    if fault == "fresh_unknown": payload["Galaxy"]["Knowledge"][0]["KnownSystemIds"] = []
                    if fault == "fresh_survey": payload["Galaxy"]["Knowledge"][0]["SystemSurveys"][0]["Level"] = 1
                    if fault == "fresh_earth": payload["Galaxy"]["PlanetaryBodies"][0]["Name"] = "Mars"
                    if fault == "fresh_funds": payload["Galaxy"]["Economies"][0]["Credits"] = 0
                    save.write_text(json.dumps(payload), encoding="utf-8")
                    capture = Path(args[args.index("--smoke") + 1])
                    stdout = "gpu_driver=vulkan systems=500 image_uploads=0 save=ok"
                else:
                    payload = json.loads(save.read_text())
                    reload = "--surface-reload-smoke" in args
                    mode = "paused_reload" if reload else "ordered"
                    state = {"mode": mode, "system_id": 0, "body_id": 0, "colony_id": 4,
                             "type_id": "power_generator", "site_id": 0,
                             "x": 96.0, "z": 112.0, "rotation": 15.0,
                             "authorization": 25.0, "refund": 12.5,
                             "treasury_before": 500.0, "treasury_after_cancel": 500.0,
                             "treasury_after_place": 475.0, "treasury_after_refund": 487.5,
                             "treasury_saved": 462.6, "site_count_before": 0,
                             "site_count_saved": 1, "progress": 1.25,
                             "before_days": .5 if reload else 0.0, "saved_days": .5,
                             "palette_selected": not reload, "ghost_previewed": not reload,
                             "placement_cancelled": not reload, "cancel_no_change": not reload,
                             "placement_confirmed": not reload, "removal_previewed": not reload,
                             "removal_confirmed": not reload, "refund_exact": not reload,
                             "persisted_site": True, "paused": True}
                    state["render"] = {"sites": 1, "meshes": 3, "triangles": 24,
                                        "road_segments": 1}
                    art = {"requested": 2, "ready": 2, "pending": 0, "deferred": 0, "failed": 0,
                           "replaced": 2, "entries": 2, "cache_bytes": 1024, "reserved_bytes": 0,
                           "admitted": 2, "completed": 2, "canceled": 0, "terrain": [100, 100, 300, 300]}
                    if fault == "art_bad_terrain": art["terrain"] = [100, 100, "bad", 300]
                    if fault == "art_ready_mismatch": art["ready"] = 1
                    if fault == "art_zero_entries": art["entries"] = 0
                    if not reload:
                        payload["SavedAtUtc"] = "ordered"
                        payload["SimulationDays"] = .5
                        payload["Galaxy"]["Economies"][0]["Credits"] = 462.6
                        payload["Galaxy"]["Colonies"][0]["SurfaceBuildings"] = [{
                            "Id": 0, "TypeId": "power_generator", "X": 96.0, "Z": 112.0,
                            "RotationDegrees": 15.0, "IndustryProgress": 1.25,
                            "IsComplete": False, "IsEnabled": True}]
                        if fault == "systems_after": payload["Galaxy"]["Systems"].pop()
                        if fault == "owner": payload["Galaxy"]["Colonies"][0]["CivilizationId"] = 1
                        if fault == "knowledge": payload["Galaxy"]["Knowledge"][0]["KnownSystemIds"] = []
                        if fault == "site_type": payload["Galaxy"]["Colonies"][0]["SurfaceBuildings"][0]["TypeId"] = "science_lab"
                        if fault == "site_complete": payload["Galaxy"]["Colonies"][0]["SurfaceBuildings"][0]["IsComplete"] = True
                        if fault == "site_position": payload["Galaxy"]["Colonies"][0]["SurfaceBuildings"][0]["X"] += 1
                        if fault == "site_progress": payload["Galaxy"]["Colonies"][0]["SurfaceBuildings"][0]["IndustryProgress"] += 1
                        if fault == "saved_treasury": payload["Galaxy"]["Economies"][0]["Credits"] += 1
                    else:
                        payload["SavedAtUtc"] = "reload"
                        if fault == "payload": payload["Galaxy"]["ReloadMutation"] = True
                    if fault in state:
                        state[fault] = False if isinstance(state[fault], bool) else -1
                    if fault == "extra": state["hidden_name"] = "Earth"
                    if fault == "charge": state["treasury_after_place"] += 1
                    if fault == "cancel_charge": state["treasury_after_cancel"] -= 1
                    if fault == "refund_amount": state["refund"] += 1
                    if fault == "refund_delta": state["treasury_after_refund"] += 1
                    if fault == "no_progress": state["progress"] = 0
                    if fault == "no_time": state["saved_days"] = state["before_days"]
                    if fault == "site_count": state["site_count_saved"] = 2
                    if fault == "render_missing": del state["render"]
                    if fault == "render_extra": state["render"]["lines"] = 1
                    if fault == "render_bool": state["render"]["triangles"] = True
                    if fault == "render_negative": state["render"]["sites"] = -1
                    if fault == "render_inconsistent": state["render"]["meshes"] = 25
                    if fault == "render_huge": state["render"]["triangles"] = 8193
                    save.write_text(json.dumps(payload), encoding="utf-8")
                    flag = "--surface-reload-smoke" if reload else "--surface-smoke"
                    capture = Path(args[args.index(flag) + 1])
                    stdout = ("gpu_driver=vulkan systems=500 image_uploads=9 save=ok\n" +
                              "surface_art=" + json.dumps(art, separators=(",", ":")) + "\n" +
                              "surface=" + json.dumps(state, separators=(",", ":")))
                if fault != "capture":
                    image = bmp(width - 1 if fault == "geometry" else width, height)
                    if fault == "truncated":
                        image = bytearray(image[:-64]); struct.pack_into("<I", image, 2, len(image)); image = bytes(image)
                    capture.write_bytes(image)
                    sidecar = capture.with_name(capture.stem + "-without-buildings.bmp")
                    if fault != "art_missing_sidecar":
                        side = bytearray(image); stride = ((width * 24 + 31) // 32) * 4
                        changed = ((0, 0, 10, 10) if fault == "art_outside" else
                                   (100, 100, 11, 9) if fault == "art_too_few" else
                                   (100, 100, 0, 0) if fault in ("art_unchanged", "art_header_only") else
                                   (100, 100, 100, 100))
                        left, top, changed_width, changed_height = changed
                        for py in range(top, top + changed_height):
                            for px in range(left, left + changed_width):
                                at = 54 + (height - py - 1) * stride + px * 3; side[at] ^= 7
                        if fault == "art_header_only": side[6] ^= 1
                        sidecar.write_bytes(side)
                if fault == "renderer": stdout = stdout.replace("gpu_driver=vulkan", "gpu_driver=software")
                if fault == "uploads" and "--smoke" not in args: stdout = stdout.replace("image_uploads=9", "image_uploads=0")
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_surface_runtime.subprocess.run", side_effect=run):
                result = validate_native_surface_export(package, {})
            self.assertEqual(len(calls), 3)
            self.assertNotIn("--load", calls[0]); self.assertTrue(all("--load" in c for c in calls[1:]))
            self.assertNotIn("--profile-frames", calls[0])
            self.assertTrue(all(c[-2:] == ["--profile-frames", "120"] for c in calls[1:]))
            self.assertTrue(result["nativeSurfaceFreshOwnedEarth"])
            self.assertTrue(result["nativeSurfacePlayerInput"])
            self.assertTrue(result["nativeSurfacePausedReload"])
            self.assertEqual(len(result["surfaceCaptures"]), 3)

    def test_order_reload(self): self.exercise()
    def test_palette_required(self):
        with self.assertRaises(RuntimeError): self.exercise("palette_selected")
    def test_ghost_required(self):
        with self.assertRaises(RuntimeError): self.exercise("ghost_previewed")
    def test_cancel_required(self):
        with self.assertRaises(RuntimeError): self.exercise("placement_cancelled")
    def test_cancel_no_change_required(self):
        with self.assertRaises(RuntimeError): self.exercise("cancel_no_change")
    def test_confirm_required(self):
        with self.assertRaises(RuntimeError): self.exercise("placement_confirmed")
    def test_removal_preview_required(self):
        with self.assertRaises(RuntimeError): self.exercise("removal_previewed")
    def test_removal_confirm_required(self):
        with self.assertRaises(RuntimeError): self.exercise("removal_confirmed")
    def test_refund_proof_required(self):
        with self.assertRaises(RuntimeError): self.exercise("refund_exact")
    def test_persisted_required(self):
        with self.assertRaises(RuntimeError): self.exercise("persisted_site")
    def test_paused_required(self):
        with self.assertRaises(RuntimeError): self.exercise("paused")
    def test_cancel_charge_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("cancel_charge")
    def test_wrong_charge_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("charge")
    def test_wrong_refund_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("refund_amount")
    def test_wrong_refund_delta_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("refund_delta")
    def test_no_progress_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("no_progress")
    def test_no_time_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("no_time")
    def test_site_count_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("site_count")
    def test_render_missing_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("render_missing")
    def test_render_extra_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("render_extra")
    def test_render_bool_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("render_bool")
    def test_render_negative_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("render_negative")
    def test_render_inconsistent_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("render_inconsistent")
    def test_render_huge_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("render_huge")
    def test_extra_diagnostic_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("extra")
    def test_changed_system_count_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("systems_after")
    def test_foreign_colony_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("owner")
    def test_revoked_knowledge_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("knowledge")
    def test_wrong_site_type_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("site_type")
    def test_completed_site_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("site_complete")
    def test_wrong_position_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("site_position")
    def test_wrong_progress_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("site_progress")
    def test_wrong_saved_treasury_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("saved_treasury")
    def test_reload_mutation_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("payload")
    def test_renderer_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("renderer")
    def test_uploads_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("uploads")
    def test_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_geometry_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("geometry")
    def test_truncated_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("truncated")
    def test_unchanged_surface_art_sidecar_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_unchanged")
    def test_header_only_surface_art_sidecar_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_header_only")
    def test_fewer_than_100_surface_art_pixels_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_too_few")
    def test_surface_art_pixels_outside_terrain_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_outside")
    def test_malformed_surface_art_terrain_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_bad_terrain")
    def test_surface_art_ready_mismatch_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_ready_mismatch")
    def test_surface_art_zero_entries_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_zero_entries")
    def test_missing_surface_art_sidecar_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("art_missing_sidecar")
    def test_surface_art_comparison_decodes_each_bmp_layout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            capture = root / "capture.bmp"
            fallback = root / "fallback.bmp"
            changed = {(x, y) for y in range(10) for x in range(10)}
            capture.write_bytes(logical_bmp(20, 20, bits=24, top_down=False, offset=54))
            fallback.write_bytes(logical_bmp(20, 20, bits=32, top_down=True,
                                             offset=70, changed=changed))
            _validate_surface_art_pixels(capture, fallback, 20, 20, [0, 0, 20, 20])
    def test_fresh_format_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh_format")
    def test_fresh_system_count_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh_systems")
    def test_fresh_unknown_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh_unknown")
    def test_fresh_survey_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh_survey")
    def test_fresh_not_earth_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh_earth")
    def test_fresh_without_funds_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh_funds")
    def test_failed_launch_reports_streams(self):
        with self.assertRaisesRegex(RuntimeError, "(?s)caught-output.*terminal-error"):
            self.exercise("failed")


if __name__ == "__main__":
    unittest.main()
