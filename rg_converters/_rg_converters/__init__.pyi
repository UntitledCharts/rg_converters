from __future__ import annotations
import collections.abc
import rg_converters.format
import typing
from . import transforms
from . import formats

__all__: list[str] = [
    "BdonFormatError",
    "Bpm",
    "DecompressedSizeError",
    "FeverChance",
    "FeverStart",
    "Guide",
    "GuidePoint",
    "HolodoriChargeEnd",
    "HolodoriChargeStart",
    "HolodoriFeverEnd",
    "HolodoriFeverStart",
    "HolodoriSkill",
    "MetaData",
    "Score",
    "Single",
    "Skill",
    "Slide",
    "SlideEndPoint",
    "SlideRelayPoint",
    "SlideStartPoint",
    "TimeScaleGroup",
    "TimeScalePoint",
    "UnsupportedFeatureError",
    "Volume",
    "detect",
    "formats",
    "transforms",
]

class _Node:
    pass

class MetaData:
    artist: str
    designer: str
    title: str
    waveoffset: int | float
    def __init__(
        self,
        title: str,
        artist: str,
        designer: str,
        waveoffset: int | float,
        requests: collections.abc.Sequence[str] | None,
    ) -> None: ...
    @property
    def requests(self) -> list[str] | None: ...
    @requests.setter
    def requests(self, arg0: collections.abc.Sequence[str] | None) -> None: ...

class Bpm(_Node):
    beat: int | float
    bpm: int | float
    type: typing.Literal["bpm"]
    def __init__(
        self, beat: int | float, bpm: int | float, type: typing.Literal["bpm"] = "bpm"
    ) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class TimeScalePoint(_Node):
    beat: int | float
    timeScale: int | float
    def __init__(self, beat: int | float, timeScale: int | float) -> None: ...

class TimeScaleGroup(_Node):
    type: typing.Literal["timeScaleGroup"]
    def __init__(
        self,
        changes: collections.abc.Sequence[TimeScalePoint] = [],
        type: typing.Literal["timeScaleGroup"] = "timeScaleGroup",
    ) -> None: ...
    def append(self, time_scale: TimeScalePoint) -> None: ...
    def get_sus_sort_number(self) -> int: ...
    def insert(
        self,
        index: typing.SupportsInt | typing.SupportsIndex,
        time_scale: TimeScalePoint,
    ) -> None: ...
    @property
    def changes(self) -> list[TimeScalePoint]: ...
    @changes.setter
    def changes(self, arg0: collections.abc.Sequence[TimeScalePoint]) -> None: ...

class Volume(_Node):
    beat: int | float
    type: typing.Literal["volume"]
    volume: int | float
    def __init__(
        self,
        beat: int | float,
        volume: int | float,
        type: typing.Literal["volume"] = "volume",
    ) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class Skill(_Node):
    beat: int | float
    type: typing.Literal["skill"]
    def __init__(self, *, beat: int | float) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class FeverChance(_Node):
    beat: int | float
    type: typing.Literal["feverChance"]
    def __init__(self, *, beat: int | float) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class FeverStart(_Node):
    beat: int | float
    type: typing.Literal["feverStart"]
    def __init__(self, *, beat: int | float) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class Single(_Node):
    beat: int | float
    critical: bool | None
    direction: str | None
    fake: bool
    lane: int | float
    size: int | float
    speedRatio: int | float
    trace: bool | None
    type: typing.Literal["single", "damage"]
    def __init__(
        self,
        *,
        beat: int | float,
        critical: bool | None = None,
        lane: int | float,
        size: int | float,
        fake: bool = False,
        timeScaleGroup: typing.SupportsInt | typing.SupportsIndex,
        speedRatio: int | float = 1.0,
        trace: bool | None = None,
        direction: str | None = None,
        type: typing.Literal["single", "damage"] = "single",
    ) -> None: ...
    def get_sus_sort_number(self) -> int: ...
    @property
    def timeScaleGroup(self) -> int: ...
    @timeScaleGroup.setter
    def timeScaleGroup(
        self, arg0: typing.SupportsInt | typing.SupportsIndex
    ) -> None: ...

class SlideStartPoint(_Node):
    beat: int | float
    critical: bool
    ease: str
    judgeType: str
    lane: int | float
    size: int | float
    speedRatio: int | float
    type: typing.Literal["start"]
    def __init__(
        self,
        beat: int | float,
        critical: bool,
        ease: str,
        judgeType: str,
        lane: int | float,
        size: int | float,
        timeScaleGroup: typing.SupportsInt | typing.SupportsIndex,
        speedRatio: int | float = 1.0,
        type: typing.Literal["start"] = "start",
    ) -> None: ...
    @property
    def timeScaleGroup(self) -> int: ...
    @timeScaleGroup.setter
    def timeScaleGroup(
        self, arg0: typing.SupportsInt | typing.SupportsIndex
    ) -> None: ...

class SlideRelayPoint(_Node):
    beat: int | float
    critical: bool | None
    ease: str
    fake: bool
    lane: int | float
    size: int | float
    speedRatio: int | float
    type: typing.Literal["tick", "attach"]
    def __init__(
        self,
        beat: int | float,
        ease: str,
        lane: int | float,
        size: int | float,
        timeScaleGroup: typing.SupportsInt | typing.SupportsIndex,
        type: typing.Literal["tick", "attach"],
        critical: bool | None = None,
        fake: bool = False,
        speedRatio: int | float = 1.0,
    ) -> None: ...
    @property
    def timeScaleGroup(self) -> int: ...
    @timeScaleGroup.setter
    def timeScaleGroup(
        self, arg0: typing.SupportsInt | typing.SupportsIndex
    ) -> None: ...

class SlideEndPoint(_Node):
    beat: int | float
    critical: bool
    direction: str | None
    judgeType: str
    lane: int | float
    size: int | float
    speedRatio: int | float
    type: typing.Literal["end"]
    def __init__(
        self,
        beat: int | float,
        critical: bool,
        judgeType: str,
        lane: int | float,
        size: int | float,
        timeScaleGroup: typing.SupportsInt | typing.SupportsIndex,
        speedRatio: int | float = 1.0,
        direction: str | None = None,
        type: typing.Literal["end"] = "end",
    ) -> None: ...
    @property
    def timeScaleGroup(self) -> int: ...
    @timeScaleGroup.setter
    def timeScaleGroup(
        self, arg0: typing.SupportsInt | typing.SupportsIndex
    ) -> None: ...

class Slide(_Node):
    critical: bool
    fake: bool
    type: typing.Literal["slide"]
    def __init__(
        self,
        critical: bool,
        fake: bool = False,
        connections: collections.abc.Sequence[
            SlideStartPoint | SlideRelayPoint | SlideEndPoint
        ] = [],
        type: typing.Literal["slide"] = "slide",
    ) -> None: ...
    def append(
        self, slidepoint: SlideStartPoint | SlideRelayPoint | SlideEndPoint
    ) -> None: ...
    def get_sus_sort_number(self) -> int: ...
    def sort(self) -> None: ...
    @property
    def connections(
        self,
    ) -> list[SlideStartPoint | SlideRelayPoint | SlideEndPoint]: ...
    @connections.setter
    def connections(
        self,
        arg0: collections.abc.Sequence[
            SlideStartPoint | SlideRelayPoint | SlideEndPoint
        ],
    ) -> None: ...

class GuidePoint(_Node):
    alpha: int | float | None
    beat: int | float
    ease: str
    lane: int | float
    size: int | float
    speedRatio: int | float
    def __init__(
        self,
        beat: int | float,
        ease: str | None,
        lane: int | float,
        size: int | float,
        timeScaleGroup: typing.SupportsInt | typing.SupportsIndex,
        speedRatio: int | float = 1.0,
        alpha: int | float | None = None,
    ) -> None: ...
    @property
    def timeScaleGroup(self) -> int: ...
    @timeScaleGroup.setter
    def timeScaleGroup(
        self, arg0: typing.SupportsInt | typing.SupportsIndex
    ) -> None: ...

class Guide(_Node):
    color: str
    color_code: str | None
    fade: str
    type: typing.Literal["guide"]
    def __init__(
        self,
        color: str,
        fade: str,
        midpoints: collections.abc.Sequence[GuidePoint] = [],
        type: typing.Literal["guide"] = "guide",
        color_code: str | None = None,
    ) -> None: ...
    def append(self, guidepoint: GuidePoint) -> None: ...
    def get_sus_sort_number(self) -> int: ...
    def sort(self) -> None: ...
    @property
    def midpoints(self) -> list[GuidePoint]: ...
    @midpoints.setter
    def midpoints(self, arg0: collections.abc.Sequence[GuidePoint]) -> None: ...

class HolodoriSkill(_Node):
    beat: int | float
    type: typing.Literal["holodoriSkill"]
    def __init__(
        self, *, beat: int | float, slot: typing.SupportsInt | typing.SupportsIndex
    ) -> None: ...
    def get_sus_sort_number(self) -> int: ...
    @property
    def slot(self) -> int: ...
    @slot.setter
    def slot(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None: ...

class HolodoriChargeStart(_Node):
    beat: int | float
    type: typing.Literal["holodoriChargeStart"]
    def __init__(self, *, beat: int | float) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class HolodoriChargeEnd(_Node):
    beat: int | float
    type: typing.Literal["holodoriChargeEnd"]
    def __init__(self, *, beat: int | float) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class HolodoriFeverStart(_Node):
    beat: int | float
    type: typing.Literal["holodoriFeverStart"]
    def __init__(self, *, beat: int | float) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class HolodoriFeverEnd(_Node):
    beat: int | float
    type: typing.Literal["holodoriFeverEnd"]
    def __init__(self, *, beat: int | float) -> None: ...
    def get_sus_sort_number(self) -> int: ...

class Score:
    format_metadata: dict[
        str,
        bool
        | int
        | float
        | str
        | rg_converters.format.Format[typing.Any, typing.Any, typing.Any],
    ]
    metadata: MetaData
    def __init__(
        self,
        metadata: MetaData,
        notes: collections.abc.Sequence[
            Bpm
            | TimeScaleGroup
            | Volume
            | Skill
            | FeverChance
            | FeverStart
            | Single
            | Slide
            | Guide
            | HolodoriSkill
            | HolodoriChargeStart
            | HolodoriChargeEnd
            | HolodoriFeverStart
            | HolodoriFeverEnd
        ],
    ) -> None: ...
    def beat_at_time(
        self, target_time: typing.SupportsFloat | typing.SupportsIndex
    ) -> float: ...
    def check_skill_overlap(self) -> bool: ...
    def combo_events(self) -> list[tuple[str, bool, float]]: ...
    def time_at_beat(
        self, target_beat: typing.SupportsFloat | typing.SupportsIndex
    ) -> float: ...
    @property
    def combo_count(self) -> int: ...
    @property
    def duration(self) -> float: ...
    @property
    def note_count(self) -> int: ...
    @property
    def notes(
        self,
    ) -> list[
        Bpm
        | TimeScaleGroup
        | Volume
        | Skill
        | FeverChance
        | FeverStart
        | Single
        | Slide
        | Guide
        | HolodoriSkill
        | HolodoriChargeStart
        | HolodoriChargeEnd
        | HolodoriFeverStart
        | HolodoriFeverEnd
    ]: ...
    @notes.setter
    def notes(
        self,
        arg0: collections.abc.Sequence[
            Bpm
            | TimeScaleGroup
            | Volume
            | Skill
            | FeverChance
            | FeverStart
            | Single
            | Slide
            | Guide
            | HolodoriSkill
            | HolodoriChargeStart
            | HolodoriChargeEnd
            | HolodoriFeverStart
            | HolodoriFeverEnd
        ],
    ) -> None: ...

class UnsupportedFeatureError(ValueError):
    pass

class BdonFormatError(ValueError):
    pass

class DecompressedSizeError(ValueError):
    pass

def detect(
    data: bytes,
    max_decompressed_size: typing.SupportsInt | typing.SupportsIndex = 134217728,
) -> list[str]: ...
