import io
import os
from pathlib import Path
from typing import BinaryIO, TextIO, Union

from ...._rg_converters import leveldata as _lvd
from ....core.score import Score

__all__ = ["load", "export", "UnsupportedFeatureError"]

UnsupportedFeatureError = _lvd.next_sekai.UnsupportedFeatureError


def load(
    fp: Union[str, bytes, os.PathLike, BinaryIO, TextIO],
    error_on_unsupported_feature: bool = True,
    attempt_attached_note_convert: bool = True,
) -> Score:
    """Load Next SEKAI / Next RUSH LevelData (json or gzipped json): a path, the data, or a file.

    Anything the exporter does not write raises UnsupportedFeatureError (a ValueError) listing
    all of it; with error_on_unsupported_feature=False it is skipped or mapped to the closest
    thing a Score holds instead.

    With attempt_attached_note_convert, a judged note riding a slide or guide becomes a note of
    its own and the line stops a 480th of a beat either side of it (no round trip); without it,
    such a note is an unsupported feature.
    """
    if isinstance(fp, os.PathLike) or (isinstance(fp, str) and not fp.lstrip().startswith("{")):
        with open(fp, "rb") as f:
            data = f.read()
    elif isinstance(fp, (str, bytes, bytearray)):
        data = bytes(fp) if isinstance(fp, bytearray) else fp
    else:
        data = fp.read()
    return _lvd.next_sekai.load_text(data, error_on_unsupported_feature, attempt_attached_note_convert)


def export(path, score, as_compressed: bool = True, smooth_guide_fade: bool = False,
           use_guide_layer: bool = False):
    data = _lvd.next_sekai.export_bytes(score, as_compressed, smooth_guide_fade, use_guide_layer)
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
