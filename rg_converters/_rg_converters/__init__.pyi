from __future__ import annotations
import collections.abc
import typing
from . import transforms
from . import formats
__all__: list[str] = ['BdonFormatError', 'Bpm', 'FeverChance', 'FeverStart', 'Guide', 'GuidePoint', 'HolodoriChargeEnd', 'HolodoriChargeStart', 'HolodoriFeverEnd', 'HolodoriFeverStart', 'HolodoriSkill', 'MetaData', 'Score', 'Single', 'Skill', 'Slide', 'SlideEndPoint', 'SlideRelayPoint', 'SlideStartPoint', 'TimeScaleGroup', 'TimeScalePoint', 'UnsupportedFeatureError', 'Volume', 'detect', 'formats', 'transforms']
class _Node:
    pass
class MetaData:
    artist: str
    designer: str
    title: str
    waveoffset: int | float
    def __init__(self, title: str, artist: str, designer: str, waveoffset: int | float, requests: collections.abc.Sequence[str] | None) -> None:
        ...
    @property
    def requests(self) -> list[str] | None:
        ...
    @requests.setter
    def requests(self, arg0: collections.abc.Sequence[str] | None) -> None:
        ...
class Bpm(_Node):
    beat: int | float
    bpm: int | float
    type: str
    def __init__(self, beat: int | float, bpm: int | float, type: str = 'bpm') -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class TimeScalePoint(_Node):
    beat: int | float
    timeScale: int | float
    def __init__(self, beat: int | float, timeScale: int | float) -> None:
        ...
class TimeScaleGroup(_Node):
    type: str
    def __init__(self, changes: collections.abc.Sequence[TimeScalePoint] = [], type: str = 'timeScaleGroup') -> None:
        ...
    def append(self, time_scale: TimeScalePoint) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
    def insert(self, index: typing.SupportsInt | typing.SupportsIndex, time_scale: TimeScalePoint) -> None:
        ...
    @property
    def changes(self) -> list[TimeScalePoint]:
        ...
    @changes.setter
    def changes(self, arg0: collections.abc.Sequence[TimeScalePoint]) -> None:
        ...
class Volume(_Node):
    beat: int | float
    type: str
    volume: int | float
    def __init__(self, beat: int | float, volume: int | float, type: str = 'volume') -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class Skill(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class FeverChance(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class FeverStart(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class Single(_Node):
    beat: int | float
    critical: bool | None
    direction: str | None
    fake: bool
    lane: int | float
    size: int | float
    speedRatio: int | float
    trace: bool | None
    type: str
    def __init__(self, *, beat: int | float, critical: bool | None = None, lane: int | float, size: int | float, fake: bool = False, timeScaleGroup: typing.SupportsInt | typing.SupportsIndex, speedRatio: int | float = 1.0, trace: bool | None = None, direction: str | None = None, type: str = 'single') -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
    @property
    def timeScaleGroup(self) -> int:
        ...
    @timeScaleGroup.setter
    def timeScaleGroup(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class SlideStartPoint(_Node):
    beat: int | float
    critical: bool
    ease: str
    judgeType: str
    lane: int | float
    size: int | float
    speedRatio: int | float
    type: str
    def __init__(self, beat: int | float, critical: bool, ease: str, judgeType: str, lane: int | float, size: int | float, timeScaleGroup: typing.SupportsInt | typing.SupportsIndex, speedRatio: int | float = 1.0, type: str = 'start') -> None:
        ...
    @property
    def timeScaleGroup(self) -> int:
        ...
    @timeScaleGroup.setter
    def timeScaleGroup(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class SlideRelayPoint(_Node):
    beat: int | float
    critical: bool | None
    ease: str
    fake: bool
    lane: int | float
    size: int | float
    speedRatio: int | float
    type: str
    def __init__(self, beat: int | float, ease: str, lane: int | float, size: int | float, timeScaleGroup: typing.SupportsInt | typing.SupportsIndex, type: str, critical: bool | None = None, fake: bool = False, speedRatio: int | float = 1.0) -> None:
        ...
    @property
    def timeScaleGroup(self) -> int:
        ...
    @timeScaleGroup.setter
    def timeScaleGroup(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class SlideEndPoint(_Node):
    beat: int | float
    critical: bool
    direction: str | None
    judgeType: str
    lane: int | float
    size: int | float
    speedRatio: int | float
    type: str
    def __init__(self, beat: int | float, critical: bool, judgeType: str, lane: int | float, size: int | float, timeScaleGroup: typing.SupportsInt | typing.SupportsIndex, speedRatio: int | float = 1.0, direction: str | None = None, type: str = 'end') -> None:
        ...
    @property
    def timeScaleGroup(self) -> int:
        ...
    @timeScaleGroup.setter
    def timeScaleGroup(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class Slide(_Node):
    critical: bool
    fake: bool
    type: str
    def __init__(self, critical: bool, fake: bool = False, connections: collections.abc.Sequence[_Node] = [], type: str = 'slide') -> None:
        ...
    def append(self, slidepoint: _Node) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
    def sort(self) -> None:
        ...
    @property
    def connections(self) -> list[_Node]:
        ...
    @connections.setter
    def connections(self, arg0: collections.abc.Sequence[_Node]) -> None:
        ...
class GuidePoint(_Node):
    alpha: int | float | None
    beat: int | float
    ease: str
    lane: int | float
    size: int | float
    speedRatio: int | float
    def __init__(self, beat: int | float, ease: str | None, lane: int | float, size: int | float, timeScaleGroup: typing.SupportsInt | typing.SupportsIndex, speedRatio: int | float = 1.0, alpha: int | float | None = None) -> None:
        ...
    @property
    def timeScaleGroup(self) -> int:
        ...
    @timeScaleGroup.setter
    def timeScaleGroup(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class Guide(_Node):
    color: str
    color_code: str | None
    fade: str
    type: str
    def __init__(self, color: str, fade: str, midpoints: collections.abc.Sequence[GuidePoint] = [], type: str = 'guide', color_code: str | None = None) -> None:
        ...
    def append(self, guidepoint: GuidePoint) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
    def sort(self) -> None:
        ...
    @property
    def midpoints(self) -> list[GuidePoint]:
        ...
    @midpoints.setter
    def midpoints(self, arg0: collections.abc.Sequence[GuidePoint]) -> None:
        ...
class HolodoriSkill(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float, slot: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
    @property
    def slot(self) -> int:
        ...
    @slot.setter
    def slot(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class HolodoriChargeStart(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class HolodoriChargeEnd(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class HolodoriFeverStart(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class HolodoriFeverEnd(_Node):
    beat: int | float
    type: str
    def __init__(self, *, beat: int | float) -> None:
        ...
    def get_sus_sort_number(self) -> int:
        ...
class Score:
    format_metadata: dict
    metadata: MetaData
    def __init__(self, metadata: MetaData, notes: collections.abc.Sequence[_Node]) -> None:
        ...
    def beat_at_time(self, target_time: typing.SupportsFloat | typing.SupportsIndex) -> float:
        ...
    def check_skill_overlap(self) -> bool:
        ...
    def combo_events(self) -> list:
        ...
    def time_at_beat(self, target_beat: typing.SupportsFloat | typing.SupportsIndex) -> float:
        ...
    @property
    def combo_count(self) -> int:
        ...
    @property
    def duration(self) -> float:
        ...
    @property
    def note_count(self) -> int:
        ...
    @property
    def notes(self) -> list[_Node]:
        ...
    @notes.setter
    def notes(self, arg0: collections.abc.Sequence[_Node]) -> None:
        ...
class UnsupportedFeatureError(ValueError):
    pass
class BdonFormatError(ValueError):
    pass
def detect(data: bytes) -> list[str]:
    ...
