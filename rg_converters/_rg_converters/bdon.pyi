from __future__ import annotations
import rg_converters._rg_converters
__all__: list[str] = ['BdonFormatError', 'loads']
class BdonFormatError(ValueError):
    pass
def loads(data: str, guide_traces: bool = True, trace_ends: bool = True, flick_body_collide: bool = True) -> rg_converters._rg_converters.Score:
    ...
