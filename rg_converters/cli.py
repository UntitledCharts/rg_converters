import sys
import os

from .detector import detect
from .version import __version__
from .converters import sus, usc, mmws, pjsk, bdon

FORMAT_NAMES = {
    "sus": "SUS",
    "usc": "USC",
    "mmw": "MMWS",
    "pjsk": "PJSK JSON",
    "lvd": "LevelData",
    "bdon": "BDON JSON",
}

OUTPUT_FORMATS = ["sus", "usc", "mmws", "pjsk", "chcy", "pysekai", "usekai"]


def _detect_file(path: str) -> tuple[str, str] | None:
    try:
        return detect(path)
    except Exception:
        return None


def _load_score(path: str, fmt: str, spec: str):
    if fmt == "sus":
        with open(path, "r", encoding="utf-8") as f:
            return sus.load(f)
    elif fmt == "bdon":
        return bdon.load(path)
    elif fmt == "usc":
        with open(path, "r", encoding="utf-8") as f:
            return usc.load(f)
    elif fmt == "mmw":
        with open(path, "rb") as f:
            return mmws.load(f)  # type: ignore[arg-type]
    elif fmt == "pjsk":
        return pjsk.load(path)
    elif fmt == "lvd":
        from .converters import LevelData

        base_spec = spec.replace("compress_", "")
        if base_spec == "chcy":
            print(
                "WARNING: Chart Cyanvas LevelData loading is not fully supported and may produce incorrect results."
            )
            if not _ask_yes_no("Continue anyway?"):
                sys.exit(0)
            with open(path, "rb") as f:
                return LevelData.chart_cyanvas.load(f)
        elif base_spec == "pysekai":
            with open(path, "rb") as f:
                return LevelData.next_sekai.load(f)
    raise ValueError(f"Unsupported input format: {fmt}")


def _ask_export_settings(fmt: str) -> dict:
    settings: dict = {}
    if fmt == "sus":
        print("SUS export settings (press Enter for defaults):")
        if _ask_yes_no("  Allow TIL layers?"):
            settings["allow_layers"] = True
        if _ask_yes_no("  Allow extended lanes?"):
            settings["allow_extended_lanes"] = True
        if _ask_yes_no("  Keep damage notes?"):
            settings["delete_damage"] = False
        if _ask_yes_no("  Keep note speed ratios?"):
            settings["keep_note_speed_ratios"] = True
        if _ask_yes_no("  Enable MEASUREBS (measures > 999)?"):
            settings["measure_extensions"] = True
    elif fmt == "pjsk":
        mid = _prompt("  Music ID (default 0): ")
        settings["music_id"] = int(mid) if mid else 0
    elif fmt == "mmws":
        sub = _prompt("  Sub-format (.mmws/.ccmmws/.unchmmws, default .mmws): ")
        if sub:
            settings["format"] = sub
    elif fmt in ("chcy", "pysekai", "usekai"):
        if _ask_yes_no("  Compress output (gzip)?"):
            settings["as_compressed"] = True
        else:
            settings["as_compressed"] = False
        if fmt == "pysekai":
            if _ask_yes_no("  Smooth guide fade?"):
                settings["smooth_guide_fade"] = True
            if _ask_yes_no("  Use guide layer?"):
                settings["use_guide_layer"] = True
    return settings


def _export_score(score, output_path: str, fmt: str, settings: dict | None = None):
    from .converters import LevelData

    settings = settings or {}
    if fmt == "sus":
        sus.export(output_path, score, **settings)
    elif fmt == "usc":
        usc.export(output_path, score)
    elif fmt == "mmws":
        mmws.export(output_path, score, **settings)
    elif fmt == "pjsk":
        pjsk.export(output_path, score, music_id=settings.get("music_id", 0))
    elif fmt == "chcy":
        LevelData.chart_cyanvas.export(output_path, score, **settings)
    elif fmt == "pysekai":
        LevelData.next_sekai.export(output_path, score, **settings)
    elif fmt == "usekai":
        LevelData.untitled_sekai.export(output_path, score, **settings)
    else:
        raise ValueError(f"Unsupported output format: {fmt}")


def _prompt(msg: str) -> str:
    try:
        return input(msg).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        sys.exit(0)


def _ask_yes_no(msg: str) -> bool:
    return _prompt(f"{msg} (y/n): ").lower() in ("y", "yes")


def _detect_or_ask(path: str) -> tuple[str, str]:
    result = _detect_file(path)
    if result:
        fmt, spec = result
        base_spec = spec.replace("compress_", "")
        if fmt == "lvd" and base_spec == "pysekai":
            print("Detected pjsekai LevelData - loading this format is not supported.")
        else:
            name = FORMAT_NAMES.get(fmt, fmt)
            label = f"{name} ({spec})" if spec else name
            if _ask_yes_no(f"Detected format: {label}. Correct?"):
                return fmt, spec

    print("Could not auto-detect or format was rejected.")
    print("Supported input formats: sus, bdon, usc, mmws, pjsk, lvd (chcy only)")
    fmt = _prompt("Enter format: ").lower()
    spec = ""
    if fmt == "lvd":
        spec = "chcy"
        print("Note: only Chart Cyanvas LevelData loading is supported.")
    elif fmt == "mmw":
        spec = _prompt("Enter specifier (base/chcy/unch): ").lower()
    return fmt, spec


def _get_output_format() -> str:
    print(f"Supported output formats: {', '.join(OUTPUT_FORMATS)}")
    return _prompt("Enter output format: ").lower()


def interactive():
    print(f"rg-converters v{__version__}")
    print()

    path = _prompt("Input file path: ").strip('"').strip("'")
    if not os.path.isfile(path):
        print(f"File not found: {path}")
        sys.exit(1)

    fmt, spec = _detect_or_ask(path)
    score = _load_score(path, fmt, spec)
    print(f"Loaded: {score.combo_count} combo")

    output_path = _prompt("Output file path: ").strip('"').strip("'")
    out_fmt = _get_output_format()
    settings = _ask_export_settings(out_fmt)
    _export_score(score, output_path, out_fmt, settings)
    print(f"Exported to {output_path} ({out_fmt})")


def main():
    import argparse

    parser = argparse.ArgumentParser(
        prog="rg-converters",
        description=f"rg-converters v{__version__} - Convert between PJSK charting formats",
    )
    parser.add_argument("input", nargs="?", help="Input file path")
    parser.add_argument("output", nargs="?", help="Output file path")
    parser.add_argument(
        "-f",
        "--format",
        choices=["sus", "bdon", "usc", "mmws", "pjsk", "lvd"],
        help="Input format (auto-detected if not specified)",
    )
    parser.add_argument(
        "--version", action="version", version=f"%(prog)s {__version__}"
    )

    args = parser.parse_args()

    if not args.input:
        interactive()
        return

    path = args.input
    if not os.path.isfile(path):
        print(f"File not found: {path}")
        sys.exit(1)

    if args.format:
        fmt, spec = args.format, ""
    else:
        fmt, spec = _detect_or_ask(path)

    score = _load_score(path, fmt, spec)
    print(f"Loaded: {score.combo_count} combo")

    if args.output:
        output_path = args.output
    else:
        output_path = _prompt("Output file path: ").strip('"').strip("'")

    out_fmt = _get_output_format()
    settings = _ask_export_settings(out_fmt)
    _export_score(score, output_path, out_fmt, settings)
    print(f"Exported to {output_path} ({out_fmt})")


if __name__ == "__main__":
    main()
