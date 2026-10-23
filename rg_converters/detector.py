import os
from ._rg_converters import detect as _detect

__all__ = ["detect"]


def detect(data):
    if isinstance(data, (os.PathLike, str)):
        with open(data, "rb") as f:
            data = f.read()
    elif isinstance(data, memoryview):
        data = data.tobytes()
    elif hasattr(data, "read"):
        data = data.read()
    r = _detect(bytes(data))
    return r
