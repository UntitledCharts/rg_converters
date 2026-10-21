import base64
import gzip
import json
import os
from pathlib import Path
from ..._rg_converters import pjsk as _pjsk
from ..._pathio import write_bytes


def detect(data):
    if isinstance(data, str):
        data = data.encode("utf-8", "surrogateescape")
    return _pjsk.detect(bytes(data))


def _decode(data: bytes) -> dict:
    try:
        return json.loads(gzip.decompress(base64.b64decode(data)))
    except Exception:
        pass
    try:
        return json.loads(gzip.decompress(data))
    except Exception:
        pass
    return json.loads(data)


def load_raw(data):
    if isinstance(data, (os.PathLike, str)):
        with open(data, "rb") as f:
            raw = f.read()
    elif isinstance(data, bytes):
        raw = data
    else:
        raw = data.read()
    return _decode(raw)


def load(data):
    if isinstance(data, (os.PathLike, str)):
        with open(data, "rb") as f:
            raw = f.read()
    elif isinstance(data, bytes):
        raw = data
    else:
        raw = data.read()
    return _pjsk.load_bytes(bytes(raw))


def export(path, score, music_id):
    data = _pjsk.export_bytes(score, music_id)
    write_bytes(path, data)
