"""The MikuMikuWorld chart editor's formats and those of its forks."""
from __future__ import annotations

from dataclasses import dataclass

from ._format import ExportOptions, Format, NoOptions

__all__ = ["MmwsExportOptions", "ccmmws", "mmws", "unchmmws"]


@dataclass(frozen=True, kw_only=True)
class MmwsExportOptions(ExportOptions):
    strip_layers: bool = False


mmws = Format("mmws", NoOptions, MmwsExportOptions, bytes, "mmw")
ccmmws = Format("ccmmws", NoOptions, NoOptions, bytes, "mmw")
unchmmws = Format("unchmmws", NoOptions, NoOptions, bytes, "mmw")
