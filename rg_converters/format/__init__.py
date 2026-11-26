"""Every chart format rg-converters can load or export, as one object per format.

from rg_converters.format import pjsk, sus
from rg_converters.format.mmw import ccmmws

score = sus.load("chart.sus")
pjsk.export("chart.pjsk", score, pjsk.ExportOptions(music_id=1))
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .._rg_converters import (
    BdonFormatError,
    DecompressedSizeError,
    UnsupportedFeatureError,
)
from .._rg_converters import formats as _formats
from ..core.score import Score
from . import mmw, sonolus_leveldata
from ._format import (
    DEFAULT_MAX_DECOMPRESSED_SIZE,
    FEATURES,
    _FEATURES_BY_ID,
    Destination,
    ExportOptions,
    Feature,
    Format,
    ImportOptions,
    NoOptions,
    OptionInfo,
    Source,
    Support,
)

__all__ = [
    "BdonFormatError",
    "BdonImportOptions",
    "DecompressedSizeError",
    "Destination",
    "ExportOptions",
    "FEATURES",
    "Feature",
    "Format",
    "ImportOptions",
    "NoOptions",
    "OptionInfo",
    "PjskExportOptions",
    "PjskImportOptions",
    "Source",
    "Support",
    "SusExportOptions",
    "SusImportOptions",
    "UnsupportedFeatureError",
    "UscExportOptions",
    "YumeSusExportOptions",
    "all",
    "bdon",
    "features_used",
    "get",
    "holodori_sus",
    "mmw",
    "pjsk",
    "sonolus_leveldata",
    "sus",
    "usc",
    "yume_sus",
]


@dataclass(frozen=True, kw_only=True)
class SusImportOptions(ImportOptions):
    skip_note_speed_ratios: bool = False


@dataclass(frozen=True, kw_only=True)
class SusExportOptions(ExportOptions):
    allow_layers: bool = False
    allow_damage: bool = False
    allow_note_speed_ratios: bool = False
    allow_measure_extensions: bool = False
    skip_shift: bool = False


@dataclass(frozen=True, kw_only=True)
class YumeSusExportOptions(ExportOptions):
    skip_shift: bool = False
    strip_layers: bool = False


@dataclass(frozen=True, kw_only=True)
class UscExportOptions(ExportOptions):
    minified: bool = True


@dataclass(frozen=True, kw_only=True)
class PjskImportOptions(ImportOptions):
    max_decompressed_size: int = DEFAULT_MAX_DECOMPRESSED_SIZE


@dataclass(frozen=True, kw_only=True)
class PjskExportOptions(ExportOptions):
    music_id: int
    compressed: bool = True
    strip_layers: bool = False
    max_decompressed_size: int = DEFAULT_MAX_DECOMPRESSED_SIZE


@dataclass(frozen=True, kw_only=True)
class BdonImportOptions(ImportOptions):
    guide_traces: bool = True
    trace_ends: bool = True
    flick_body_collide: bool = True
    max_decompressed_size: int = DEFAULT_MAX_DECOMPRESSED_SIZE


sus: Format[SusImportOptions, SusExportOptions, str] = Format(
    "sus", SusImportOptions, SusExportOptions, str
)
yume_sus: Format[NoOptions, YumeSusExportOptions, str] = Format(
    "yume_sus", NoOptions, YumeSusExportOptions, str
)
holodori_sus: Format[NoOptions, NoOptions, str] = Format(
    "holodori_sus", NoOptions, NoOptions, str
)
usc: Format[NoOptions, UscExportOptions, str] = Format(
    "usc", NoOptions, UscExportOptions, str
)
pjsk: Format[PjskImportOptions, PjskExportOptions, bytes] = Format(
    "pjsk", PjskImportOptions, PjskExportOptions, bytes
)
bdon: Format[BdonImportOptions, NoOptions, bytes] = Format(
    "bdon", BdonImportOptions, NoOptions, bytes
)

_BY_ID: dict[str, Format[Any, Any, Any]] = {
    f.id: f
    for f in (
        sus,
        yume_sus,
        holodori_sus,
        usc,
        mmw.mmws,
        mmw.ccmmws,
        mmw.unchmmws,
        pjsk,
        bdon,
        sonolus_leveldata.next_sekai,
        sonolus_leveldata.chart_cyanvas,
        sonolus_leveldata.untitled_sekai,
    )
}
_ALL: tuple[Format[Any, Any, Any], ...] = tuple(
    _BY_ID[info["id"]] for info in _formats.all()
)


def all() -> tuple[Format[Any, Any, Any], ...]:
    """Every format, grouped ones included, in the registry's order."""
    return _ALL


def get(id: str) -> Format[Any, Any, Any]:
    """The format with this id (the ids detect() returns); ValueError when there is none."""
    try:
        return _BY_ID[id]
    except KeyError:
        raise ValueError(
            f"Unknown format {id!r} (known: {', '.join(_BY_ID)})"
        ) from None


def features_used(score: Score) -> list[Feature]:
    """The features a score uses, in display order."""
    return [_FEATURES_BY_ID[id] for id in _formats.features_used(score)]
