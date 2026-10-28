import os
from typing import IO, Union

from ._rg_converters import detect as _detect

__all__ = ["detect"]


def detect(data: Union[str, "os.PathLike[str]", bytes, bytearray, memoryview, IO[bytes]]) -> list[str]:
    if isinstance(data, (os.PathLike, str)):
        with open(data, "rb") as f:
            data = f.read()
    elif isinstance(data, memoryview):
        data = data.tobytes()
    elif not isinstance(data, (bytes, bytearray)):
        data = data.read()
    return _detect(bytes(data))
