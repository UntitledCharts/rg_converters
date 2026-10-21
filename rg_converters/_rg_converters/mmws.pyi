from __future__ import annotations
import rg_converters._rg_converters
import typing
__all__: list[str] = ['detect', 'export_bytes', 'load_bytes']
def detect(data: bytes) -> str | None:
    ...
def export_bytes(score: rg_converters._rg_converters.Score, format: str) -> bytes:
    ...
def load_bytes(data: bytes) -> rg_converters._rg_converters.Score:
    ...
