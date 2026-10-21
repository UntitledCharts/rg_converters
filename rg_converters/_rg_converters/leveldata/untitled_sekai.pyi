from __future__ import annotations
import rg_converters._rg_converters
__all__: list[str] = ['export_bytes', 'load_text']
def export_bytes(score: rg_converters._rg_converters.Score, as_compressed: bool = True) -> bytes:
    ...
def load_text(data: str) -> rg_converters._rg_converters.Score:
    ...
