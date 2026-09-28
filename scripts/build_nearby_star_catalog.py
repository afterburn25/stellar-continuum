#!/usr/bin/env python3
"""Build the pinned, reproducible 500-system nearby-star catalogue from HYG v4.1."""

import argparse
import csv
import hashlib
import json
import math
import os
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SOURCE_URL = (
    "https://raw.githubusercontent.com/astronexus/HYG-Database/"
    "c7f7f883fe678cc7680169a50ccd7dcc49b060ce/hyg/CURRENT/hygdata_v41.csv"
)
SOURCE_SHA256 = "d9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd"
LIGHT_YEARS_PER_PARSEC = 3.26156
CATALOG_VERSION = "hyg-nearby-500-v1"
SYSTEM_COUNT = 500


@dataclass(frozen=True)
class SourceStar:
    hyg_id: int
    primary_hyg_id: int
    row: dict[str, str]
    distance_parsecs: float
    x: float
    y: float
    z: float


def normalized_text(row: dict[str, str], field: str) -> str:
    return " ".join((row.get(field) or "").split())


def parse_number(row: dict[str, str], field: str) -> float | None:
    try:
        return float(normalized_text(row, field))
    except ValueError:
        return None


def system_name(row: dict[str, str]) -> tuple[str, str]:
    for field in ("proper", "bf", "gl"):
        value = normalized_text(row, field)
        if value:
            return value, "proper" if field == "proper" else "catalogue"
    for field in ("hip", "hd"):
        value = normalized_text(row, field)
        if value:
            return f"{field.upper()} {value}", "catalogue"
    return f"HYG {normalized_text(row, 'id')}", "catalogue"


def read_pinned_source(source_path: Path) -> bytes:
    source_bytes = source_path.read_bytes()
    actual_sha256 = hashlib.sha256(source_bytes).hexdigest()
    if actual_sha256 != SOURCE_SHA256:
        raise ValueError(
            "source SHA-256 does not match pinned HYG commit: "
            f"{actual_sha256}"
        )
    return source_bytes


def parse_valid_stars(source_bytes: bytes) -> tuple[list[SourceStar], dict[int, SourceStar]]:
    valid_stars: list[SourceStar] = []
    stars_by_hyg_id: dict[int, SourceStar] = {}
    for row in csv.DictReader(source_bytes.decode("utf-8-sig").splitlines()):
        try:
            hyg_id = int(normalized_text(row, "id"))
            primary_hyg_id = int(normalized_text(row, "comp_primary"))
        except ValueError:
            continue
        values = [parse_number(row, field) for field in ("dist", "x", "y", "z")]
        if any(value is None or not math.isfinite(value) for value in values):
            continue
        distance_parsecs, x, y, z = values
        if distance_parsecs >= 100000 or distance_parsecs < 0:
            continue
        if distance_parsecs == 0 and hyg_id != 0:
            continue
        star = SourceStar(hyg_id, primary_hyg_id, row, distance_parsecs, x, y, z)
        valid_stars.append(star)
        stars_by_hyg_id[hyg_id] = star
    return valid_stars, stars_by_hyg_id


def normalized_coordinates(star: SourceStar) -> tuple[float, float, float]:
    if star.hyg_id == 0:
        return 0.0, 0.0, 0.0
    source_radius = math.sqrt(star.x * star.x + star.y * star.y + star.z * star.z)
    if source_radius == 0:
        raise ValueError(f"HYG {star.hyg_id} has a zero coordinate vector at nonzero distance")
    scale = star.distance_parsecs * LIGHT_YEARS_PER_PARSEC / source_radius
    return star.x * scale, star.y * scale, star.z * scale


def build_systems(valid_stars: list[SourceStar], stars_by_hyg_id: dict[int, SourceStar]) -> list[dict[str, Any]]:
    members_by_primary: defaultdict[int, list[SourceStar]] = defaultdict(list)
    for star in valid_stars:
        if star.primary_hyg_id in stars_by_hyg_id:
            members_by_primary[star.primary_hyg_id].append(star)

    systems: list[dict[str, Any]] = []
    for primary_hyg_id, members in members_by_primary.items():
        primary = stars_by_hyg_id[primary_hyg_id]
        name, name_kind = system_name(primary.row)
        ordered_components = [primary] + sorted(
            (member for member in members if member.hyg_id != primary_hyg_id),
            key=lambda member: member.hyg_id,
        )
        components = [
            {
                "name": system_name(component.row)[0],
                "spectralType": normalized_text(component.row, "spect"),
                "hygId": component.hyg_id,
            }
            for component in ordered_components
        ]
        x_light_years, y_light_years, z_light_years = normalized_coordinates(primary)
        systems.append(
            {
                "hygId": primary_hyg_id,
                "name": name,
                "nameKind": name_kind,
                "distanceParsecs": primary.distance_parsecs,
                "xLightYears": x_light_years,
                "yLightYears": y_light_years,
                "zLightYears": z_light_years,
                "spectralType": normalized_text(primary.row, "spect"),
                "components": components,
            }
        )

    systems.sort(key=lambda system: (system["distanceParsecs"], system["hygId"]))
    systems = systems[:SYSTEM_COUNT]
    seen_names: set[str] = set()
    for system in systems:
        casefolded_name = system["name"].casefold()
        if casefolded_name in seen_names:
            system["name"] += f" [HYG {system['hygId']}]"
        seen_names.add(casefolded_name)
    return systems


def validate_catalog(catalog: dict[str, Any]) -> None:
    systems = catalog["systems"]
    if len(systems) != SYSTEM_COUNT:
        raise ValueError(f"catalog must contain exactly {SYSTEM_COUNT} systems")
    if len({system["hygId"] for system in systems}) != SYSTEM_COUNT:
        raise ValueError("catalogue HYG identifiers are not unique")
    if len({system["name"].casefold() for system in systems}) != SYSTEM_COUNT:
        raise ValueError("catalogue system names are not unique")
    if len({(system["xLightYears"], system["yLightYears"], system["zLightYears"]) for system in systems}) != SYSTEM_COUNT:
        raise ValueError("catalogue system coordinates are not unique")

    for system in systems:
        coordinate_fields = ("distanceParsecs", "xLightYears", "yLightYears", "zLightYears")
        if not all(math.isfinite(system[field]) for field in coordinate_fields):
            raise ValueError(f"HYG {system['hygId']} has non-finite coordinates")
        light_year_radius = math.sqrt(
            system["xLightYears"] ** 2
            + system["yLightYears"] ** 2
            + system["zLightYears"] ** 2
        )
        expected_radius = system["distanceParsecs"] * LIGHT_YEARS_PER_PARSEC
        if abs(light_year_radius - expected_radius) >= 0.001:
            raise ValueError(f"HYG {system['hygId']} does not match its distance")
        if not system["components"]:
            raise ValueError(f"HYG {system['hygId']} has no stellar components")
        if system["components"][0]["hygId"] != system["hygId"]:
            raise ValueError(f"HYG {system['hygId']} does not lead its component list")

    contains_sol = any(
        system["hygId"] == 0
        and system["xLightYears"] == 0
        and system["yLightYears"] == 0
        and system["zLightYears"] == 0
        for system in systems
    )
    if not contains_sol:
        raise ValueError("catalogue does not contain the origin Sol entry")


def build_catalog(source_path: Path) -> str:
    source_bytes = read_pinned_source(source_path)
    source_sha256 = hashlib.sha256(source_bytes).hexdigest()
    valid_stars, stars_by_hyg_id = parse_valid_stars(source_bytes)
    catalog = {
        "catalogVersion": CATALOG_VERSION,
        "sourceUrl": SOURCE_URL,
        "sourceSha256": source_sha256,
        "epoch": "J2000",
        "attribution": (
            "HYG Database v4.1, Astronexus; nearby-system selection and coordinate "
            "normalization by Stellar Continuum"
        ),
        "license": "CC-BY-SA-4.0",
        "licenseUrl": "https://creativecommons.org/licenses/by-sa/4.0/",
        "systems": build_systems(valid_stars, stars_by_hyg_id),
    }
    validate_catalog(catalog)
    return json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", default="data/astronomy/hyg-nearby-500-v1.json", type=Path)
    parser.add_argument("--verify", action="store_true")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    expected_catalog = build_catalog(arguments.source)
    if arguments.verify:
        if not arguments.output.is_file() or arguments.output.read_bytes() != expected_catalog.encode("utf-8"):
            raise ValueError("output differs from reproducible catalog; verification left output unchanged")
        print("verified 500 systems; output unchanged")
        return

    if arguments.output.parent != Path("."):
        os.makedirs(arguments.output.parent, exist_ok=True)
    with arguments.output.open("w", encoding="utf-8", newline="\n") as output_file:
        output_file.write(expected_catalog)
    print("wrote", arguments.output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        raise SystemExit(f"catalog build failed: {error}") from None
