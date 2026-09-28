"""Apply the provisional audition-A cast without replacing SAPI fallback identities."""
import json
from pathlib import Path

path = Path(__file__).resolve().parents[2] / "data/voice_profiles/human.json"
profiles = json.loads(path.read_text(encoding="utf-8"))
casting = {
    "human_female_fleet_commander": "af_kore", "human_female_chief_scientist": "bf_emma",
    "human_female_diplomat": "af_bella", "human_female_narrator": "bf_isabella",
    "human_male_fleet_commander": "am_fenrir", "human_male_governor": "bm_george",
    "ship_computer": "af_nova", "human_operations_officer": "am_michael", "grey_diplomat": "af_nicole",
}
for profile in profiles:
    if profile["id"] not in casting:
        continue
    profile["neuralVoice"] = casting[profile["id"]]
    profile["preferredBackend"] = "offline-neural"
    profile["preferredModel"] = "kokoro-82m-v1.0"
    if profile["neuralVoice"].startswith("b"):
        profile["culture"] = "en-GB"
        profile["accent"] = "British English"
    # Retain natural identity in human performances. Alien/computer processing remains.
    if profile["species"] == "human" and profile["role"] != "computer":
        profile["pitch"] = 0
    if profile["id"] == "human_female_chief_scientist":
        profile["requirePreferredBackend"] = True
        profile["preferredVoice"] = "Microsoft Hazel"
        profile["pronunciations"] = {"exoplanet": "exo planet"}
path.write_text(json.dumps(profiles, indent=2) + "\n", encoding="utf-8")
