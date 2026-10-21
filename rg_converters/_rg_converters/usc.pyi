from __future__ import annotations
import rg_converters._rg_converters
import typing
__all__: list[str] = ['detect', 'dumps', 'load_text']
def detect(data: str) -> str | None:
    ...
def dumps(score: rg_converters._rg_converters.Score, minified: bool = True) -> str:
    ...
def load_text(json_text: str) -> rg_converters._rg_converters.Score:
    ...
