import os
from pathlib import Path
from typing import BinaryIO, TextIO, Union

from ...._rg_converters import leveldata as _lvd
from ....core.score import Score

__all__ = ["load", "export", "UnsupportedFeatureError"]

UnsupportedFeatureError = _lvd.chart_cyanvas.UnsupportedFeatureError


def load(
    fp: Union[str, bytes, os.PathLike, BinaryIO, TextIO],
    error_on_unsupported_feature: bool = True,
) -> Score:
    """Load Chart Cyanvas LevelData (json or gzipped json): a path, the data, or a file.

    Anything the exporter (or the engine's own converter) does not write raises
    UnsupportedFeatureError (a ValueError) listing all of it; with
    error_on_unsupported_feature=False it is skipped or mapped to the closest thing a Score
    holds instead.
    """
    if isinstance(fp, os.PathLike) or (isinstance(fp, str) and not fp.lstrip().startswith("{")):
        with open(fp, "rb") as f:
            data = f.read()
    elif isinstance(fp, (str, bytes, bytearray)):
        data = bytes(fp) if isinstance(fp, bytearray) else fp
    else:
        data = fp.read()
    return _lvd.chart_cyanvas.load_text(data, error_on_unsupported_feature)


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
