"""Sonolus LevelData, one format per engine."""
from __future__ import annotations

from dataclasses import dataclass

from ._format import ExportOptions, Format, ImportOptions, NoOptions

__all__ = [
    "ChartCyanvasExportOptions",
    "ChartCyanvasImportOptions",
    "NextSekaiExportOptions",
    "NextSekaiImportOptions",
    "UntitledSekaiExportOptions",
    "chart_cyanvas",
    "next_sekai",
    "untitled_sekai",
]


@dataclass(frozen=True, kw_only=True)
class NextSekaiImportOptions(ImportOptions):
    error_on_unsupported_feature: bool = True
    attempt_attached_note_convert: bool = True


@dataclass(frozen=True, kw_only=True)
class NextSekaiExportOptions(ExportOptions):
    compressed: bool = True
    smooth_guide_fade: bool = False
    use_guide_layer: bool = False


@dataclass(frozen=True, kw_only=True)
class ChartCyanvasImportOptions(ImportOptions):
    error_on_unsupported_feature: bool = True


@dataclass(frozen=True, kw_only=True)
class ChartCyanvasExportOptions(ExportOptions):
    compressed: bool = True


@dataclass(frozen=True, kw_only=True)
class UntitledSekaiExportOptions(ExportOptions):
    compressed: bool = True


next_sekai = Format("next_sekai", NextSekaiImportOptions, NextSekaiExportOptions, bytes, "sonolus_leveldata")
chart_cyanvas = Format("chart_cyanvas", ChartCyanvasImportOptions, ChartCyanvasExportOptions, bytes,
                       "sonolus_leveldata")
untitled_sekai = Format("untitled_sekai", NoOptions, UntitledSekaiExportOptions, bytes, "sonolus_leveldata")
