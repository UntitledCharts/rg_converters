import os
from typing import IO, Union

from ._rg_converters import detect as _detect
from .format._format import DEFAULT_MAX_DECOMPRESSED_SIZE

__all__ = ["detect"]


def detect(
    data: Union[str, "os.PathLike[str]", bytes, bytearray, memoryview, IO[bytes]],
    *,
    max_decompressed_size: int = DEFAULT_MAX_DECOMPRESSED_SIZE,
) -> list[str]:
    if isinstance(data, (os.PathLike, str)):
        with open(data, "rb") as f:
            data = f.read()
    elif isinstance(data, memoryview):
        data = data.tobytes()
    elif not isinstance(data, (bytes, bytearray)):
        data = data.read()
    return _detect(bytes(data), max_decompressed_size)
