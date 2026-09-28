#!/usr/bin/env python3
"""Validate the Stellar Continuum visual asset contract using stdlib only."""

from __future__ import annotations

import json
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ICON_ROOT = ROOT / "assets" / "visual" / "icons"
SEMANTIC_NAV_ROOT = ROOT / "assets" / "visual" / "ui" / "navigation"
TOKENS = ROOT / "assets" / "visual" / "ui" / "visual_tokens.json"
THEME = ROOT / "assets" / "visual" / "ui" / "stellar_continuum_theme.tres"
MANIFEST = ROOT / "docs" / "ASSET_MANIFEST.md"
GUIDE = ROOT / "docs" / "VISUAL_STYLE_GUIDE.md"
PROJECT = ROOT / "project.godot"
MAIN_SCENE = ROOT / "scenes" / "Main.tscn"
RUNTIME_PALETTE = ROOT / "src" / "Game" / "Presentation" / "VisualPalette.cs"
ICON_LIBRARY = ROOT / "src" / "Game" / "Presentation" / "VisualIconLibrary.cs"
VISUAL_MAP = ROOT / "src" / "Game" / "Presentation" / "Main.VisualMap.cs"
INTEGRATED_VISUALS = ROOT / "src" / "Game" / "Presentation" / "IntegratedMain.Visuals.cs"
MAIN_MENU_BACKDROP = ROOT / "src" / "Game" / "Presentation" / "MainMenuBackdrop.cs"
MAIN_MENU_LAYER = ROOT / "src" / "Game" / "Presentation" / "MainMenuLayer.cs"
VISUAL_UI = ROOT / "src" / "Game" / "Presentation" / "VisualUi.cs"

ICON_FAMILIES = {
    "navigation": {
        "icon_nav_galaxy.svg",
        "icon_nav_home.svg",
        "icon_nav_system.svg",
        "icon_nav_ships.svg",
        "icon_nav_menu.svg",
        "icon_nav_close.svg",
        "icon_nav_back.svg",
        "icon_nav_zoom_in.svg",
        "icon_nav_zoom_out.svg",
    },
    "core": {
        "icon_hud_pause.svg",
        "icon_hud_speed.svg",
        "icon_hud_save.svg",
        "icon_hud_support.svg",
        "icon_action_research.svg",
        "icon_action_construction.svg",
        "icon_system_exploration.svg",
        "icon_system_logistics.svg",
        "icon_system_relations.svg",
        "icon_map_colony.svg",
        "icon_map_scout.svg",
        "icon_status_info.svg",
        "icon_status_warning.svg",
        "icon_status_success.svg",
        "icon_status_unknown.svg",
        "icon_status_hostile.svg",
    },
    "resources": {
        "icon_resource_credits.svg",
        "icon_resource_industry.svg",
        "icon_resource_science.svg",
    },
    "construction": {
        "icon_construction_research_network.svg",
        "icon_construction_industrial_automation.svg",
        "icon_construction_orbital_launch_complex.svg",
        "icon_construction_orbital_shipyard.svg",
        "icon_construction_warp_test_facility.svg",
    },
    "ships": {
        "icon_ship_science_vessel.svg",
        "icon_ship_patrol_corvette.svg",
        "icon_ship_colony_ship.svg",
    },
    "research": {
        "icon_research_locked.svg",
    },
    "map": {
        "icon_map_detected.svg",
        "icon_map_partially_surveyed.svg",
        "icon_map_fully_surveyed.svg",
    },
    "diplomacy": {
        "icon_diplomacy_contact.svg",
        "icon_diplomacy_peace.svg",
        "icon_diplomacy_war.svg",
        "icon_diplomacy_ceasefire.svg",
        "icon_diplomacy_access_granted.svg",
        "icon_diplomacy_access_denied.svg",
        "icon_diplomacy_trade.svg",
        "icon_diplomacy_agreement.svg",
        "icon_diplomacy_claim.svg",
        "icon_diplomacy_dispute.svg",
    },
    "combat": {
        "icon_combat_hold.svg",
        "icon_combat_defend.svg",
        "icon_combat_attack.svg",
        "icon_combat_retreat.svg",
        "icon_combat_damage.svg",
        "icon_combat_destroyed.svg",
    },
}
SEMANTIC_NAVIGATION = {
    "nav_research.svg", "nav_economy.svg", "nav_construction.svg", "nav_shipyard.svg",
    "nav_exploration.svg", "nav_colonization.svg", "nav_logistics.svg", "nav_relations.svg",
    "nav_inspection.svg", "nav_home.svg", "nav_galaxy.svg", "nav_settings.svg",
}

# The integration shell also carries a compact, color-coded rail family in the
# established icon tree.  It has a different 32-unit source grid from the
# 64-unit diplomacy rail, so keep it explicitly registered and validate it
# with the same semantic safety rules rather than treating it as v1 artwork.
SEMANTIC_ICON_FAMILIES = {
    "navigation": {
        "nav_colonies.svg", "nav_construction.svg", "nav_economy.svg", "nav_explore.svg",
        "nav_fleets.svg", "nav_galaxy.svg", "nav_home.svg", "nav_inspection.svg",
        "nav_logistics.svg", "nav_menu.svg", "nav_relations.svg", "nav_research.svg",
        "nav_zoom_in.svg", "nav_zoom_out.svg",
    },
}

SVG_NAMESPACE = "http://www.w3.org/2000/svg"
ALLOWED_SVG_TAGS = {"svg", "g", "path", "circle", "ellipse", "rect", "line", "polyline", "polygon"}
ALLOWED_SVG_ATTRIBUTES = {
    "width", "height", "viewBox", "fill", "stroke", "stroke-width",
    "stroke-linecap", "stroke-linejoin", "aria-hidden", "transform",
    "d", "cx", "cy", "r", "rx", "ry", "x", "y", "x1", "x2", "y1", "y2", "points",
}
ALLOWED_SOURCE_COLORS = {"none", "#E6F0F6"}


def fail(message: str) -> None:
    print(f"visual-assets: ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def validate_svg(path: Path, require_neutral_stroke: bool = True) -> None:
    try:
        source = path.read_text(encoding="utf-8")
        if "<!DOCTYPE" in source or "<!ENTITY" in source or "<?" in source:
            fail(f"{path.relative_to(ROOT)} must not contain declarations or processing instructions")
        root = ET.fromstring(source)
    except (OSError, ET.ParseError) as exc:
        fail(f"{path.relative_to(ROOT)} is not valid XML: {exc}")

    if local_name(root.tag) != "svg":
        fail(f"{path.relative_to(ROOT)} root element is not svg")

    if root.get("viewBox") != "0 0 24 24":
        fail(f"{path.relative_to(ROOT)} must use viewBox='0 0 24 24'")
    if root.get("width") != "24" or root.get("height") != "24":
        fail(f"{path.relative_to(ROOT)} must declare width='24' height='24'")
    if root.get("stroke-width") != "1.8":
        fail(f"{path.relative_to(ROOT)} must use the v1 1.8 root stroke")
    if require_neutral_stroke and root.get("stroke") != "#E6F0F6":
        fail(f"{path.relative_to(ROOT)} must use the neutral source stroke #E6F0F6")
    if root.get("fill") != "none":
        fail(f"{path.relative_to(ROOT)} must use transparent root fill")
    for key in ("stroke-linecap", "stroke-linejoin"):
        if root.get(key) != "round":
            fail(f"{path.relative_to(ROOT)} must use rounded {key}")

    for element in root.iter():
        name = local_name(element.tag)
        if name not in ALLOWED_SVG_TAGS or element.tag != f"{{{SVG_NAMESPACE}}}{name}":
            fail(f"{path.relative_to(ROOT)} contains unsupported element {element.tag!r}")
        for key, value in element.attrib.items():
            # The compact v1 family has no CSS, references, events, fonts or animation.
            # An allowlist also rejects relative/file hrefs and style-based color overrides.
            if key not in ALLOWED_SVG_ATTRIBUTES:
                fail(f"{path.relative_to(ROOT)} contains unsupported attribute {key!r}")
            allowed_colors = ALLOWED_SOURCE_COLORS | ({"#8EA1B5"} if not require_neutral_stroke else set())
            if key in {"stroke", "fill"} and value not in allowed_colors:
                fail(
                    f"{path.relative_to(ROOT)} contains non-contract {key} color {value!r}"
                )
            if key in {"stroke-linecap", "stroke-linejoin"} and value != "round":
                fail(f"{path.relative_to(ROOT)} overrides rounded {key}")
            if key == "stroke-width" and value != "1.8":
                fail(f"{path.relative_to(ROOT)} overrides the v1 stroke width")


def validate_semantic_navigation_svg(path: Path, view_box: str = "0 0 64 64") -> None:
    try:
        source = path.read_text(encoding="utf-8")
        if "<!DOCTYPE" in source or "<!ENTITY" in source or "<?" in source:
            fail(f"{path.relative_to(ROOT)} must not contain declarations or processing instructions")
        root = ET.fromstring(source)
    except (OSError, ET.ParseError) as exc:
        fail(f"{path.relative_to(ROOT)} is not valid XML: {exc}")
    if local_name(root.tag) != "svg" or root.get("viewBox") != view_box:
        fail(f"{path.relative_to(ROOT)} must use viewBox='{view_box}'")
    allowed = ALLOWED_SVG_TAGS | {"defs", "linearGradient", "stop"}
    allowed_attributes = ALLOWED_SVG_ATTRIBUTES | {"id", "offset", "stop-color", "stop-opacity", "opacity", "xlink:href"}
    hex_color = re.compile(r"^#[0-9A-Fa-f]{3}(?:[0-9A-Fa-f]{3})?$")
    internal_paint = re.compile(r"url\(#([A-Za-z_][\w.-]*)\)")
    paint_ids = {element.get("id") for element in root.iter() if local_name(element.tag) == "linearGradient"}
    for element in root.iter():
        name = local_name(element.tag)
        if name not in allowed or element.tag != f"{{{SVG_NAMESPACE}}}{name}":
            fail(f"{path.relative_to(ROOT)} contains unsupported element {element.tag!r}")
        for key, value in element.attrib.items():
            if key not in allowed_attributes:
                fail(f"{path.relative_to(ROOT)} contains unsupported attribute {key!r}")
            if key in {"href", "xlink:href"} and not value.startswith("#"):
                fail(f"{path.relative_to(ROOT)} contains external reference {value!r}")
            if key in {"fill", "stroke", "stop-color"} and value != "none" and not hex_color.fullmatch(value):
                reference = internal_paint.fullmatch(value)
                if reference is None or reference.group(1) not in paint_ids:
                    fail(f"{path.relative_to(ROOT)} contains invalid paint reference {value!r}")
            if "on" == key[:2] or key == "style":
                fail(f"{path.relative_to(ROOT)} contains script/event styling attribute {key!r}")


def token_rgb(colors: dict, role: str) -> tuple[float, ...]:
    value = colors.get(role)
    if not isinstance(value, str) or not re.fullmatch(r"#[0-9A-F]{6}", value):
        fail(f"visual token {role!r} must be an uppercase #RRGGBB color")
    return tuple(int(value[index:index + 2], 16) / 255 for index in (1, 3, 5))


def validate_palette(colors: dict) -> None:
    entries = dict(re.findall(
        r"public static readonly Color (\w+) = Rgb\(([^)]+)\);",
        RUNTIME_PALETTE.read_text(encoding="utf-8"),
    ))
    expected = {"".join(part.title() for part in role.split("_")): role for role in colors}
    if set(entries) != set(expected):
        fail("runtime palette must mirror every canonical token color exactly once")
    for name, role in expected.items():
        try:
            actual = tuple(int(part.strip(), 0) / 255 for part in entries[name].split(","))
        except ValueError:
            fail(f"runtime palette {name} must contain literal RGB channels")
        if actual != token_rgb(colors, role):
            fail(f"runtime palette {name} differs from canonical token {role}")


def validate_theme(token_data: dict) -> None:
    sections: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for raw in THEME.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("["):
            match = re.fullmatch(r'\[sub_resource type="StyleBoxFlat" id="([^"]+)"\]', line)
            name = match[1] if match else line
            if name in sections:
                fail(f"duplicate Theme section {name}")
            current = sections.setdefault(name, {})
        elif current is not None and " = " in line:
            key, value = line.split(" = ", 1)
            if key in current:
                fail(f"duplicate Theme property {key}")
            current[key] = value

    def require(section: str, key: str, expected: str) -> None:
        if sections.get(section, {}).get(key) != expected:
            fail(f"Theme {section}/{key} must equal {expected}")

    def color(section: str, key: str, role: str, alpha: float = 1) -> None:
        literal = sections.get(section, {}).get(key, "")
        match = re.fullmatch(r"Color\(([^)]+)\)", literal)
        try:
            actual = tuple(float(part.strip()) for part in match[1].split(",")) if match else ()
        except ValueError:
            actual = ()
        expected = (*token_rgb(token_data["colors"], role), alpha)
        if len(actual) != 4 or any(not math.isclose(a, b, abs_tol=0.000001) for a, b in zip(actual, expected)):
            fail(f"Theme {section}/{key} must match token {role} with alpha {alpha}")

    styles = {
        "panel": ("surface_primary", .96, "keyline", 1),
        "button_normal": ("surface_secondary", .98, "keyline", 1),
        "button_hover": ("surface_raised", .98, "selected", 1),
        "button_pressed": ("surface_primary", 1, "focus", 1),
        "button_disabled": ("surface_primary", .72, "disabled", .7),
    }
    for name, (background, alpha, border, border_alpha) in styles.items():
        section = "StyleBoxFlat_" + name
        color(section, "bg_color", background, alpha)
        color(section, "border_color", border, border_alpha)
        radius = token_data["geometry"]["radius_standard" if name == "panel" else "radius_small"]
        for corner in ("top_left", "top_right", "bottom_right", "bottom_left"):
            require(section, "corner_radius_" + corner, str(radius))
        for side in ("left", "top", "right", "bottom"):
            require(section, "border_width_" + side, str(token_data["geometry"]["keyline_px"]))

    focus = "StyleBoxFlat_button_focus"
    color(focus, "border_color", "focus")
    require(focus, "draw_center", "false")
    for side in ("left", "top", "right", "bottom"):
        require(focus, "border_width_" + side, "2")
    for state in ("normal", "hover", "pressed", "disabled", "focus"):
        require("[resource]", "Button/styles/" + state, f'SubResource("StyleBoxFlat_button_{state}")')
    require("[resource]", "PanelContainer/styles/panel", 'SubResource("StyleBoxFlat_panel")')
    for key, role in {
        "Label/colors/font_color": "text_primary",
        "Button/colors/font_color": "text_primary",
        "Button/colors/font_hover_color": "text_primary",
        "Button/colors/font_pressed_color": "focus",
        "Button/colors/font_focus_color": "text_primary",
        "Button/colors/font_disabled_color": "text_muted",
    }.items():
        color("[resource]", key, role)
    for key in ("default_font_size", "Button/font_sizes/font_size", "Label/font_sizes/font_size"):
        require("[resource]", key, str(token_data["typography"]["ui_default_px"]))


def validate_contrast(colors: dict) -> None:
    def luminance(role: str) -> float:
        channels = token_rgb(colors, role)
        return sum(weight * (channel / 12.92 if channel <= .04045 else ((channel + .055) / 1.055) ** 2.4)
                   for weight, channel in zip((.2126, .7152, .0722), channels))

    # Actual normal-size text pairings. Disabled controls are deliberately excluded.
    pairs = [("text_primary", surface) for surface in ("surface_primary", "surface_secondary", "surface_raised")]
    pairs += [(role, "surface_primary") for role in ("text_secondary", "text_muted", "focus")]
    for foreground, background in pairs:
        values = sorted((luminance(foreground), luminance(background)))
        ratio = (values[1] + .05) / (values[0] + .05)
        if ratio < 4.5:
            fail(f"{foreground} on {background} contrast {ratio:.2f}:1 is below 4.5:1")


def require_contains(path: Path, snippets: tuple[str, ...]) -> None:
    text = path.read_text(encoding="utf-8")
    for snippet in snippets:
        if snippet not in text:
            fail(f"{path.relative_to(ROOT)} is missing required contract entry {snippet!r}")


def main() -> int:
    required = [
        ICON_ROOT,
        TOKENS,
        THEME,
        MANIFEST,
        GUIDE,
        PROJECT,
        MAIN_SCENE,
        RUNTIME_PALETTE,
        ICON_LIBRARY,
        VISUAL_MAP,
        INTEGRATED_VISUALS,
        MAIN_MENU_BACKDROP,
        MAIN_MENU_LAYER,
        VISUAL_UI,
    ]
    missing = [str(path.relative_to(ROOT)) for path in required if not path.exists()]
    if missing:
        fail("missing required visual resources: " + ", ".join(missing))

    expected_paths: list[Path] = []
    for family, expected_names in ICON_FAMILIES.items():
        family_root = ICON_ROOT / family
        if not family_root.is_dir():
            fail(f"missing icon family directory {family_root.relative_to(ROOT)}")
        actual_names = {
            path.name for path in family_root.glob("*.svg")
            if path.name not in SEMANTIC_ICON_FAMILIES.get(family, set())
        }
        if actual_names != expected_names:
            missing_icons = sorted(expected_names - actual_names)
            unexpected_icons = sorted(actual_names - expected_names)
            fail(
                f"{family} icon family mismatch; missing={missing_icons!r} "
                f"unexpected={unexpected_icons!r}"
            )
        expected_paths.extend(family_root / name for name in expected_names)

    if not SEMANTIC_NAV_ROOT.is_dir():
        fail(f"missing semantic navigation family directory {SEMANTIC_NAV_ROOT.relative_to(ROOT)}")
    semantic_paths = [SEMANTIC_NAV_ROOT / name for name in sorted(SEMANTIC_NAVIGATION)]
    if {path.name for path in SEMANTIC_NAV_ROOT.glob("*.svg")} != SEMANTIC_NAVIGATION:
        fail("semantic navigation family mismatch")

    semantic_icon_paths: list[Path] = []
    for family, expected_names in SEMANTIC_ICON_FAMILIES.items():
        family_root = ICON_ROOT / family
        actual_names = {path.name for path in family_root.glob("*.svg") if path.name in expected_names}
        if actual_names != expected_names:
            fail(f"semantic {family} icon family mismatch")
        semantic_icon_paths.extend(family_root / name for name in sorted(expected_names))

    unexpected_paths = set(ICON_ROOT.rglob("*.svg")) - set(expected_paths) - set(semantic_icon_paths)
    if unexpected_paths:
        fail("unregistered SVG icons: " + ", ".join(str(path.relative_to(ROOT)) for path in sorted(unexpected_paths)))
    for path in sorted(expected_paths):
        validate_svg(path, require_neutral_stroke=path.name != "icon_research_locked.svg")
    for path in semantic_paths:
        validate_semantic_navigation_svg(path)
    for path in semantic_icon_paths:
        validate_semantic_navigation_svg(path, "0 0 32 32")

    try:
        token_data = json.loads(TOKENS.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"visual token JSON is invalid: {exc}")

    if token_data.get("schema") != "stellar-continuum.visual-tokens.v1":
        fail("visual token schema must be stellar-continuum.visual-tokens.v1")
    if token_data.get("geometry", {}).get("icon_grid") != 24:
        fail("visual token icon_grid must remain 24 for v1")
    if token_data.get("geometry", {}).get("icon_stroke") != 1.8:
        fail("visual token icon_stroke must remain 1.8 for v1")
    if not isinstance(token_data.get("colors"), dict) or not token_data["colors"]:
        fail("visual tokens must define canonical colors")
    validate_palette(token_data["colors"])
    validate_theme(token_data)
    validate_contrast(token_data["colors"])

    icon_paths = re.findall(r'public const string \w+Path = "res://([^"\n]+)";', ICON_LIBRARY.read_text(encoding="utf-8"))
    if not icon_paths:
        fail("runtime icon library must export registered resource paths")
    for path in icon_paths:
        if ROOT / path not in expected_paths and ROOT / path not in semantic_paths:
            fail(f"runtime icon library references unregistered icon {path}")

    require_contains(
        THEME,
        (
            '[gd_resource type="Theme"',
            'Button/styles/normal',
            'Button/styles/hover',
            'Button/styles/pressed',
            'Button/styles/disabled',
            'Button/styles/focus',
            'PanelContainer/styles/panel',
            'Label/colors/font_color',
        ),
    )
    require_contains(
        PROJECT,
        (
            '[gui]',
            'theme/custom="res://assets/visual/ui/stellar_continuum_theme.tres"',
        ),
    )
    require_contains(
        MAIN_SCENE,
        (
            '[node name="MainMenuLayer" type="CanvasLayer" parent="."]',
            'layer = 100',
        ),
    )
    require_contains(
        RUNTIME_PALETTE,
        (
            'public static class VisualPalette',
            'public static readonly Color Canvas',
            'public static readonly Color Selected',
            'public static readonly Color Danger',
        ),
    )
    require_contains(
        ICON_LIBRARY,
        (
            'public static class VisualIconLibrary',
            'Visual asset could not be loaded',
            'icon_map_detected.svg',
            'icon_ship_patrol_corvette.svg',
            'icon_diplomacy_contact.svg',
        ),
    )
    require_contains(
        VISUAL_MAP,
        (
            'protected void DrawVisualMapOverlay()',
            'SystemSurveyLevel.PartiallySurveyed',
            'DrawVisualColonies',
            'DrawVisualPlayerFleets',
            'DrawRegionalReticle',
            'DrawRegionalSpace',
        ),
    )
    require_contains(
        INTEGRATED_VISUALS,
        (
            'public override void _Draw()',
            'DrawVisualMapOverlay();',
        ),
    )
    require_contains(
        MAIN_MENU_BACKDROP,
        (
            'public partial class MainMenuBackdrop : Control',
            'res://assets/visual/loading/stellar-continuum-splash.png',
            'DrawTextureRect(_artwork',
            'VisualPalette.Selected',
        ),
    )
    require_contains(
        MAIN_MENU_LAYER,
        (
            'var backdrop = new MainMenuBackdrop();',
            'VisualUi.Text("STELLAR", 48)',
            'VisualUi.Text("C O N T I N U U M", 23)',
            'VisualUi.Text(_main.UiBuildLabel, 10, VisualUi.Muted)',
            'button.Alignment = HorizontalAlignment.Left;',
        ),
    )
    # The menu delegates label styling to this helper. Check that it still applies
    # the requested size and color rather than requiring duplicate menu overrides.
    require_contains(
        VISUAL_UI,
        (
            'label.AddThemeFontSizeOverride("font_size", size);',
            'label.AddThemeColorOverride("font_color", color.Value);',
        ),
    )

    manifest_text = MANIFEST.read_text(encoding="utf-8")
    for path in sorted(expected_paths) + semantic_paths:
        if path in semantic_paths and "assets/visual/ui/navigation/nav_*.svg" in manifest_text:
            continue
        if path.name not in manifest_text:
            fail(f"asset manifest does not list {path.name}")
    for path in (RUNTIME_PALETTE, ICON_LIBRARY, VISUAL_MAP, MAIN_MENU_BACKDROP):
        if path.name not in manifest_text:
            fail(f"asset manifest does not list {path.name}")

    print(
        f"visual-assets: validated {len(expected_paths)} SVG icons plus {len(semantic_paths)} semantic navigation icons across "
        f"{len(ICON_FAMILIES)} families, token/palette/Theme value parity, text contrast, Godot Theme binding, "
        "runtime palette/icon loader, strategic map overlay, cinematic main-menu backdrop, "
        "style guide and manifest"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
