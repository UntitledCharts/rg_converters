import os
from typing import IO

from ..._rg_converters import bdon as _b

BdonFormatError = _b.BdonFormatError


def _read_source(source: "os.PathLike | IO[bytes] | bytes | str") -> "bytes | str":
    if isinstance(source, (os.PathLike, str)):
        with open(source, "rb") as file:
            return file.read()
    if isinstance(source, (bytes, bytearray, memoryview)):
        return bytes(source)
    return source.read()


def loads(
    data: "bytes | str",
    *,
    guide_traces: bool = True,
    trace_ends: bool = True,
    flick_body_collide: bool = True,
):
    return _b.loads(
        data,
        guide_traces=guide_traces,
        trace_ends=trace_ends,
        flick_body_collide=flick_body_collide,
    )


def load(
    source: "os.PathLike | IO[bytes] | bytes | str",
    *,
    guide_traces: bool = True,
    trace_ends: bool = True,
    flick_body_collide: bool = True,
):
    return loads(
        _read_source(source),
        guide_traces=guide_traces,
        trace_ends=trace_ends,
        flick_body_collide=flick_body_collide,
    )
