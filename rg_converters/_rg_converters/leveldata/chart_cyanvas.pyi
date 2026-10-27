from __future__ import annotations
import rg_converters._rg_converters
import typing
__all__: list[str] = ['UnsupportedFeatureError', 'export_bytes', 'load_text']
class UnsupportedFeatureError(ValueError):
    pass
def export_bytes(score: rg_converters._rg_converters.Score, as_compressed: bool = True) -> bytes:
    ...
def load_text(data: typing.Any, error_on_unsupported_feature: bool = True) -> rg_converters._rg_converters.Score:
    ...
