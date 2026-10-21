from __future__ import annotations
import rg_converters._rg_converters
import typing
__all__: list[str] = ['detect', 'dumps', 'loads']
def detect(data: str) -> str | None:
    ...
def dumps(score: rg_converters._rg_converters.Score, allow_layers: bool = False, allow_extended_lanes: bool = False, delete_damage: bool = True, keep_note_speed_ratios: bool = False, measure_extensions: bool = False, skip_shift: bool = False) -> str:
    ...
def loads(data: str) -> rg_converters._rg_converters.Score:
    ...
