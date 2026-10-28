import json
import subprocess
import sys
from pathlib import Path

import pytest

pytest.importorskip("typer")

import rg_converters  # noqa: E402
from rg_converters import __version__  # noqa: E402
from rg_converters import format as F  # noqa: E402
from rg_converters.core import Bpm, MetaData, Score, Single, TimeScaleGroup, TimeScalePoint  # noqa: E402

SUS_CHART = Path(__file__).resolve().parent.parent / "test_files" / "88s_append.sus"
# Where the package being tested lives (the source tree or an installed wheel), so -m finds the same one.
PACKAGE_PARENT = Path(rg_converters.__file__).resolve().parent.parent


def cli(*args: str, stdin: str = "") -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, "-m", "rg_converters.cli", *args], input=stdin, capture_output=True,
                          text=True, encoding="utf-8", cwd=PACKAGE_PARENT)


def test_detect() -> None:
    result = cli("detect", str(SUS_CHART), "--json")
    assert result.returncode == 0
    assert [f["id"] for f in json.loads(result.stdout)["formats"]] == ["sus"]


def test_detect_nothing(tmp_path: Path) -> None:
    (tmp_path / "junk.bin").write_bytes(b"junk")
    assert cli("detect", str(tmp_path / "junk.bin")).returncode == 3
    assert cli("convert", str(tmp_path / "junk.bin"), str(tmp_path / "x.sus"), "--to", "sus", "-y").returncode == 3


def test_convert(tmp_path: Path) -> None:
    out = tmp_path / "out.usc"
    result = cli("convert", str(SUS_CHART), str(out), "--to", "usc", "-y", "--json")
    assert result.returncode == 0, result.stderr
    report = json.loads(result.stdout)
    assert (report["from"], report["to"], report["combo_count"]) == ("sus", "usc", F.usc.load(out).combo_count)
    assert cli("convert", str(SUS_CHART), str(out), "--to", "usc", "-y").returncode == 2
    assert cli("convert", str(SUS_CHART), str(out), "--to", "usc", "-y", "--force").returncode == 0


def test_convert_asks_for_what_is_missing(tmp_path: Path) -> None:
    out = tmp_path / "out.ccmmws"
    pick = [f.id for f in F.all() if "export" in f.operations and not f.deprecated].index("ccmmws") + 1
    result = cli("convert", str(SUS_CHART), str(out), stdin=f"y\n{pick}\n")
    assert result.returncode == 0, result.stderr
    assert F.mmw.ccmmws.load(out).combo_count == F.sus.load(SUS_CHART).combo_count


def test_required_export_option(tmp_path: Path) -> None:
    out = tmp_path / "out.pjsk"
    assert cli("convert", str(SUS_CHART), str(out), "--to", "pjsk", "-y").returncode == 2
    assert cli("convert", str(SUS_CHART), str(out), "--to", "pjsk", "-y", "-e", "music_id=3").returncode == 0
    assert cli("convert", str(SUS_CHART), str(out), "--to", "pjsk", "--force", stdin="y\n4\n").returncode == 0


def test_bad_options(tmp_path: Path) -> None:
    out = str(tmp_path / "out.sus")
    assert cli("convert", str(SUS_CHART), out, "--to", "sus", "-y", "-e", "nope=1").returncode == 2
    assert cli("convert", str(SUS_CHART), out, "--to", "sus", "-y", "-e", "allow_layers=maybe").returncode == 2
    assert cli("convert", str(SUS_CHART), out, "--to", "holodori_sus", "-y").returncode == 3
    assert cli("convert", str(SUS_CHART), out, "--to", "sus", "-y", "--transform", "nope").returncode == 2
    assert cli("convert", str(SUS_CHART), out, "--to", "sus", "-y", "--transform").returncode == 2


def test_export_errors_and_strip_layers(tmp_path: Path) -> None:
    layered = Score(MetaData("t", "a", "d", 0, None), [
        Bpm(0, 120), TimeScaleGroup([TimeScalePoint(0, 1)]), TimeScaleGroup([TimeScalePoint(0, 2)]),
        Single(beat=1, lane=0, size=1.5, critical=False, timeScaleGroup=1)])
    source = tmp_path / "layered.usc"
    F.usc.export(source, layered)
    out = str(tmp_path / "out.pjsk")
    failed = cli("convert", str(source), out, "--to", "pjsk", "-y", "-e", "music_id=1")
    assert failed.returncode == 5
    assert "strip_layers" in failed.stderr
    assert cli("convert", str(source), out, "--to", "pjsk", "-y", "-e", "music_id=1", "-e",
               "strip_layers=true").returncode == 0
    assert cli("convert", str(source), out, "--to", "pjsk", "-y", "-e", "music_id=1", "--force", "--transform",
               "strip_layers").returncode == 0


def test_unsupported_features(tmp_path: Path) -> None:
    level = tmp_path / "level.json"
    level.write_text(json.dumps({"bgmOffset": 0, "entities": [{"archetype": "SomethingNew", "data": []}]}))
    out = str(tmp_path / "out.usc")
    args = ("convert", str(level), out, "--from", "next_sekai", "--to", "usc")
    assert cli(*args, stdin="n\n").returncode == 4
    assert cli(*args, "-y").returncode == 4
    assert cli(*args, "-y", "--skip-unsupported").returncode == 0
    assert cli(*args, "--force", stdin="y\n").returncode == 0


def test_info() -> None:
    result = cli("info", str(SUS_CHART), "--json")
    assert result.returncode == 0, result.stderr
    report = json.loads(result.stdout)
    assert report["format"] == "sus"
    assert report["combo_count"] == F.sus.load(SUS_CHART).combo_count
    assert report["has_fever"] is True
    feature = next(f for f in report["features"] if f["id"] == "skill_fever")
    assert "pjsk" in feature["not_supported_by"] and "sus" not in feature["not_supported_by"]


def test_version() -> None:
    result = cli("--version")
    assert result.returncode == 0
    assert result.stdout.strip() == f"rg-converters {__version__}"
