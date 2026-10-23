from dataclasses import dataclass
from typing import Optional, List, Union

__all__ = ["LevelDataEntity", "LevelData"]


@dataclass
class LevelDataEntity:
    archetype: str
    data: List[Union[dict, dict]]
    name: Optional[str] = None


@dataclass
class LevelData:
    entities: List[LevelDataEntity]
    bgmOffset: float = 0
