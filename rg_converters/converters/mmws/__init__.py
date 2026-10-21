import io
import os
from pathlib import Path
from ..._rg_converters import mmws as _mmws
from ..._pathio import write_bytes


def detect(data):
    if isinstance(data, str):
        data = data.encode("utf-8", "surrogateescape")
    return _mmws.detect(bytes(data))


def load(fp):
    return _mmws.load_bytes(fp.buffer.read() if hasattr(fp, "buffer") else fp.read())


def export(path, score, *, format=None):
    if format is None and isinstance(path, (str, Path)):
        _, format = os.path.splitext(Path(path).name)
    if format is None:
        format = ""
    data = _mmws.export_bytes(score, format)
    write_bytes(path, data)
