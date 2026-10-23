import io
from pathlib import Path
from ...._rg_converters import leveldata as _lvd
from .loader import load  # legacy Python loader (un-ported)

__all__ = ["load", "export"]


def export(path, score, as_compressed: bool = True):
    data = _lvd.chart_cyanvas.export_bytes(score, as_compressed)
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
