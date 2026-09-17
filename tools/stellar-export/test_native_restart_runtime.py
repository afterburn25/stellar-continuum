import copy
import json
import unittest
from native_restart_runtime import _state, _unchanged

class RestartEvidenceTests(unittest.TestCase):
    def text(self, action="cancel", **changes):
        value = {"setup_opened": True, "boot_replayed": False, "menu_ready_recalled": False,
                 "exit": action == "exit", "returned": action == "cancel", "created": action == "create",
                 "music_start_count": 1}
        value.update(changes)
        return "restart_renderer=vulkan\nrestart=" + json.dumps(value) + "\nrestart_cancel=preserved_same_campaign\n"

    def test_all_three_distinct_outcomes(self):
        for action in ("cancel", "exit", "create"):
            self.assertTrue(_state(self.text(action), action, True)["setup_opened"])

    def test_rejects_replayed_boot_music_and_wrong_outcomes(self):
        for changes in ({"boot_replayed": True}, {"menu_ready_recalled": True},
                        {"music_start_count": 0}, {"music_start_count": 2},
                        {"music_start_count": True}, {"exit": True}, {"created": True},
                        {"setup_opened": 1}, {"unexpected": True}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                _state(self.text(**changes), "cancel", True)

    def test_rejects_missing_cancel_identity_renderer_duplicate_outcomes(self):
        for text in (self.text().replace("restart_cancel=preserved_same_campaign", ""),
                     self.text().replace("restart_renderer=vulkan", "restart_renderer=software"),
                     self.text() + self.text(), ""):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                _state(text, "cancel", True)

    def test_only_timestamp_may_change_in_previous_campaign(self):
        before = {"SavedAtUtc": "old", "Galaxy": {"Seed": 1, "Fleets": [{"Id": 3}]}, "SimulationDays": 4}
        after = copy.deepcopy(before);after["SavedAtUtc"]="new"
        _unchanged(before, after)
        for changed in ({**after, "SimulationDays": 5},
                        {**after, "Galaxy": {"Seed": 2, "Fleets": [{"Id": 3}]}},
                        {**after, "Galaxy": {"Seed": 1, "Fleets": []}}):
            with self.assertRaises(RuntimeError): _unchanged(before, changed)
        self.assertEqual(before["SavedAtUtc"], "old")

if __name__ == "__main__": unittest.main()
