"""The MikuMikuWorld chart editor's formats and those of its forks."""

from __future__ import annotations

from dataclasses import dataclass

from ._format import ExportOptions, Format, NoOptions

__all__ = ["MmwsExportOptions", "ccmmws", "mmws", "unchmmws"]


@dataclass(frozen=True, kw_only=True)
class MmwsExportOptions(ExportOptions):
    strip_layers: bool = False


mmws: Format[NoOptions, MmwsExportOptions, bytes] = Format(
    "mmws", NoOptions, MmwsExportOptions, bytes, "mmw"
)
ccmmws: Format[NoOptions, NoOptions, bytes] = Format(
    "ccmmws", NoOptions, NoOptions, bytes, "mmw"
)
unchmmws: Format[NoOptions, NoOptions, bytes] = Format(
    "unchmmws", NoOptions, NoOptions, bytes, "mmw"
)
