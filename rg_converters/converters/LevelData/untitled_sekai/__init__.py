import io
from pathlib import Path
from ...._rg_converters import leveldata as _lvd

__all__ = ["load", "export"]


def load(fp):
    return _lvd.untitled_sekai.load_text(fp.read() if hasattr(fp, "read") else fp)


def export(path, score, as_compressed: bool = True):
    data = _lvd.untitled_sekai.export_bytes(score, as_compressed)
    if isinstance(path, (str, Path)):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("wb") as f:
            f.write(data)
    else:
        path.write(data)
        try:
            path.seek(0)
        except Exception:
            pass
