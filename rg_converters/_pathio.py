import io
import os
from pathlib import Path


def write_text(path, text: str):
    if isinstance(path, (str, Path)):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8") as f:
            f.write(text)
    elif isinstance(path, (io.StringIO, io.TextIOBase)):
        path.write(text)
        path.seek(0)
    elif isinstance(path, io.BytesIO):
        path.write(text.encode("utf-8"))
        path.seek(0)
    else:
        raise TypeError(f"Unsupported path type: {type(path)}")


def write_bytes(path, data: bytes):
    if isinstance(path, (str, Path)):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("wb") as f:
            f.write(data)
    elif isinstance(path, io.BytesIO) or (hasattr(path, "write") and callable(path.write)):
        path.write(data)
        try:
            path.seek(0)
        except Exception:
            pass
    else:
        raise TypeError(f"Unsupported path type: {type(path)}")
