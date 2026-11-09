"""Embed only the four approved UI images; never scan a ROM or asset directory."""
import argparse
from pathlib import Path

IMAGES = {
    "egg_png": "icon.png",
    "github_svg": "github-mark-white.svg",
    "xbox_svg": "controller-xbox.svg",
    "playstation_svg": "controller-playstation.svg",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent / "assets"
    lines = ["// Generated from the explicitly listed UI images. Do not edit.",
             "#pragma once", "namespace ui_images {"]
    for name, filename in IMAGES.items():
        data = (root / filename).read_bytes()
        lines.append(f"inline constexpr unsigned char {name}[] = {{")
        for offset in range(0, len(data), 24):
            lines.append("    " + ",".join(f"0x{v:02x}" for v in data[offset:offset + 24]) + ",")
        lines.append("};")
    lines.append("} // namespace ui_images\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
