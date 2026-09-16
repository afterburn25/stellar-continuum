"""Regenerate approved navigation PNGs; never invoked by build or export."""
from importlib.metadata import version
from pathlib import Path


def main():
    if version("resvg_py") != "0.5.0":
        raise RuntimeError("Navigation regeneration requires resvg_py==0.5.0")
    from resvg_py import svg_to_bytes

    directory = Path(__file__).resolve().parents[2] / "assets/visual/ui/navigation"
    for name in ("research", "shipyard", "construction", "relations"):
        (directory / f"nav_{name}.png").write_bytes(svg_to_bytes(
            svg_path=str(directory / f"nav_{name}.svg"), width=256, height=256))


if __name__ == "__main__":
    main()
