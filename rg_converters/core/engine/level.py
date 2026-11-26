from dataclasses import dataclass
from typing import Any, Dict, Optional, List

__all__ = ["LevelDataEntity", "LevelData"]


@dataclass
class LevelDataEntity:
    archetype: str
    data: List[Dict[str, Any]]
    name: Optional[str] = None


@dataclass
class LevelData:
    entities: List[LevelDataEntity]
    bgmOffset: float = 0
