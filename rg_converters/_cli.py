from __future__ import annotations

import json
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import Annotated, Any, NoReturn, Optional

import typer
from rich import box
from rich.console import Console
from rich.markup import escape
from rich.prompt import Confirm, Prompt
from rich.table import Table

from . import format as formats
from . import transforms
from ._rg_converters import UnsupportedFeatureError
from .core import Bpm, FeverChance, FeverStart, HolodoriFeverStart, HolodoriSkill, Score, Skill
from .detector import detect
from .format import Feature, Format, OptionInfo, Support
from .version import __version__


class Code(IntEnum):
    OK = 0
    UNEXPECTED = 1
    USAGE = 2
    NOT_DETECTED = 3
    LOAD_FAILED = 4
    EXPORT_FAILED = 5
    ABORTED = 130


class CliError(Exception):
    def __init__(self, message: str, code: Code) -> None:
        super().__init__(message)
        self.code = code


def fail(message: str, code: Code) -> NoReturn:
    raise CliError(message, code)


TRANSFORMS: dict[str, Callable[[Score], Score]] = {
    "shift": transforms.shift,
    "strip_layers": transforms.strip_layers,
    "strip_extended_lanes": transforms.strip_extended_lanes,
    "strip_speed_ratios": transforms.strip_speed_ratios,
    "flatten_speed_ratios_to_layers": transforms.flatten_speed_ratios_to_layers,
    "delete_fake_notes": transforms.delete_fake_notes,
    "delete_damage_notes": transforms.delete_damage_notes,
    "replace_extended_ease": transforms.replace_extended_ease,
    "replace_extended_guide_colors": transforms.replace_extended_guide_colors,
    "convert_holodori_events": transforms.convert_holodori_events,
    "close_open_slides": transforms.close_open_slides,
    "add_point_without_fade": transforms.add_point_without_fade,
    "sort_by_beat": transforms.sort_by_beat,
}

# The export option that keeps an "option" feature.
ALLOWING_OPTION = {
    ("sus", "layers"): "allow_layers",
    ("sus", "note_speed_ratios"): "allow_note_speed_ratios",
    ("sus", "measure_extensions"): "allow_measure_extensions",
    ("sus", "damage_notes"): "allow_damage",
}

TRUE = {"true", "1", "yes", "on"}
FALSE = {"false", "0", "no", "off"}
SUPPORT_ORDER: list[Support] = ["no", "option", "lossy", "yes"]


@dataclass
class Ui:
    json: bool = False
    quiet: bool = False
    yes: bool = False
    out: Console = field(init=False)
    err: Console = field(init=False)

    def __post_init__(self) -> None:
        self.out = Console(stderr=self.json, quiet=self.quiet, highlight=False)
        self.err = Console(stderr=True, highlight=False)

    @property
    def interactive(self) -> bool:
        return not (self.yes or self.quiet)

    def print(self, *objects: Any) -> None:
        self.out.print(*objects)

    def note(self, *objects: Any) -> None:
        """Beside the prompts, on stderr; nothing with --quiet."""
        if not self.quiet:
            self.err.print(*objects)

    def emit(self, data: dict[str, Any]) -> None:
        if self.json:
            print(json.dumps(data, ensure_ascii=False, indent=2))

    def ask(self, question: str, needed_for: str, default: Optional[str] = None) -> str:
        if not self.interactive:
            if default is not None:
                return default
            fail(f"{needed_for} is needed; pass it on the command line", Code.USAGE)
        try:
            if default is None:
                answer = Prompt.ask(question, console=self.err)
            else:
                answer = Prompt.ask(question, console=self.err, default=default, show_default=bool(default))
        except EOFError:
            fail(f"{needed_for} is needed; pass it on the command line", Code.USAGE)
        return answer.strip()

    def confirm(self, question: str, default: bool) -> bool:
        if not self.interactive:
            return default
        try:
            return Confirm.ask(question, console=self.err, default=default)
        except EOFError:
            return default

    def pick(self, title: str, choices: Sequence[Format], needed_for: str, default: Optional[int] = None) -> Format:
        if not self.interactive:
            if default is not None:
                return choices[default - 1]
            fail(f"{needed_for} is needed; pass it on the command line", Code.USAGE)
        self.err.print(format_table(title, choices, numbered=True))
        while True:
            answer = self.ask("Number", needed_for, str(default) if default is not None else None)
            if answer.isdigit() and 1 <= int(answer) <= len(choices):
                return choices[int(answer) - 1]
            self.err.print(f"[red]Pick a number from 1 to {len(choices)}.[/]")


def format_label(fmt: Format) -> str:
    return f"{fmt.id} [dim](deprecated)[/]" if fmt.deprecated else fmt.id


def format_table(title: str, choices: Sequence[Format], numbered: bool = False) -> Table:
    table = Table(title=title, box=box.ROUNDED, header_style="bold cyan", title_justify="left")
    if numbered:
        table.add_column("#", justify="right", style="bold")
    table.add_column("Format", no_wrap=True)
    table.add_column("Description")
    for i, fmt in enumerate(choices, 1):
        row = [format_label(fmt), escape(fmt.description)]
        table.add_row(*([str(i)] if numbered else []), *row)
    return table


def loadable() -> list[Format]:
    return [f for f in formats.all() if "load" in f.operations]


def exportable(show_deprecated: bool) -> list[Format]:
    return [f for f in formats.all() if "export" in f.operations and (show_deprecated or not f.deprecated)]


def require_format(id: str, operation: str) -> Format:
    try:
        fmt = formats.get(id)
    except ValueError as e:
        fail(str(e), Code.USAGE)
    if operation not in fmt.operations:
        fail(f"{fmt.id} can't be {'loaded' if operation == 'load' else 'exported'}", Code.NOT_DETECTED)
    return fmt


def read_input(ui: Ui, path: Optional[Path]) -> tuple[Path, bytes]:
    if path is None:
        path = Path(ui.ask("Chart to read", "The input file"))
    if not path.is_file():
        fail(f"{path} is not a file", Code.USAGE)
    try:
        return path, path.read_bytes()
    except OSError as e:
        fail(f"couldn't read {path}: {e.strerror or e}", Code.LOAD_FAILED)


def choose_input_format(ui: Ui, data: bytes, forced: Optional[str], confirm: bool) -> Format:
    if forced is not None:
        return require_format(forced, "load")
    candidates = [formats.get(id) for id in detect(data)]
    if not candidates:
        if not ui.interactive:
            fail("couldn't detect the format; pass --from", Code.NOT_DETECTED)
        ui.err.print("[yellow]Couldn't detect the format.[/]")
        return ui.pick("Load it as", loadable(), "The input format (--from)")
    if not confirm:
        return candidates[0]
    if not ui.interactive:
        ui.print(f"Detected {format_label(candidates[0])}")
        return candidates[0]
    if len(candidates) == 1:
        fmt = candidates[0]
        ui.err.print(format_table("Detected", [fmt]))
        if ui.confirm(f"Load it as {fmt.id}?", True):
            return fmt
        return ui.pick("Load it as", loadable(), "The input format (--from)")
    return ui.pick("The content fits more than one format", candidates, "The input format (--from)", 1)


def parse_pairs(pairs: Optional[list[str]], flag: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for pair in pairs or []:
        name, sep, value = pair.partition("=")
        if not sep or not name.strip():
            fail(f"{flag} {pair!r} should be NAME=VALUE", Code.USAGE)
        values[name.strip()] = value.strip()
    return values


def convert_value(fmt: Format, kind: str, info: OptionInfo, value: Any) -> Any:
    if not isinstance(value, str) or info.type == "str":
        return value
    if info.type == "bool":
        if value.lower() in TRUE:
            return True
        if value.lower() in FALSE:
            return False
        fail(f"{fmt.id} {kind} option {info.name} takes true or false, not {value!r}", Code.USAGE)
    try:
        return int(value)
    except ValueError:
        fail(f"{fmt.id} {kind} option {info.name} takes a whole number, not {value!r}", Code.USAGE)


def build_options(ui: Ui, fmt: Format, kind: str, given: dict[str, Any]) -> Any:
    infos = fmt.import_options if kind == "import" else fmt.export_options
    cls = fmt.ImportOptions if kind == "import" else fmt.ExportOptions
    flag = "-i" if kind == "import" else "-e"
    by_name = {info.name: info for info in infos}
    for name in given:
        if name not in by_name:
            known = ", ".join(by_name) or "none"
            fail(f"{fmt.id} has no {kind} option {name!r} (it has: {known})", Code.USAGE)
    values = {name: convert_value(fmt, kind, by_name[name], value) for name, value in given.items()}
    for info in infos:
        if info.required and info.name not in values:
            answer = ui.ask(f"{fmt.id} {kind} option [bold]{info.name}[/]", f"{fmt.id}'s {info.name} ({flag} {info.name}=...)")
            values[info.name] = convert_value(fmt, kind, info, answer)
    return cls(**values)


def load_chart(ui: Ui, fmt: Format, data: bytes, given: dict[str, Any], skip_unsupported: bool) -> Score:
    try:
        return fmt.loads(data, build_options(ui, fmt, "import", given))
    except UnsupportedFeatureError as e:
        table = Table(title="This file has things a score can't hold", box=box.ROUNDED,
                      header_style="bold yellow", title_justify="left")
        table.add_column("Unsupported")
        for line in str(e).splitlines():
            # The message opens with a heading line ("Unsupported ... level data (...):").
            if line.strip() and not line.rstrip().endswith(":"):
                table.add_row(escape(line.strip()))
        ui.note(table)
        if not any(o.name == "error_on_unsupported_feature" for o in fmt.import_options):
            fail(f"couldn't load as {fmt.id}", Code.LOAD_FAILED)
        if not (skip_unsupported or ui.confirm("Load it anyway, skipping or approximating these?", False)):
            fail("not loaded: the file has things the score can't hold (--skip-unsupported loads it anyway)",
                 Code.LOAD_FAILED)
        lenient = {**given, "error_on_unsupported_feature": False}
        return load_chart(ui, fmt, data, lenient, True)
    except CliError:
        raise
    except (ValueError, RuntimeError) as e:
        fail(f"couldn't load as {fmt.id}: {e}", Code.LOAD_FAILED)


def worst(supports: list[Support]) -> Support:
    return min(supports, key=SUPPORT_ORDER.index)


def displayed_features(used: list[Feature], formats_involved: Sequence[Format]) -> list[Feature]:
    """Note speed ratios count as layers unless sus (which holds them itself) is involved."""
    if any(f.id == "sus" for f in formats_involved):
        return used
    shown: list[Feature] = []
    layers = next(f for f in formats.FEATURES if f.id == "layers")
    for feature in used:
        feature = layers if feature.id == "note_speed_ratios" else feature
        if feature not in shown:
            shown.append(feature)
    return shown


def support_for(fmt: Format, feature: Feature, used_ids: set[str], merged: bool) -> Support:
    if feature.id != "layers" or not merged or "note_speed_ratios" not in used_ids:
        return fmt.supports(feature)
    parts: list[Support] = []
    if "layers" in used_ids:
        parts.append(fmt.supports("layers"))
    speed = fmt.supports("note_speed_ratios")
    parts.append("yes" if speed == "lossy" and fmt.supports("layers") == "yes" else speed)
    return worst(parts)


def export_status(fmt: Format, feature: Feature, support: Support, options: Any) -> Optional[str]:
    """What exporting does to a feature the chart uses; None when it's kept."""
    names = {o.name for o in fmt.export_options}
    if support == "yes":
        return None
    if support == "lossy":
        return "approximated"
    if support == "option":
        option = ALLOWING_OPTION.get((fmt.id, feature.id))
        if option is None:
            return "needs an export option"
        if getattr(options, option, False):
            return None
        if feature.id == "note_speed_ratios" and getattr(options, "allow_layers", False):
            return None
        return f"needs -e {option}=true"
    if feature.id == "layers" and "strip_layers" in names:
        return "stripped" if getattr(options, "strip_layers", False) else "export fails without -e strip_layers=true"
    if feature.id == "measure_extensions":
        return "export fails (measures past 999 can't be written)"
    return "lost"


def apply_transforms(ui: Ui, score: Score, named: list[str], pick: bool) -> tuple[Score, list[str]]:
    for name in named:
        if name not in TRANSFORMS:
            fail(f"unknown transform {name!r} (known: {', '.join(TRANSFORMS)})", Code.USAGE)
    chosen = list(dict.fromkeys(named))
    if pick:
        if not ui.interactive:
            fail("a bare --transform picks from a list, which needs a terminal; name them instead", Code.USAGE)
        names = list(TRANSFORMS)
        while True:
            table = Table(title="Transforms (applied in the order picked)", box=box.ROUNDED,
                          header_style="bold cyan", title_justify="left")
            table.add_column("#", justify="right", style="bold")
            table.add_column("Transform")
            table.add_column("Order", justify="right")
            for i, name in enumerate(names, 1):
                table.add_row(str(i), name, str(chosen.index(name) + 1) if name in chosen else "")
            ui.err.print(table)
            answer = ui.ask("Numbers to add or remove (Enter when done)", "The transform list", "")
            if not answer:
                break
            for token in answer.replace(",", " ").split():
                if not token.isdigit() or not 1 <= int(token) <= len(names):
                    ui.err.print(f"[red]{escape(token)} isn't a number from 1 to {len(names)}.[/]")
                    continue
                name = names[int(token) - 1]
                if name in chosen:
                    chosen.remove(name)
                else:
                    chosen.append(name)
    for name in chosen:
        try:
            score = TRANSFORMS[name](score)
        except (ValueError, RuntimeError) as e:
            fail(f"transform {name} failed: {e}", Code.EXPORT_FAILED)
    return score, chosen


def default_output(path: Path, fmt: Format) -> Path:
    known = {t for f in formats.all() for t in f.file_types}
    name = path.name
    stripped = True
    while stripped:
        stripped = False
        for suffix in sorted(known, key=len, reverse=True):
            if name.lower().endswith(suffix) and len(name) > len(suffix):
                name = name[: -len(suffix)]
                stripped = True
                break
    return path.with_name(name + fmt.file_types[0])


def handle(ui: Ui, body: Callable[[], None]) -> None:
    try:
        body()
    except CliError as e:
        if ui.json:
            print(json.dumps({"error": str(e), "exit_code": int(e.code)}, ensure_ascii=False, indent=2))
        else:
            ui.err.print(f"[red]error:[/] {escape(str(e))}")
        raise typer.Exit(int(e.code))
    except (KeyboardInterrupt, typer.Abort):
        ui.err.print("[yellow]aborted[/]")
        raise typer.Exit(int(Code.ABORTED))
    except typer.Exit:
        raise
    except Exception as e:
        ui.err.print(f"[red]unexpected error:[/] {escape(f'{type(e).__name__}: {e}')}")
        raise typer.Exit(int(Code.UNEXPECTED))


def version_callback(value: bool) -> None:
    if value:
        print(f"rg-converters {__version__}")
        raise typer.Exit()


app = typer.Typer(
    name="rg-converters",
    help=f"rg-converters {__version__}: convert rhythm game charts between formats.",
    no_args_is_help=True,
    add_completion=False,
    pretty_exceptions_enable=False,
    rich_markup_mode="rich",
)

JsonOption = Annotated[bool, typer.Option("--json", help="Print the result as JSON.")]
QuietOption = Annotated[bool, typer.Option("--quiet", "-q", help="Print nothing but errors; never ask.")]
YesOption = Annotated[bool, typer.Option("--yes", "-y", help="Don't ask for confirmation; take the defaults.")]
FromOption = Annotated[Optional[str], typer.Option("--from", "-f", metavar="FORMAT",
                                                   help="Load as this format, skipping detection.")]
ImportOption = Annotated[Optional[list[str]], typer.Option("--import-option", "-i", metavar="NAME=VALUE",
                                                           help="An import option of the input format (repeatable).")]
SkipUnsupportedOption = Annotated[bool, typer.Option("--skip-unsupported",
                                                     help="Load a file with unsupported things without asking.")]


@app.callback()
def root(version: Annotated[bool, typer.Option("--version", callback=version_callback, is_eager=True,
                                               help="Print the version.")] = False) -> None:
    pass


@app.command()
def convert(
    input: Annotated[Optional[Path], typer.Argument(help="The chart to convert.", show_default=False)] = None,
    output: Annotated[Optional[Path], typer.Argument(help="The file to write.", show_default=False)] = None,
    from_: FromOption = None,
    to: Annotated[Optional[str], typer.Option("--to", "-t", metavar="FORMAT", help="The format to write.")] = None,
    import_option: ImportOption = None,
    export_option: Annotated[Optional[list[str]], typer.Option("--export-option", "-e", metavar="NAME=VALUE",
                                                               help="An export option of the output format (repeatable).")] = None,
    transform: Annotated[Optional[list[str]], typer.Option("--transform", metavar="NAME",
                                                           help="Apply a transform before exporting (repeatable); "
                                                                "a bare --transform picks them from a list.")] = None,
    pick_transforms: Annotated[bool, typer.Option("--pick-transforms", hidden=True)] = False,
    skip_unsupported: SkipUnsupportedOption = False,
    force: Annotated[bool, typer.Option("--force", help="Overwrite the output without asking.")] = False,
    show_deprecated: Annotated[bool, typer.Option("--show-deprecated",
                                                  help="List deprecated formats when picking the output format.")] = False,
    yes: YesOption = False,
    quiet: QuietOption = False,
    json_: JsonOption = False,
) -> None:
    """Convert a chart. The input format is detected from the content and confirmed."""
    ui = Ui(json=json_, quiet=quiet, yes=yes)

    def body() -> None:
        path, data = read_input(ui, input)
        source = choose_input_format(ui, data, from_, confirm=True)
        import_values = parse_pairs(import_option, "-i")
        export_values = parse_pairs(export_option, "-e")
        score = load_chart(ui, source, data, import_values, skip_unsupported)
        score, applied = apply_transforms(ui, score, transform or [], pick_transforms)

        target = require_format(to, "export") if to is not None else ui.pick(
            "Write it as", exportable(show_deprecated), "The output format (--to)")
        destination = output
        if destination is None:
            destination = Path(ui.ask("File to write", "The output file", str(default_output(path, target))))
        if destination.exists() and not force:
            if destination.is_dir():
                fail(f"{destination} is a folder", Code.USAGE)
            if not ui.interactive:
                fail(f"{destination} exists; pass --force to overwrite it", Code.USAGE)
            if not ui.confirm(f"{destination} exists. Overwrite it?", False):
                fail("not overwritten", Code.ABORTED)
        options = build_options(ui, target, "export", export_values)

        used = formats.features_used(score)
        used_ids = {f.id for f in used}
        merged = not any(f.id == "sus" for f in (source, target))
        notes: list[tuple[Feature, str]] = []
        for feature in displayed_features(used, (source, target)):
            status = export_status(target, feature, support_for(target, feature, used_ids, merged), options)
            if status is not None:
                notes.append((feature, status))
        if notes:
            table = Table(title=f"Not fully kept by {target.id}", box=box.ROUNDED, header_style="bold yellow",
                          title_justify="left")
            table.add_column("Feature")
            table.add_column("On export")
            for feature, status in notes:
                table.add_row(escape(feature.name), status)
            ui.print(table)

        try:
            written = target.export(destination, score, options)
        except OSError as e:
            fail(f"couldn't write {destination}: {e.strerror or e}", Code.EXPORT_FAILED)
        except (ValueError, RuntimeError, TypeError) as e:
            fail(f"couldn't export as {target.id}: {e}", Code.EXPORT_FAILED)

        summary = f"{path.name} ({source.id}) -> {destination} ({target.id}): {written.combo_count} combo"
        if notes:
            summary += f", {len(notes)} feature{'s' if len(notes) != 1 else ''} not fully kept"
        ui.print(f"[green]done[/] {escape(summary)}")
        ui.emit({
            "input": str(path), "from": source.id, "output": str(destination), "to": target.id,
            "combo_count": written.combo_count, "transforms": applied,
            "features": [{"id": f.id, "name": f.name, "on_export": status} for f, status in notes],
        })

    handle(ui, body)


@app.command("detect")
def detect_command(
    input: Annotated[Optional[Path], typer.Argument(help="The chart to look at.", show_default=False)] = None,
    quiet: QuietOption = False,
    json_: JsonOption = False,
) -> None:
    """List the formats a chart could be, most likely first (from its content, never its name)."""
    ui = Ui(json=json_, quiet=quiet)

    def body() -> None:
        path, data = read_input(ui, input)
        found = [formats.get(id) for id in detect(data)]
        ui.emit({"input": str(path), "formats": [
            {"id": f.id, "description": f.description, "deprecated": f.deprecated} for f in found]})
        if not found:
            fail(f"{path.name} isn't a format rg-converters knows", Code.NOT_DETECTED)
        ui.print(format_table(f"{path.name} could be", found, numbered=True))

    handle(ui, body)


@app.command()
def info(
    input: Annotated[Optional[Path], typer.Argument(help="The chart to describe.", show_default=False)] = None,
    from_: FromOption = None,
    import_option: ImportOption = None,
    skip_unsupported: SkipUnsupportedOption = False,
    quiet: QuietOption = False,
    json_: JsonOption = False,
) -> None:
    """Describe a chart, and which formats can't hold the features it uses."""
    ui = Ui(json=json_, quiet=quiet)

    def body() -> None:
        path, data = read_input(ui, input)
        source = choose_input_format(ui, data, from_, confirm=False)
        score = load_chart(ui, source, data, parse_pairs(import_option, "-i"), skip_unsupported)

        bpms = sorted((n for n in score.notes if isinstance(n, Bpm)), key=lambda n: n.beat)
        skills = sum(isinstance(n, (Skill, HolodoriSkill)) for n in score.notes)
        fever = any(isinstance(n, (FeverChance, FeverStart, HolodoriFeverStart)) for n in score.notes)
        overlaps = transforms.export_overlaps_score(score)[1]
        used = formats.features_used(score)
        used_ids = {f.id for f in used}
        merged = source.id != "sus"
        targets = [f for f in formats.all() if "export" in f.operations]
        rows: list[tuple[Feature, list[str]]] = []
        for feature in displayed_features(used, (source,)):
            missing = [t.id for t in targets if support_for(t, feature, used_ids, merged) == "no"]
            rows.append((feature, missing))

        ui.print(f"[bold]{escape(path.name)}[/] ({format_label(source)})")
        details = Table(box=box.ROUNDED, show_header=False)
        details.add_column(style="bold cyan")
        details.add_column()
        details.add_row("Wave offset", f"{score.metadata.waveoffset + 0.0:g}")
        details.add_row("Combo", str(score.combo_count))
        details.add_row("Skills", str(skills))
        details.add_row("Fever", "yes" if fever else "no")
        details.add_row("Duration", f"{score.duration:.2f} s")
        details.add_row("BPM", "-".join(f"{b.bpm:g}" for b in bpms) or "none")
        details.add_row("Overlaps", str(overlaps))
        ui.print(details)
        if rows:
            table = Table(title="Features", box=box.ROUNDED, header_style="bold cyan", title_justify="left")
            table.add_column("Feature")
            table.add_column("Not supported by")
            for feature, missing in rows:
                table.add_row(escape(feature.name), ", ".join(missing) or "[dim]-[/]")
            ui.print(table)
        else:
            ui.print("[dim]Uses no features beyond what every format holds.[/]")
        ui.emit({
            "input": str(path), "format": source.id, "wave_offset": score.metadata.waveoffset + 0.0,
            "combo_count": score.combo_count, "skill_count": skills, "has_fever": fever,
            "duration": score.duration, "bpm_changes": [b.bpm for b in bpms], "overlap_count": overlaps,
            "features": [{"id": f.id, "name": f.name, "not_supported_by": missing} for f, missing in rows],
        })

    handle(ui, body)


def expand_bare_transform(argv: list[str]) -> list[str]:
    """A --transform with no name after it (end of line, or another option next) asks for a pick."""
    out: list[str] = []
    for i, token in enumerate(argv):
        if token == "--transform" and (i + 1 == len(argv) or argv[i + 1].startswith("-")):
            out.append("--pick-transforms")
        else:
            out.append(token)
    return out


def run(argv: list[str]) -> int:
    try:
        app(args=expand_bare_transform(argv), prog_name="rg-converters")
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else int(e.code is not None)
    return 0
