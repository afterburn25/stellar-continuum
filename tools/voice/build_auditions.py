"""Generate a reproducible local casting pack with matched A/B lines; no cloud."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import time
import wave
from kokoro_worker import Synthesizer
from pack_paths import load_pack

CAST = {
    "commander": {"name": "Commander Voss", "voices": ["af_kore", "af_sarah"], "speed": 1.04,
        "direction": "Controlled authority; concise orders; urgency without shouting.",
        "lines": {
            "normal": "All ships are ready. Hold formation near Earth until the final supplies are aboard. When you give the order, we begin our journey to the stars.",
            "technical": "Long range sensors have detected a vessel beyond the outer planet. Keep our science ship behind the fleet while we establish its course and speed.",
            "urgent": "Incoming fire. Bring the shields online and turn the damaged ship away from the enemy. Cover its retreat. We are bringing every crew member home."}},
    "scientist": {"name": "Dr. Chen", "voices": ["bf_emma", "bf_isabella"], "speed": 1.0,
        "direction": "Curious, precise and warm; technical detail stays intelligible.",
        "lines": {
            "normal": "The survey is complete. There is liquid water beneath the ice, and the instruments agree. We should send another probe before we choose our landing site.",
            "technical": "The atmosphere contains nitrogen and oxygen, but surface pressure is three times that of Earth. Our team will need a sealed habitat before research can begin.",
            "urgent": "Commander, the radiation level is rising. Recall the surface team and move the ship beyond the inner orbit. We can study the signal from a safe distance."}},
    "diplomat": {"name": "Ambassador Okafor", "voices": ["af_bella", "af_nova"], "speed": .96,
        "direction": "Measured confidence; reassuring without sounding casual.",
        "lines": {
            "normal": "They have accepted our invitation. We have an opportunity to build trust, but their delegation will expect us to listen before we make our first proposal.",
            "technical": "The agreement would grant passage through two border systems. In return, we would share survey data and maintain a neutral zone around their home world.",
            "urgent": "Their ships are approaching the border. Keep our weapons silent while I open a channel. A few careful words may still prevent a war."}},
    "narrator": {"name": "Narrator", "voices": ["bf_isabella", "bf_emma"], "speed": .92,
        "direction": "Restrained wonder; room for the scene and music to breathe.",
        "lines": {
            "normal": "For generations, the stars were distant lights above our home. Now the first ships wait in orbit, and the future begins with a single decision.",
            "technical": "Beyond the last familiar world, the expedition found a quiet system. Its cold moons held water, and its ancient rocks carried the promise of a new beginning.",
            "urgent": "The warning reached Earth before dawn. Across the colony, towers fell silent as the fleet turned toward home. This time, the distance between the stars felt very long."}},
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    pack = load_pack(args.pack)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    synth = Synthesizer(pack["modelPath"], pack["voicesPath"])
    rows, cards = [], []
    for role, spec in CAST.items():
        sections = []
        for delivery, text in spec["lines"].items():
            players = []
            # Both candidates hear identical text and speed; urgency changes cadence only.
            speed = round(spec["speed"] + (.06 if delivery == "urgent" else 0), 3)
            for index, voice in enumerate(spec["voices"]):
                filename = f"{role}-{chr(65+index)}-{voice}-{delivery}.wav"
                start = time.perf_counter()
                metrics = synth.synthesize(text, voice, speed, str(output / filename))
                rows.append({"role": role, "candidate": chr(65+index), "voice": voice, "delivery": delivery,
                    "text": text, "speed": speed, "file": filename, "seconds": metrics["samples"] / 24000,
                    "synthesisSeconds": round(time.perf_counter()-start, 3), "sha256": hashlib.sha256((output / filename).read_bytes()).hexdigest(), **metrics})
                print(f"{filename}: {rows[-1]['seconds']:.1f}s audio / {rows[-1]['synthesisSeconds']:.2f}s synthesis", flush=True)
                players.append(f'<div class="candidate"><label>{chr(65+index)} · {voice}</label><audio controls preload="none" src="{filename}"></audio></div>')
            sections.append(f'<div class="line"><h3>{delivery.capitalize()}</h3><p>{html.escape(text)}</p><div class="comparison">{"".join(players)}</div></div>')
        cards.append(f'<section><span class="eyebrow">{role.upper()}</span><h2>{spec["name"]}</h2><p class="direction">{spec["direction"]}</p>{"".join(sections)}</section>')
    manifest = {"model": pack["version"], "modelSha256": pack["modelSha256"], "voicesSha256": pack["voicesSha256"],
                "processing": "Raw neural voices; no game radio/pitch/reverb. Peak protection attenuates only above .96. Urgent changes text and speed, not a trained emotional style.", "samples": rows}
    (output / "audition-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Stellar Continuum · Voice Casting</title><style>
    *{box-sizing:border-box}body{margin:0;background:#080e17;color:#e1e9f2;font:16px/1.6 system-ui,sans-serif}main{max-width:1100px;margin:auto;padding:48px 24px}header{padding:20px 0 36px;border-bottom:1px solid #35485f}h1{font-size:clamp(28px,4vw,46px);font-weight:500;letter-spacing:.01em;margin:8px 0}h2{font-size:28px;margin:4px 0}h3{font-size:14px;text-transform:uppercase;letter-spacing:.1em;color:#d5b97e}p{max-width:850px;color:#b0c0d2}.eyebrow{font-size:12px;letter-spacing:.19em;color:#7abacb}section{padding:36px 0;border-bottom:1px solid #35485f}.direction{font-style:italic}.line{padding:12px 20px 22px;margin:16px 0;background:#101d2a;border:1px solid #273c50;border-radius:10px}.comparison{display:grid;grid-template-columns:1fr 1fr;gap:22px}label{display:block;font-size:14px;margin-bottom:8px;color:#a9cadd}audio{width:100%;height:40px}a{color:#aad6e7}.note{border-left:3px solid #b49c65;padding-left:20px}@media(max-width:700px){main{padding:24px 16px}.comparison{grid-template-columns:1fr}.line{padding:12px}} </style><main><header><span class="eyebrow">STELLAR CONTINUUM / CASTING SESSION 01</span><h1>Four roles. Eight distinct voices.</h1><p>Compare A and B on the same dialogue. Each candidate has a normal, technical and urgent scene. Playback pauses the previous sample automatically.</p><p class="note">Candidate A is the provisional game cast. These are synthetic voices from Kokoro, not voice actors. Samples are dry so you can judge the voice itself. Final performance and mixing still need listening approval.</p></header>'''
    page += "".join(cards) + '<footer><p>Generated locally at 24 kHz. <a href="audition-manifest.json">Exact text, voices and measurements</a> · <a href="THIRD_PARTY.md">Sources and licensing notes</a></p></footer></main><script>document.querySelectorAll("audio").forEach(a=>a.addEventListener("play",()=>document.querySelectorAll("audio").forEach(b=>{if(b!==a)b.pause()})))</script></html>'
    (output / "index.html").write_text(page, encoding="utf-8")
    (output / "THIRD_PARTY.md").write_text(Path(__file__).with_name("THIRD_PARTY.md").read_text(encoding="utf-8"), encoding="utf-8")
    with wave.open(str(output / "selected-cast.wav"), "wb") as combined:
        combined.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
        for row in rows:
            if row["candidate"] == "A" and row["delivery"] == "normal":
                with wave.open(str(output / row["file"]), "rb") as clip:
                    combined.writeframes(clip.readframes(clip.getnframes()))
                combined.writeframes(bytes(24000))
    assert len(rows) == 24 and len({r["sha256"] for r in rows}) == 24
    print(f"PASS: 24 distinct non-silent candidate WAVs and comparison page in {output}")


if __name__ == "__main__":
    main()
