from __future__ import annotations
import typing
from . import chart_cyanvas
from . import next_sekai
from . import untitled_sekai
__all__: list[str] = ['chart_cyanvas', 'detect', 'next_sekai', 'untitled_sekai']
def detect(data: typing.Any, skip_gzip: bool = False, skip_json: bool = False) -> str | None:
    ...
