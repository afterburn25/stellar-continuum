"""Development-only Windows exporter. The produced native runtime needs no Python/toolchain."""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SYSTEM_DLLS = {"kernel32.dll", "user32.dll", "advapi32.dll", "shell32.dll", "ole32.dll", "oleaut32.dll", "ws2_32.dll", "bcrypt.dll", "ntdll.dll", "msvcrt.dll", "ucrtbase.dll", "version.dll"}

def run(args, *, env=None, cwd=ROOT, capture=False, timeout=300):
    command = [str(a) for a in args]
    command[0] = shutil.which(command[0], path=(env or os.environ).get("PATH")) or command[0]
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            capture_output=capture, timeout=timeout, check=True)
    return result.stdout if capture else ""

def build_environment():
    if platform.system() != "Windows":
        raise RuntimeError("This export platform currently supports Windows x64 hosts only")
    env = os.environ.copy()
    local_tools = ROOT / ".tools/build-tools/Scripts"
    if local_tools.exists():
        env["PATH"] = str(local_tools) + os.pathsep + env.get("PATH", "")
    if not shutil.which("cl", path=env.get("PATH")):
        vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        if not vswhere.is_file():
            raise RuntimeError("Developer build requires the Visual C++ x64 toolchain; players do not")
        install = run([vswhere, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], capture=True).strip()
        setup = Path(install) / "Common7/Tools/VsDevCmd.bat"
        if not install or not setup.is_file() or any(c in str(setup) for c in '\r\n"&|<>'):
            raise RuntimeError("No valid Visual C++ developer environment found")
        # Only a verified toolchain-owned path enters cmd. Never log the captured environment.
        command = f'call "{setup}" -arch=x64 -host_arch=x64 >nul && set'
        # cmd uses its own quoting grammar; Python's argv quoting escapes embedded
        # quotes in a way cmd does not understand. This command contains only the
        # validated VS path above and fixed flags, never user arguments.
        text = subprocess.run('cmd.exe /d /s /c "' + command + '"', cwd=ROOT,
                              env=env, capture_output=True, text=True, check=True,
                              timeout=60).stdout
        for line in text.splitlines():
            if "=" in line and not line.startswith("="):
                key, value = line.split("=", 1)
                if key.upper() == "PATH": key = "PATH"
                env[key] = value
    for tool in ("cmake", "ninja", "cl", "dumpbin"):
        if not shutil.which(tool, path=env.get("PATH")):
            raise RuntimeError(f"Developer tool not found: {tool}. See docs/engine/WINDOWS_EXPORT.md")
    return env

def native_build(preset, env):
    run(["cmake", "--fresh", "--preset", preset], env=env)
    run(["cmake", "--build", "--preset", preset, "--parallel", "4"], env=env)
    run(["ctest", "--preset", preset], env=env)
    suffix = {"windows-testing": "testing", "windows-development": "development", "windows-headless": "headless"}[preset]
    directory = ROOT / "build-native" / suffix
    test_env = dict(env, STELLAR_NATIVE_EXE=str(directory / "stellar-continuum.exe"))
    run([sys.executable, ROOT / "tools/stellar-export/test_export.py", "-v"], env=test_env)
    return directory

def executable_dependencies(executable, env):
    data = executable.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ": raise RuntimeError("Export is not a Windows PE executable")
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if offset + 6 > len(data) or data[offset:offset+4] != b"PE\0\0" or struct.unpack_from("<H", data, offset+4)[0] != 0x8664:
        raise RuntimeError("Export executable is not AMD64/x86_64")
    output = run(["dumpbin", "/nologo", "/dependents", executable], env=env, capture=True)
    dependencies = sorted(set(re.findall(r"(?im)^\s+([a-z0-9_.-]+\.dll)\s*$", output)))
    if not dependencies: raise RuntimeError("Could not identify executable imports")
    unsupported = [name for name in dependencies if name.lower() not in SYSTEM_DLLS and not name.lower().startswith("api-ms-win-")]
    if unsupported: raise RuntimeError("Unpackaged runtime dependencies: " + ", ".join(unsupported))
    return dependencies

def hashes(folder):
    return [{"path": p.relative_to(folder).as_posix(), "bytes": p.stat().st_size,
             "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in sorted(folder.rglob("*")) if p.is_file() and p.relative_to(folder).as_posix() != "build-manifest.json"]

def validate_manifest(folder):
    folder = folder.resolve()
    manifest = json.loads((folder / "build-manifest.json").read_text(encoding="utf-8"))
    expected = manifest["files"]
    if len({row["path"] for row in expected}) != len(expected): raise RuntimeError("Duplicate manifest paths")
    for row in expected:
        relative = Path(row["path"])
        path = folder / relative
        if relative.is_absolute() or ".." in relative.parts or not path.resolve().is_relative_to(folder):
            raise RuntimeError("Unsafe manifest path")
        if any(parent.is_symlink() or parent.is_junction() for parent in [path, *path.parents] if parent != folder and parent.is_relative_to(folder)):
            raise RuntimeError("Linked files are not allowed in runtime exports")
    if hashes(folder) != expected: raise RuntimeError("Export files do not match the manifest")
    for row in expected:
        path = Path(row["path"])
        if path.suffix.lower() in {".cs", ".cpp", ".h", ".hpp", ".py", ".obj", ".lib", ".gd", ".gdshader"}:
            raise RuntimeError("Development-only file in runtime export")
        if path.suffix.lower() == ".pdb" and not manifest["includeSymbols"]:
            raise RuntimeError("Symbols in a public headless preset")
    required=["stellar-continuum.exe","Configuration/runtime-config.json",*manifest.get("requiredRuntimeFiles",[])]
    if any(not (folder / name).is_file() for name in required):
        raise RuntimeError("Missing required runtime executable, configuration or data")
    return manifest

def validate_fresh_campaign(data, report):
    # This diagnostic currently supports the existing default founding profile.
    # Full simulation-state semantics are checked against C# by fresh_campaign_tests.
    if (data.get("format") != "stellar-fresh-campaign-v1" or
            data.get("phase") != "fresh-campaign-before-first-tick" or
            data.get("playerSaveCompatible") is not False or
            data.get("gameplayParity") is not False or
            report.get("freshCampaignInitialized") is not True or
            report.get("mode") != "fresh-campaign" or
            report.get("gameplayParity") is not False or
            report.get("playerSaveCompatible") is not False):
        raise RuntimeError("Fresh campaign diagnostic boundary is missing")
    civilizations = {row["id"]: row for row in data["civilizations"]}
    players = [row for row in civilizations.values() if row["isPlayer"]]
    if len(civilizations) != len(data["civilizations"]) or len(players) != 1:
        raise RuntimeError("Fresh campaign has duplicate civilization IDs or invalid player count")
    player = players[0]
    if data["playerCivilizationId"] != player["id"] or report["playerCivilizationId"] != player["id"]:
        raise RuntimeError("Fresh campaign player identity mismatch")
    for name in ("economies", "technologies", "constructionStates", "shipyards", "knowledge"):
        rows = data[name]
        if len(rows) != len(civilizations) or {row["civilizationId"] for row in rows} != set(civilizations):
            raise RuntimeError("Fresh campaign is missing per-civilization " + name)
    if (report["systems"] != len(data["systems"]) or data["count"] != len(data["systems"]) or
            report["seed"] != data["seed"] or
            report["foundingCivilizations"] != len(civilizations) or
            report["planetaryBodies"] != len(data["planetaryBodies"]) or
            report["seededColonies"] != len(data["colonies"]) or
            report["seededEconomies"] != len(data["economies"]) or
            report["seededFleets"] != len(data["fleets"]) or
            report["seededTechnologies"] != len(data["technologies"]) or
            report["seededShipyards"] != len(data["shipyards"]) or
            report["seededKnowledgeObservers"] != len(data["knowledge"])):
        raise RuntimeError("Fresh campaign summary does not match its complete state")
    system_ids = {row["id"] for row in data["systems"]}
    if len(system_ids) != len(data["systems"]) or any(c["homeSystemId"] not in system_ids for c in civilizations.values()):
        raise RuntimeError("Fresh campaign has duplicate systems or missing home systems")
    # The source only seeds fleets for WarpCapable civilizations. The default
    # founding profile creates PreWarp and AncientSpacefaring civilizations, so
    # an empty fleet collection is correct; inventing starter ships is not.
    if any(c["developmentStage"] not in (0, 2) for c in civilizations.values()) or data["fleets"]:
        raise RuntimeError("Fresh campaign fleets do not match the default founding stages")
    for state in data["economies"]:
        ancient = civilizations[state["civilizationId"]]["isSeededAncient"]
        expected = dict(civilizationId=state["civilizationId"],
                        credits=50000 if ancient else 500, industry=25000 if ancient else 200,
                        science=10000 if ancient else 0, lastCreditsPerSecond=0,
                        lastIndustryPerSecond=0, lastSciencePerSecond=0,
                        lastResearchSpendingPerDay=0, lastResearchFundingFraction=1,
                        operatingArrears=0, lastBaseOperationsFundingFraction=1, industryPriority=None)
        if state != expected:
            raise RuntimeError("Fresh campaign economy seed payload mismatch")
    projects = {"research_network", "industrial_automation", "orbital_launch_complex",
                "orbital_shipyard", "asteroid_resource_network", "warp_test_facility"}
    for state in data["constructionStates"]:
        completed = projects if civilizations[state["civilizationId"]]["isSeededAncient"] else set()
        if (set(state.get("completedProjectIds", [])) != completed or
                len(state.get("completedProjectIds", [])) != len(completed) or
                state.get("activeProjectId", "missing") is not None or
                state.get("activeProjectProgress") != 0 or
                state.get("activeProjectAuthorizationCredits") != 0 or
                state.get("queuedProjects") != []):
            raise RuntimeError("Fresh campaign construction seed payload mismatch")
    legacy_ids = {"orbital_industry", "fusion_propulsion", "deep_space_sensors",
                  "exotic_field_theory", "warp_field_control", "prototype_warp_drive"}
    for state in data["technologies"]:
        expected = legacy_ids if civilizations[state["civilizationId"]]["developmentStage"] == 2 else set()
        if (set(state["completedTechnologyIds"]) != expected or
                len(state["completedTechnologyIds"]) != len(expected) or
                state["activeResearchId"] is not None or state["activeResearchProgress"] != 0):
            raise RuntimeError("Fresh campaign legacy research seed mismatch")
    for state in data["shipyards"]:
        if (state["nextOrderSequence"] != 1 or state["activeOrderId"] is not None or
                state["activeDesignId"] is not None or state["queuedBuilds"] or
                state["activeBuildProgress"] != 0 or state["reservedPopulationMillions"] != 0 or
                state.get("activeAuthorizationCredits") != 0 or
                state.get("reservedPopulationSpeciesId", "missing") is not None or
                state.get("reservedPopulationSourceColonyId", "missing") is not None):
            raise RuntimeError("Fresh campaign contains unexpected shipyard work")
    if data["coreObservers"]:
        raise RuntimeError("Fresh campaign has prematurely unlocked the galactic core")
    for observer in data["knowledge"]:
        home = civilizations[observer["civilizationId"]]["homeSystemId"]
        surveys = {row["systemId"]: row for row in observer["surveys"]}
        if (observer["coreAccess"] or observer["coreDiscovered"] or observer["knownCivilizations"] or
                set(observer["knownSystems"]) != set(surveys) or home not in surveys or
                surveys[home]["level"] != 3 or surveys[home]["progress"] != 1):
            raise RuntimeError("Fresh campaign home survey or hidden knowledge mismatch")
        if any(row["level"] != 1 or row["progress"] != 0 for sid, row in surveys.items() if sid != home):
            raise RuntimeError("Fresh campaign sensor contacts were incorrectly fully surveyed")

def relocated_smoke(folder):
    # Run an independent copy with only Windows system paths, no repository/toolchain cwd.
    with tempfile.TemporaryDirectory(prefix="stellar-export-smoke-") as temporary:
        root = Path(temporary); copy = root / "runtime"; shutil.copytree(folder, copy)
        env = {key: value for key, value in os.environ.items() if key.upper() in {"SYSTEMROOT", "WINDIR", "TEMP", "TMP", "USERPROFILE", "LOCALAPPDATA"}}
        windows = env.get("SystemRoot", env.get("SYSTEMROOT", r"C:\Windows"))
        env["PATH"] = str(Path(windows) / "System32")
        exe = copy / "stellar-continuum.exe"
        first = json.loads(run([exe, "--headless", "--systems", "500", "--ticks", "5", "--workers", "2", "--save", root / "roundtrip.scf"], cwd=root, env=env, capture=True, timeout=30))
        second = json.loads(run([exe, "--headless", "--ticks", "5", "--workers", "1", "--load", root / "roundtrip.scf"], cwd=root, env=env, capture=True, timeout=30))
        reference = json.loads(run([exe, "--headless", "--systems", "500", "--ticks", "10", "--workers", "4"], cwd=root, env=env, capture=True, timeout=30))
        if second["checkpointHash"] != reference["checkpointHash"] or second["distanceSum"] != reference["distanceSum"] or first["completedTicks"] != 5:
            raise RuntimeError("Relocated headless save/restore or worker determinism failed")
        catalog_path = root / "surface-support-catalog.json"
        galaxy=json.loads(run([exe,"--headless","--generate-galaxy","--seed-colonies","--systems","500","--catalog-output",catalog_path],cwd=root,env=env,capture=True,timeout=30))
        if galaxy["systems"]!=500 or galaxy["solBodies"]!=10 or galaxy["planetaryBodies"]<=10:
            raise RuntimeError("Relocated runtime catalog generation failed")
        if Path(galaxy["assetPath"]).resolve() != (copy/"Data/astronomy/hyg-nearby-500-v1.json").resolve():
            raise RuntimeError("Export used catalog outside its runtime directory")
        if galaxy["plannedHomeworlds"]!=7 or galaxy["foundingCivilizations"]!=7:
            raise RuntimeError("Relocated civilization founding failed")
        if galaxy["seededColonies"]!=9 or galaxy["seededEconomies"]!=7:
            raise RuntimeError("Relocated colony and starting budget seeding failed")
        if not galaxy.get("logisticsPreview"):
            raise RuntimeError("Relocated seeded-colony logistics preview was not reported")
        if not catalog_path.is_file() or Path(galaxy["catalogOutput"]).resolve() != catalog_path.resolve():
            raise RuntimeError("Relocated runtime did not produce the requested surface support catalog")
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        colony_ids = {colony["id"] for colony in catalog["colonies"]}
        support = catalog["colonySupport"]
        if len(support) != 9 or {profile["colonyId"] for profile in support} != colony_ids:
            raise RuntimeError("Relocated colony support profiles do not match seeded colonies")
        logistics=catalog.get("logistics",[])
        if not catalog.get("logisticsPreview") or len(logistics)!=7 or {entry["civilizationId"] for entry in logistics}!={economy["civilizationId"] for economy in catalog["economies"]}:
            raise RuntimeError("Relocated logistics preview rows do not match seeded economies")
        for entry in logistics:
            network=entry["homeNetwork"]
            flow=network["dailyFlow"]
            if entry["economyLogistics"]["civilizationId"]!=entry["civilizationId"] or entry["coverage"]["civilizationId"]!=entry["civilizationId"] or network["civilizationId"]!=entry["civilizationId"]:
                raise RuntimeError("Relocated logistics preview civilization identity mismatch")
            if flow["totalAllocatedPerDay"]!=network["totalAllocatedPerDay"] or flow["totalUnmetDemandPerDay"]!=network["totalUnmetDemandPerDay"]:
                raise RuntimeError("Relocated logistics preview flow totals mismatch")
        if any(profile["surface"]["supply"] != 2 or profile["surface"]["demand"] != 0 for profile in support):
            raise RuntimeError("Relocated colony support baseline allocation failed")
        by_id = {colony["id"]: colony for colony in catalog["colonies"]}
        for profile in support:
            colony = by_id[profile["colonyId"]]
            habitat, turnover = profile["habitat"], profile["turnover"]
            if (habitat["colonyId"], habitat["civilizationId"], habitat["systemId"], habitat["speciesId"]) != (colony["id"], colony["civilizationId"], colony["systemId"], colony["populationSpeciesId"]):
                raise RuntimeError("Relocated habitat support identity mismatch")
            if turnover["colonyId"] != colony["id"] or turnover["speciesId"] != colony["populationSpeciesId"] or turnover["effectiveGrowthPaceFactor"] <= 0 or habitat["typicalDayMetabolicDemandMillions"] <= 0 or habitat["adultBiomassMillionKg"] <= 0:
                raise RuntimeError("Relocated biology support values are invalid")
        earth = next(profile for profile in support if by_id[profile["colonyId"]]["name"] == "Earth")
        if earth["turnover"]["planetaryBodyId"] != 3 or earth["turnover"]["naturalEnvironmentTurnoverFactor"] != 1 or earth["turnover"]["environmentalPressureApplied"]:
            raise RuntimeError("Relocated Earth biology support mismatch")
        for name, body in (("Luna", 9), ("Mars", 4)):
            profile = next(profile for profile in support if by_id[profile["colonyId"]]["name"] == name)
            habitat = profile["habitat"]
            if profile["turnover"]["planetaryBodyId"] != body or sum(habitat[key] for key in ("gravityMitigationPopulationMillions", "thermalControlPopulationMillions", "pressureControlPopulationMillions", "sealedHabitatPopulationMillions", "artificialBiospherePopulationMillions", "radiationShieldingPopulationMillions")) <= 0:
                raise RuntimeError("Relocated dependent settlement biology support mismatch")
        if galaxy.get("colonyBiologyPreview") is not True:
            raise RuntimeError("Relocated runtime did not report colony biology previews")
        if galaxy.get("surfaceSupportPreview") is not True:
            raise RuntimeError("Relocated runtime did not report surface support previews")
        campaign_path = root / "fresh-campaign.json"
        fresh = json.loads(run([exe,"--headless","--seed-campaign","--systems","500",
                                "--catalog-output",campaign_path],cwd=root,env=env,capture=True,timeout=30))
        if Path(fresh["assetPath"]).resolve() != (copy/"Data/astronomy/hyg-nearby-500-v1.json").resolve():
            raise RuntimeError("Fresh campaign used data outside the relocated runtime")
        validate_fresh_campaign(json.loads(campaign_path.read_text(encoding="utf-8")), fresh)
        return {"relocatedLaunch": True, "restrictedPath": True, "checkpointRoundtrip": True,"relocatedGalaxyGeneration":True,
                "relocatedCivilizationFounding":True,"relocatedColonySeeding":True,"surfaceSupportPreview":True,
                "relocatedFreshCampaign":True,
                "cleanMachineTest": "Separate machine/VM still required; restricted-PATH test is not full clean-machine certification"}

def export(preset_name):
    config = json.loads((ROOT / "export/stellar-presets.json").read_text(encoding="utf-8"))
    preset = config["presets"].get(preset_name)
    if preset is None: raise RuntimeError("Unknown export preset")
    if "blocked" in preset: raise RuntimeError(preset["blocked"])
    env = build_environment(); directory = native_build(preset["configurePreset"], env)
    exe = directory / "stellar-continuum.exe"; dependencies = executable_dependencies(exe, env)
    version = json.loads((ROOT / "export/runtime-config.json").read_text(encoding="utf-8"))
    commit = run(["git", "rev-parse", "HEAD"], capture=True).strip()
    dirty = bool(run(["git", "status", "--porcelain", "--untracked-files=normal"], capture=True).strip())
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = ROOT / "Builds/Windows" / f"StellarContinuum-{preset_name}-{commit[:8]}-{stamp}"
    output.mkdir(parents=True, exist_ok=False)
    try:
        shutil.copy2(exe, output / exe.name)
        (output / "Configuration").mkdir()
        shutil.copy2(ROOT / "export/runtime-config.json", output / "Configuration/runtime-config.json")
        runtime_files={
            "Data/astronomy/hyg-nearby-500-v1.json":"data/astronomy/hyg-nearby-500-v1.json",
            "Data/astronomy/README.md":"data/astronomy/README.md",
            "Licenses/nlohmann-JSON-MIT.txt":"third_party/nlohmann/LICENSE.MIT",
            "Licenses/dotnet-MIT.txt":"third_party/dotnet/LICENSE.TXT",
        }
        for relative,source in runtime_files.items():
            destination=output/relative; destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(ROOT/source,destination)
        (output / "README.txt").write_text(
            f"Stellar Engine {version['engineVersion']} headless migration.\n"
            f"Game reference {version['gameVersion']}.\n"
            "Run stellar-continuum.exe --headless for the foundation check. This is not the graphical game.\n"
            "Fresh initialization: stellar-continuum.exe --headless --seed-campaign --systems 500 --catalog-output fresh.json\n"
            "Supported sizes: 250, 500, 1000, 2500. Use --help for seed, species and civilization options.\n"
            "Fresh output is diagnostic campaign state before its first tick, not a player save. Existing output files are preserved.\n"
            "Windows 10/11 x64 required. No Godot, .NET, compiler, CMake, Ninja, Vulkan SDK or Python required at runtime.\n",
            encoding="utf-8")
        if preset["includeSymbols"]:
            for symbol in directory.glob("stellar-continuum*.pdb"): shutil.copy2(symbol, output / symbol.name)
        manifest = {"schemaVersion": 1, "gameVersion": version["gameVersion"], "engineVersion": version["engineVersion"],
                    "sourceCommit": commit, "sourceDirty": dirty, "contentVersion": "stellar-catalog-1", "preset": preset_name,
                    "configuration": preset["configurePreset"], "architecture": "x86_64", "mode": preset["mode"],
                    "includeSymbols": preset["includeSymbols"], "builtAtUtc": stamp, "windowsSystemDependencies": dependencies,
                    "gameplayParity": False, "shadersRequired": False, "graphicalAssetsRequired": False,"requiredRuntimeFiles":list(runtime_files),
                    "files": hashes(output)}
        (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
        validate_manifest(output)
        smoke = relocated_smoke(output)
        if preset.get("benchmark"):
            smoke["foundationBenchmarks"] = [json.loads(run([exe, "--headless", "--systems", count, "--ticks", "100", "--workers", "4"], env=env, capture=True)) for count in (100, 500, 1000, 2500, 5000)]
            smoke["stellarGenerationBenchmarks"]=[json.loads(run([output/"stellar-continuum.exe","--headless","--generate-galaxy","--systems",count,"--repeat",10],env=env,capture=True)) for count in (250,500,1000,2500)]
            smoke["foundingBenchmarks"]=[json.loads(run([output/"stellar-continuum.exe","--headless","--generate-galaxy","--found-civilizations","--systems",count,"--repeat",3],env=env,capture=True)) for count in (250,500,1000,2500)]
            smoke["colonySeedingBenchmarks"]=[json.loads(run([output/"stellar-continuum.exe","--headless","--generate-galaxy","--seed-colonies","--systems",count,"--repeat",3],env=env,capture=True)) for count in (250,500,1000,2500)]
            smoke["freshCampaignBenchmarks"]=[json.loads(run([output/"stellar-continuum.exe","--headless","--seed-campaign","--systems",count,"--repeat",3],env=env,capture=True)) for count in (250,500,1000,2500)]
        (output.parent / (output.name+"-validation.json")).write_text(json.dumps(smoke, indent=2)+"\n", encoding="utf-8")
        archive = shutil.make_archive(str(output), "zip", output)
        print(json.dumps({"export": str(output), "archive": archive, "validation": smoke}, indent=2))
    except Exception:
        # Retain failed output for diagnosis, but never label/package it as validated.
        (output / "EXPORT_FAILED.txt").write_text("Export failed validation; do not distribute. See terminal diagnostics.\n", encoding="utf-8")
        raise

def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument("command", choices=["export","validate","build"]); parser.add_argument("target")
    args=parser.parse_args()
    if args.command=="export": export(args.target)
    elif args.command=="validate": validate_manifest(Path(args.target)); print("Manifest validated")
    else: native_build(args.target,build_environment())

if __name__ == "__main__":
    try: main()
    except Exception as error:
        print(f"Stellar export failed [{type(error).__name__}]: {error}", file=sys.stderr); sys.exit(1)
