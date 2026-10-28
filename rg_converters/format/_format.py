from __future__ import annotations

import dataclasses
import io
import os
from dataclasses import dataclass
from pathlib import Path
from typing import IO, Generic, Literal, TypeVar, Union, cast

from .._rg_converters import formats as _formats
from ..core.score import Score

Source = Union[str, "os.PathLike[str]", bytes, bytearray, memoryview, IO[bytes], IO[str]]
Destination = Union[str, "os.PathLike[str]", IO[bytes], IO[str]]
Support = Literal["yes", "option", "lossy", "no"]
Operation = Literal["load", "export"]
OptionValue = Union[bool, int, str]


@dataclass(frozen=True)
class Feature:
    id: str
    name: str


FEATURES: tuple[Feature, ...] = tuple(Feature(id, name) for id, name in _formats.features())
_FEATURES_BY_ID = {f.id: f for f in FEATURES}


@dataclass(frozen=True)
class OptionInfo:
    name: str
    type: Literal["bool", "int", "str"]
    required: bool
    default: OptionValue | None


@dataclass(frozen=True, kw_only=True)
class _Options:
    def _values(self) -> dict[str, OptionValue]:
        return {f.name: getattr(self, f.name) for f in dataclasses.fields(self)}


@dataclass(frozen=True, kw_only=True)
class ImportOptions(_Options):
    """Base of every format's ImportOptions."""


@dataclass(frozen=True, kw_only=True)
class ExportOptions(_Options):
    """Base of every format's ExportOptions."""


@dataclass(frozen=True, kw_only=True)
class NoOptions(ImportOptions, ExportOptions):
    """The options of a format that has none."""


ImportT = TypeVar("ImportT", bound=ImportOptions)
ExportT = TypeVar("ExportT", bound=ExportOptions)
DataT = TypeVar("DataT", str, bytes)


def _read(source: Source, binary: bool) -> bytes:
    if isinstance(source, (str, os.PathLike)):
        return Path(source).read_bytes()
    if isinstance(source, (bytes, bytearray, memoryview)):
        return bytes(source)
    data = (getattr(source, "buffer", source) if binary else source).read()
    return data.encode("utf-8") if isinstance(data, str) else data


def _write(destination: Destination, data: str | bytes) -> None:
    if isinstance(destination, (str, os.PathLike)):
        path = Path(destination)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode("utf-8") if isinstance(data, str) else data)
        return
    if isinstance(destination, io.TextIOBase):
        if isinstance(data, bytes):
            buffer = getattr(destination, "buffer", None)
            if buffer is None:
                raise TypeError("this format writes bytes; pass a binary file, not a text one")
            destination.flush()
            buffer.write(data)
        else:
            cast(io.TextIOBase, destination).write(data)
    else:
        destination.write(data.encode("utf-8") if isinstance(data, str) else data)  # type: ignore[arg-type]
    if isinstance(destination, (io.StringIO, io.BytesIO)):
        destination.seek(0)


class Format(Generic[ImportT, ExportT, DataT]):
    """A chart format. `operations` says whether it can be loaded, exported or both; the others
    raise ValueError."""

    id: str
    description: str
    deprecated: bool
    # The file is bytes (gzip, base64, binary); otherwise it is text.
    binary: bool
    operations: tuple[Operation, ...]
    file_types: tuple[str, ...]
    # Feature id -> how this format holds it (what exporting does with it).
    features: dict[str, Support]
    import_options: tuple[OptionInfo, ...]
    export_options: tuple[OptionInfo, ...]
    ImportOptions: type[ImportT]
    ExportOptions: type[ExportT]

    def __init__(self, id: str, import_options: type[ImportT], export_options: type[ExportT],
                 data: type[DataT], group: str = "") -> None:
        info = next(i for i in _formats.all() if i["id"] == id)
        self._name = f"{group}.{id}" if group else id
        if (data is bytes) != info["binary"]:
            raise TypeError(f"{id} data is {'bytes' if info['binary'] else 'str'}")
        self.id = id
        self.description = info["description"]
        self.deprecated = info["deprecated"]
        self.binary = info["binary"]
        operations: list[Operation] = []
        if info["can_load"]:
            operations.append("load")
        if info["can_export"]:
            operations.append("export")
        self.operations = tuple(operations)
        self.file_types = tuple(info["file_types"])
        self.features = dict(info["features"])
        self.import_options = tuple(OptionInfo(**o) for o in info["import_options"])
        self.export_options = tuple(OptionInfo(**o) for o in info["export_options"])
        self.ImportOptions = import_options
        self.ExportOptions = export_options

    def __repr__(self) -> str:
        return f"<format {self.id}>"

    def supports(self, feature: Feature | str) -> Support:
        return self.features[feature.id if isinstance(feature, Feature) else feature]

    def _options(self, options: _Options | None, kind: type[_Options], infos: tuple[OptionInfo, ...],
                 what: str) -> dict[str, OptionValue]:
        name = "ImportOptions" if what == "load" else "ExportOptions"
        if options is None:
            required = [o.name for o in infos if o.required]
            if required:
                given = ", ".join(f"{option}=..." for option in required)
                raise TypeError(f"{self._name}.{what} needs {self._name}.{name}({given})")
            return {}
        if not isinstance(options, kind):
            raise TypeError(f"{self._name}.{what} takes {self._name}.{name}, not {type(options).__name__}")
        return options._values()

    def loads(self, data: str | bytes, options: ImportT | None = None) -> Score:
        """Load a chart from its contents."""
        raw = data.encode("utf-8") if isinstance(data, str) else bytes(data)
        return _formats.load(self.id, raw, self._options(options, self.ImportOptions, self.import_options, "load"))

    def load(self, source: Source, options: ImportT | None = None) -> Score:
        """Load a chart from a path, its contents as bytes, or a file."""
        return self.loads(_read(source, self.binary), options)

    def dumps(self, score: Score, options: ExportT | None = None) -> tuple[DataT, Score]:
        """The file's contents, and the score as written (a copy; `score` is never modified)."""
        values = self._options(options, self.ExportOptions, self.export_options, "export")
        return _formats.dumps(self.id, score, values)  # type: ignore[return-value]

    def export(self, destination: Destination, score: Score, options: ExportT | None = None) -> Score:
        """Write the chart to a path or file; returns the score as written."""
        data, written = self.dumps(score, options)
        _write(destination, data)
        return written
