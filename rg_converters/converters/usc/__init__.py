import io
from pathlib import Path
from ..._rg_converters import usc as _usc

detect = _usc.detect


def load(fp):
    return _usc.load_text(fp.read())


def export(path, score, minified: bool = True):
    text = _usc.dumps(score, minified)
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
