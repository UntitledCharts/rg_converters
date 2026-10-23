# ============================================================================
# OLD / LEGACY: this loader is the original Python implementation, kept as-is.
# It has NOT been ported to C++ (only chart_cyanvas EXPORT was ported).
# ============================================================================
import json
import gzip
from typing import IO, Dict, Any, List, Optional, Tuple
from concurrent.futures import ThreadPoolExecutor, as_completed

from ....core.score import Score
from ....core.metadata import MetaData
from ....core.bpm import Bpm
from ....core.timescale import TimeScaleGroup, TimeScalePoint
from ....core.single import Single
from ....core.slide import Slide, SlideStartPoint, SlideRelayPoint, SlideEndPoint
from ....core.guide import Guide, GuidePoint

from ....core.engine.archetypes import EngineArchetypeName, EngineArchetypeDataName

__all__ = ["load"]


# inverse maps (mirror of exporter)
_INV_DIRECTIONS = {-1: "left", 0: "up", 1: "right"}
_INV_EASES = {-2: "outin", -1: "out", 0: "linear", 1: "in", 2: "inout"}
_INV_SLIDE_STARTS = {0: "normal", 1: "trace", 2: "none"}
_INV_COLORS = {
    0: "neutral",
    1: "red",
    2: "green",
    3: "blue",
    4: "yellow",
    5: "purple",
    6: "cyan",
    7: "black",
}
_INV_FADES = {
    2: "in",
    0: "out",
    1: "none",
}


def _is_gzip_start(two_bytes: bytes) -> bool:
    return two_bytes[:2] == b"\x1f\x8b"


def _entity_data_map(entity: Dict[str, Any]) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    for item in entity.get("data", []):
        nm = item.get("name")
        if "value" in item:
            out[nm] = item["value"]
        elif "ref" in item:
            out[nm] = item["ref"]
    return out


def _is_timescale_group_archetype(archetype: str) -> bool:
    return archetype == "TimeScaleGroup"


def _is_timescale_change_archetype(archetype: str) -> bool:
    return archetype == "TimeScaleChange"


def _is_bpm_archetype(archetype: str) -> bool:
    return archetype == EngineArchetypeName.BpmChange


def _is_simline_archetype(archetype: str) -> bool:
    return archetype == "SimLine"


def _is_slide_start_archetype(archetype: str) -> bool:
    return "SlideStartNote" in archetype


def _is_slide_end_archetype(archetype: str) -> bool:
    return "SlideEndNote" in archetype or "SlideEndFlickNote" in archetype


def _is_slide_tick_archetype(archetype: str) -> bool:
    return "SlideTickNote" in archetype


def _is_slide_connector_archetype(archetype: str) -> bool:
    return archetype.endswith("SlideConnector")


def _is_single_archetype(archetype: str) -> bool:
    return any(
        x in archetype for x in ("TapNote", "FlickNote", "TraceNote", "DamageNote")
    )


def _parse_tsg_index(name: str) -> Optional[int]:
    if isinstance(name, str) and name.startswith("tsg:"):
        try:
            return int(name.split(":")[1])
        except Exception:
            return None
    return None


def load(fp: IO) -> Score:
    # read JSON (possibly gzipped)
    start = fp.peek(2) if hasattr(fp, "peek") else fp.read(2)
    if not hasattr(fp, "peek"):
        fp.seek(0)
    if _is_gzip_start(start):
        with gzip.GzipFile(fileobj=fp, mode="rb", mtime=0) as gz:
            leveldata = json.load(gz)
    else:
        leveldata = json.load(fp)

    metadata = MetaData(
        title="",
        artist="",
        designer="",
        waveoffset=leveldata.get("bgmOffset", 0),
        requests=["ticks_per_beat 480"],
    )

    # stage: collect raw named and unnamed entities
    raw_named: Dict[str, Dict[str, Any]] = {}
    raw_unnamed: List[Dict[str, Any]] = []
    for ent in leveldata.get("entities", []):
        name = ent.get("name")
        if name is not None:
            raw_named[name] = ent
        else:
            raw_unnamed.append(ent)

    # normalized entity representation: (name_or_None, normalized_dict)
    # normalized_dict: {"archetype": str, "data": dict}
    normalized_entities: List[Tuple[Optional[str], Dict[str, Any]]] = []

    for name, ent in raw_named.items():
        data_map = ent.get("data")
        if not isinstance(data_map, dict):
            data_map = _entity_data_map(ent)
        normalized_entities.append(
            (name, {"archetype": ent.get("archetype", ""), "data": data_map})
        )

    for ent in raw_unnamed:
        data_map = ent.get("data")
        if not isinstance(data_map, dict):
            data_map = _entity_data_map(ent)
        normalized_entities.append(
            (None, {"archetype": ent.get("archetype", ""), "data": data_map})
        )

    # build parsed dict for quick lookup by name (only for named)
    parsed: Dict[str, Dict[str, Any]] = {}
    for name, nd in normalized_entities:
        if name is not None:
            parsed[name] = nd

    # common keys
    beat_key = (
        EngineArchetypeDataName.Beat
        if hasattr(EngineArchetypeDataName, "Beat")
        else "beat"
    )
    bpm_key = (
        EngineArchetypeDataName.Bpm
        if hasattr(EngineArchetypeDataName, "Bpm")
        else "bpm"
    )

    # Build entity_cache mapping canonical_name -> normalized entry with quick fields
    # canonical_name: existing name for named, or generated "__unnamed_ent_i"
    entity_cache: Dict[str, Dict[str, Any]] = {}
    fast_lookup: Dict[Tuple[Any, Any, Any, Any], List[str]] = (
        {}
    )  # (beat,lane,size,tsg) -> list of names

    unnamed_index = 0
    for name, nd in normalized_entities:
        if name is None:
            canonical = f"__unnamed_ent_{unnamed_index}"
            unnamed_index += 1
        else:
            canonical = name
        archetype = nd.get("archetype", "")
        data_map = nd.get("data", {}) or {}
        beat = data_map.get(beat_key, data_map.get("beat", None))
        lane = data_map.get("lane", None)
        size = data_map.get("size", None)
        tsg = data_map.get("timeScaleGroup", data_map.get("timeScale", 0))
        if isinstance(tsg, str) and str(tsg).startswith("tsg:"):
            try:
                tsg = int(str(tsg).split(":", 1)[1])
            except Exception:
                tsg = 0
        direction = data_map.get("direction", None)
        entity_cache[canonical] = {
            "name": canonical,
            "orig_name": name,
            "archetype": archetype,
            "data": data_map,
            "beat": beat,
            "lane": lane,
            "size": size,
            "timeScaleGroup": tsg,
            "direction": direction,
        }
        # populate fast_lookup when beat/lane/size known
        if beat is not None and lane is not None and size is not None:
            key = (beat, lane, size, tsg)
            fast_lookup.setdefault(key, []).append(canonical)

    # convenience iterator similar to earlier behavior (unnamed first, then named)
    def _all_entities_iter():
        # unnamed in original order
        unnamed_counter = 0
        for ent in raw_unnamed:
            yield None, ent
            unnamed_counter += 1
        for name, ent in raw_named.items():
            yield name, ent

    notes: List[Any] = []

    # -------------------------
    # TimeScaleGroups (parallelized per group)
    # -------------------------
    tsg_items = [
        (n, v)
        for n, v in parsed.items()
        if _is_timescale_group_archetype(v["archetype"])
    ]

    def _process_tsg(
        item: Tuple[str, Dict[str, Any]],
    ) -> Optional[Tuple[int, TimeScaleGroup]]:
        name, ent = item
        idx = _parse_tsg_index(name)
        if idx is None:
            return None
        length = ent["data"].get("length", 0) or 0
        changes: List[TimeScalePoint] = []
        for i in range(int(length)):
            tsc_name = f"tsc:{idx}:{i}"
            tsc_ent = parsed.get(tsc_name)
            if tsc_ent:
                beat = tsc_ent["data"].get(beat_key, tsc_ent["data"].get("beat", 0.0))
                timeScale = tsc_ent["data"].get("timeScale", 1.0)
                changes.append(TimeScalePoint(beat=beat, timeScale=timeScale))
        if not changes:
            tsc_ent = parsed.get("tsc:0:0")
            if tsc_ent:
                beat = tsc_ent["data"].get(beat_key, tsc_ent["data"].get("beat", 0.0))
                timeScale = tsc_ent["data"].get("timeScale", 1.0)
                changes.append(TimeScalePoint(beat=beat, timeScale=timeScale))
        if not changes:
            changes = [TimeScalePoint(beat=0.0, timeScale=1.0)]
        changes.sort(key=lambda c: getattr(c, "beat", 0.0))
        return (idx, TimeScaleGroup(changes=changes))

    tsg_by_index: Dict[int, TimeScaleGroup] = {}
    if tsg_items:
        with ThreadPoolExecutor() as ex:
            futures = [ex.submit(_process_tsg, it) for it in tsg_items]
            for f in as_completed(futures):
                res = f.result()
                if res:
                    idx, tsg = res
                    tsg_by_index[idx] = tsg

    for idx in sorted(tsg_by_index.keys()):
        notes.append(tsg_by_index[idx])

    print("✔ Hi-Speeds/Layers")

    # -------------------------
    # BPMs
    # -------------------------
    for name, ent in _all_entities_iter():
        arch = ent.get("archetype", "")
        if _is_bpm_archetype(arch):
            data_map = (
                ent.get("data")
                if isinstance(ent.get("data"), dict)
                else _entity_data_map(ent)
            )
            beat = data_map.get(beat_key, data_map.get("beat", 0.0))
            bpm = data_map.get(bpm_key, data_map.get("bpm", None))
            if bpm is None:
                bpm = 160.0
            notes.append(Bpm(beat=beat, bpm=bpm))

    print("✔ BPMs")

    # -------------------------
    # Singles
    # -------------------------
    for name, ent in _all_entities_iter():
        arch = ent.get("archetype", "")
        if not _is_single_archetype(arch):
            continue
        if _is_simline_archetype(arch):
            continue
        if (
            _is_slide_start_archetype(arch)
            or _is_slide_end_archetype(arch)
            or _is_slide_connector_archetype(arch)
        ):
            continue
        if "Slide" in arch and (
            "TickNote" in arch or "Attached" in arch or "IgnoredSlideTickNote" in arch
        ):
            continue

        data_map = (
            ent.get("data")
            if isinstance(ent.get("data"), dict)
            else _entity_data_map(ent)
        )
        beat = data_map.get(beat_key, data_map.get("beat", None))
        lane = data_map.get("lane", None)
        size = data_map.get("size", None)
        timeScaleGroup = data_map.get("timeScaleGroup", data_map.get("timeScale", 0))
        if isinstance(timeScaleGroup, str) and str(timeScaleGroup).startswith("tsg:"):
            try:
                timeScaleGroup = int(str(timeScaleGroup).split(":", 1)[1])
            except Exception:
                timeScaleGroup = 0

        if arch == "DamageNote":
            s = Single(
                beat=beat,
                lane=lane,
                size=size,
                timeScaleGroup=timeScaleGroup,
                type="damage",
            )
            notes.append(s)
            continue

        critical = "Critical" in arch
        trace = "Trace" in arch
        direction_val = data_map.get("direction", None)
        direction = None
        if direction_val is not None:
            if arch == "NonDirectionalTraceFlickNote":
                direction = "up"
            else:
                try:
                    direction = _INV_DIRECTIONS.get(int(direction_val), None)
                except Exception:
                    direction = None

        if beat is None:
            continue

        s = Single(
            beat=beat,
            lane=lane,
            size=size,
            critical=critical,
            trace=trace,
            timeScaleGroup=timeScaleGroup,
            direction=direction,
        )
        notes.append(s)

    print("✔ Singles")

    # -------------------------
    # Slides
    # -------------------------
    connectors: Dict[str, Dict[str, Any]] = {}
    for name, nd in parsed.items():
        if _is_slide_connector_archetype(nd.get("archetype", "")):
            connectors[name] = {"archetype": nd["archetype"], "data": nd["data"]}

    unnamed_conn_count = 0
    for idx, ent in enumerate(raw_unnamed):
        arch = ent.get("archetype")
        if arch and _is_slide_connector_archetype(arch):
            data_map = (
                ent.get("data")
                if isinstance(ent.get("data"), dict)
                else _entity_data_map(ent)
            )
            gen_name = f"__unnamed_conn_{unnamed_conn_count}"
            unnamed_conn_count += 1
            connectors[gen_name] = {"archetype": arch, "data": data_map}

    connectors_by_start: Dict[str, List[Tuple[str, Dict[str, Any]]]] = {}
    for cname, cent in connectors.items():
        start_ref = cent["data"].get("start")
        start_name = start_ref.get("name") if isinstance(start_ref, dict) else start_ref
        if start_name is None:
            continue
        connectors_by_start.setdefault(start_name, []).append((cname, cent))

    slide_starts = [
        (n, e) for n, e in parsed.items() if _is_slide_start_archetype(e["archetype"])
    ]

    def _process_slide_start(start_pair: Tuple[str, Dict[str, Any]]) -> Optional[Slide]:
        start_name, start_ent = start_pair
        conns = connectors_by_start.get(start_name, [])
        if not conns:
            return None

        def _conn_head_beat(pair):
            _, cent = pair
            head_ref = cent["data"].get("head")
            head_name = head_ref.get("name") if isinstance(head_ref, dict) else head_ref
            head_entry = entity_cache.get(head_name)
            if head_entry:
                hb = head_entry.get("beat", 0.0)
                return hb if hb is not None else 0.0
            p = parsed.get(head_name)
            if p:
                hb = p["data"].get(beat_key, p["data"].get("beat", 0.0))
                return hb if hb is not None else 0.0
            return 0.0

        conns_sorted = sorted(conns, key=_conn_head_beat)

        ease_map: Dict[str, str] = {}
        joint_critical_map: Dict[str, Optional[bool]] = {}
        for cname, cent in conns_sorted:
            conn_data = cent["data"]
            ease_val = conn_data.get("ease", None)
            ease_str = _INV_EASES.get(ease_val, None) if ease_val is not None else None
            if ease_str is not None:
                for ref_field in ("start", "head", "tail", "end"):
                    ref = conn_data.get(ref_field)
                    ref_name = ref.get("name") if isinstance(ref, dict) else ref
                    if isinstance(ref_name, str):
                        ease_map[ref_name] = ease_str
            conn_is_critical = "Critical" in cent.get("archetype", "")
            for ref_field in ("head", "tail"):
                ref = conn_data.get(ref_field)
                ref_name = ref.get("name") if isinstance(ref, dict) else ref
                if isinstance(ref_name, str):
                    joint_critical_map[ref_name] = conn_is_critical

        end_ref_candidate = None
        for _, cent in conns_sorted:
            end_ref = cent["data"].get("end")
            end_name = end_ref.get("name") if isinstance(end_ref, dict) else end_ref
            if isinstance(end_name, str):
                end_ref_candidate = end_name
                break

        start_beat = start_ent["data"].get(
            beat_key, start_ent["data"].get("beat", None)
        )
        start_lane = start_ent["data"].get("lane", None)
        start_size = start_ent["data"].get("size", None)
        start_tsg = start_ent["data"].get("timeScaleGroup", 0)
        if isinstance(start_tsg, str) and str(start_tsg).startswith("tsg:"):
            try:
                start_tsg = int(str(start_tsg).split(":", 1)[1])
            except Exception:
                start_tsg = 0
        if "Hidden" in start_ent["archetype"]:
            start_judge = "none"
        elif "Trace" in start_ent["archetype"]:
            start_judge = "trace"
        else:
            start_judge = "normal"

        if start_beat is None or start_lane is None or start_size is None:
            raise RuntimeError(
                f"Slide start '{start_name}' missing required beat/lane/size: beat={start_beat}, lane={start_lane}, size={start_size}"
            )

        found = None
        for cname, cent in conns_sorted:
            head_ref = cent["data"].get("head")
            head_name = head_ref.get("name") if isinstance(head_ref, dict) else head_ref
            if head_name == start_name:
                found = (cname, cent)
                break

        if not found:
            raise RuntimeError(
                f"No connector where head == start for slide start '{start_name}'"
            )

        cname, cent = found
        sv = cent["data"].get("ease", None)
        if sv is None:
            raise RuntimeError(
                f"Connector '{cname}' referencing start '{start_name}' is missing 'ease'"
            )

        start_ease = sv if isinstance(sv, str) else _INV_EASES.get(sv)
        if start_ease is None:
            raise RuntimeError(
                f"Unknown ease value '{sv}' on connector '{cname}' for start '{start_name}'"
            )

        start_critical = ("Critical" in start_ent.get("archetype", "")) or (
            "Critical" in cent.get("archetype", "")
        )

        start_point = SlideStartPoint(
            beat=start_beat,
            critical=start_critical,
            ease=start_ease,
            judgeType=start_judge,
            lane=start_lane,
            size=start_size,
            timeScaleGroup=start_tsg,
        )

        joint_names: List[str] = []
        for _, cent in conns_sorted:
            data = cent["data"]
            for ref_field in ("head", "tail"):
                ref = data.get(ref_field)
                ref_name = ref.get("name") if isinstance(ref, dict) else ref
                if isinstance(ref_name, str) and ref_name not in joint_names:
                    joint_names.append(ref_name)

        conn_names_for_slide = [cname for cname, _ in conns_sorted]

        relay_items: List[Dict[str, Any]] = []

        # named ticks/attaches
        for ent_name, ent in parsed.items():
            arch = ent["archetype"]
            if not _is_slide_tick_archetype(arch) or arch == "IgnoredSlideTickNote":
                continue

            attach_ref = ent["data"].get("attach")
            slide_ref = ent["data"].get("slide")
            ref_names = []
            if attach_ref:
                ref_names.append(
                    attach_ref.get("name")
                    if isinstance(attach_ref, dict)
                    else attach_ref
                )
            if slide_ref:
                ref_names.append(
                    slide_ref.get("name") if isinstance(slide_ref, dict) else slide_ref
                )

            is_related = ent_name in joint_names or any(
                rn in conn_names_for_slide for rn in ref_names if rn
            )
            if not is_related:
                continue

            beat = ent["data"].get(beat_key, ent["data"].get("beat", None))
            if beat is None:
                raise RuntimeError(
                    f"Slide tick/attach '{ent_name}' missing beat for slide starting '{start_name}'"
                )
            lane = ent["data"].get("lane", None)
            size = ent["data"].get("size", None)
            tsg = ent["data"].get("timeScaleGroup", 0)
            if isinstance(tsg, str) and str(tsg).startswith("tsg:"):
                try:
                    tsg = int(str(tsg).split(":", 1)[1])
                except Exception:
                    tsg = 0
            rtype = "attach" if ("Attached" in arch) else "tick"

            if "Critical" in arch:
                rcritical = True
            elif "Normal" in arch:
                rcritical = False
            elif "Hidden" in arch or arch == "HiddenSlideTickNote":
                rcritical = None
            else:
                rcritical = joint_critical_map.get(ent_name, start_point.critical)

            rp_ease = ease_map.get(ent_name, start_ease)
            relay_items.append(
                {
                    "beat": beat,
                    "lane": lane,
                    "size": size,
                    "timeScaleGroup": tsg,
                    "type": rtype,
                    "critical": rcritical,
                    "ease": rp_ease,
                }
            )

        # unnamed ticks/attaches (match by references or spatial match)
        for idx, ent in enumerate(raw_unnamed):
            arch = ent.get("archetype")
            if (
                not arch
                or not _is_slide_tick_archetype(arch)
                or arch == "IgnoredSlideTickNote"
            ):
                continue

            data_map = (
                ent.get("data")
                if isinstance(ent.get("data"), dict)
                else _entity_data_map(ent)
            )
            attach_ref = data_map.get("attach")
            slide_ref = data_map.get("slide")
            ref_names = []
            if attach_ref:
                ref_names.append(
                    attach_ref.get("name")
                    if isinstance(attach_ref, dict)
                    else attach_ref
                )
            if slide_ref:
                ref_names.append(
                    slide_ref.get("name") if isinstance(slide_ref, dict) else slide_ref
                )

            referenced = any(rn in conn_names_for_slide for rn in ref_names if rn)
            if not referenced:
                b = data_map.get(beat_key, data_map.get("beat", None))
                lane = data_map.get("lane", None)
                size = data_map.get("size", None)
                tsg = data_map.get("timeScaleGroup", None)
                if isinstance(tsg, str) and str(tsg).startswith("tsg:"):
                    try:
                        tsg = int(str(tsg).split(":", 1)[1])
                    except Exception:
                        tsg = 0
                if b is not None and lane is not None and size is not None:
                    key = (b, lane, size, tsg)
                    candidates = fast_lookup.get(key, [])
                    for cand in candidates:
                        cand_entry = entity_cache.get(cand)
                        if not cand_entry:
                            continue
                        orig_name = cand_entry.get("orig_name")
                        if orig_name in joint_names:
                            referenced = True
                            break

            if not referenced:
                continue

            gen_name = f"__unnamed_tick_{idx}"
            beat = data_map.get(beat_key, data_map.get("beat", None))
            if beat is None:
                raise RuntimeError(
                    f"Unnamed slide tick at index {idx} missing beat for slide starting '{start_name}'"
                )
            lane = data_map.get("lane", None)
            size = data_map.get("size", None)
            tsg = data_map.get("timeScaleGroup", 0)
            if isinstance(tsg, str) and str(tsg).startswith("tsg:"):
                try:
                    tsg = int(str(tsg).split(":", 1)[1])
                except Exception:
                    tsg = 0
            rtype = "attach" if ("Attached" in arch) else "tick"

            if "Critical" in arch:
                rcritical = True
            elif "Normal" in arch:
                rcritical = False
            elif "Hidden" in arch or arch == "HiddenSlideTickNote":
                rcritical = None
            else:
                rcritical = start_point.critical

            rp_ease = ease_map.get(gen_name, start_ease)
            relay_items.append(
                {
                    "beat": beat,
                    "lane": lane,
                    "size": size,
                    "timeScaleGroup": tsg,
                    "type": rtype,
                    "critical": rcritical,
                    "ease": rp_ease,
                }
            )

        if not end_ref_candidate or end_ref_candidate not in parsed:
            raise RuntimeError(
                f"Couldn't find end entity for slide starting at '{start_name}'"
            )

        end_ent = parsed[end_ref_candidate]
        end_beat = end_ent["data"].get(beat_key, end_ent["data"].get("beat", None))
        end_lane = end_ent["data"].get("lane", None)
        end_size = end_ent["data"].get("size", None)
        end_tsg = end_ent["data"].get("timeScaleGroup", 0)
        if isinstance(end_tsg, str) and str(end_tsg).startswith("tsg:"):
            try:
                end_tsg = int(str(end_tsg).split(":", 1)[1])
            except Exception:
                end_tsg = 0
        if "Hidden" in end_ent["archetype"]:
            end_judge = "none"
        elif "Trace" in end_ent["archetype"]:
            end_judge = "trace"
        else:
            end_judge = "normal"
        dir_val = end_ent["data"].get("direction", None)
        direction = (
            _INV_DIRECTIONS.get(int(dir_val), None) if dir_val is not None else None
        )

        if end_beat is None or end_lane is None or end_size is None:
            raise RuntimeError(
                f"Slide end '{end_ref_candidate}' missing required beat/lane/size: beat={end_beat}, lane={end_lane}, size={end_size}"
            )

        end_critical = "Critical" in end_ent["archetype"]

        end_point = SlideEndPoint(
            beat=end_beat,
            critical=end_critical,
            judgeType=end_judge,
            lane=end_lane,
            size=end_size,
            timeScaleGroup=end_tsg,
            direction=direction,
        )

        # sort relay items by beat and fill missing lane/size from previous (or start if first)
        relay_items_sorted = sorted(relay_items, key=lambda r: r["beat"])
        filled_relays: List[SlideRelayPoint] = []
        prev_lane = start_point.lane
        prev_size = start_point.size
        for item in relay_items_sorted:
            lane = item.get("lane", None)
            size = item.get("size", None)
            if lane is None:
                lane = prev_lane
            if size is None:
                size = prev_size
            prev_lane = lane
            prev_size = size
            rp = SlideRelayPoint(
                beat=item["beat"],
                ease=item["ease"],
                lane=lane,
                size=size,
                timeScaleGroup=item.get("timeScaleGroup", 0),
                type=item.get("type", "tick"),
                critical=item.get("critical"),
            )
            filled_relays.append(rp)

        connections_list = [start_point] + filled_relays + [end_point]
        slide_obj = Slide(
            critical=bool(start_point.critical), connections=connections_list
        )
        slide_obj.sort()
        return slide_obj

    slides_results: List[Slide] = []
    if slide_starts:
        with ThreadPoolExecutor() as ex:
            futures = [ex.submit(_process_slide_start, sp) for sp in slide_starts]
            for f in as_completed(futures):
                s = f.result()
                if s:
                    slides_results.append(s)
    notes.extend(slides_results)

    print("✔ Slides")

    # Guides
    # note: guides can overlap.
    # this is a massive headache.
    # it is IMPOSSIBLE to determine some placements of guides, so this will do a best-guess.
    guide_segments_raw = []
    for ent in raw_unnamed:
        if ent.get("archetype") == "Guide":
            guide_segments_raw.append(
                {d["name"]: d.get("value", d.get("ref")) for d in ent.get("data", [])}
            )
    for name, ent in parsed.items():
        if ent.get("archetype") == "Guide":
            guide_segments_raw.append(ent["data"])

    def _tsg_val(val):
        if isinstance(val, str) and val.startswith("tsg:"):
            try:
                return int(val.split(":", 1)[1])
            except Exception:
                return 0
        return val if isinstance(val, (int, float)) else 0

    def _conv_ease_required(ease_val):
        if isinstance(ease_val, str):
            return ease_val
        mapped = _INV_EASES.get(ease_val)
        if mapped is None:
            raise RuntimeError(
                f"Unknown/missing ease value for Guide segment: {ease_val}"
            )
        return mapped

    segment_nodes = []
    for data_map in guide_segments_raw:
        segment_nodes.append(
            {
                "start": GuidePoint(
                    beat=data_map.get("startBeat"),
                    lane=data_map.get("startLane"),
                    size=data_map.get("startSize"),
                    timeScaleGroup=_tsg_val(data_map.get("startTimeScaleGroup")),
                    ease=None,
                ),
                "head": GuidePoint(
                    beat=data_map.get("headBeat"),
                    lane=data_map.get("headLane"),
                    size=data_map.get("headSize"),
                    timeScaleGroup=_tsg_val(data_map.get("headTimeScaleGroup")),
                    ease=None,
                ),
                "tail": GuidePoint(
                    beat=data_map.get("tailBeat"),
                    lane=data_map.get("tailLane"),
                    size=data_map.get("tailSize"),
                    timeScaleGroup=_tsg_val(data_map.get("tailTimeScaleGroup")),
                    ease=None,
                ),
                "end": GuidePoint(
                    beat=data_map.get("endBeat"),
                    lane=data_map.get("endLane"),
                    size=data_map.get("endSize"),
                    timeScaleGroup=_tsg_val(data_map.get("endTimeScaleGroup")),
                    ease=None,
                ),
                "ease_val": data_map.get("ease"),
                "color": _INV_COLORS.get(data_map.get("color")),
                "fade": _INV_FADES.get(data_map.get("fade")),
            }
        )

    groups = {}
    for seg in segment_nodes:
        start_key = (
            seg["start"].lane,
            seg["start"].size,
            seg["start"].beat,
            seg["start"].timeScaleGroup,
        )
        end_key = (
            seg["end"].lane,
            seg["end"].size,
            seg["end"].beat,
            seg["end"].timeScaleGroup,
        )
        color = seg["color"]
        groups.setdefault((start_key, end_key, color), []).append(seg)

    guides_results = []

    for (_, _, color), segs in groups.items():
        remaining = list(segs)
        remaining.sort(
            key=lambda s: (
                getattr(s["head"], "beat", 0.0),
                getattr(s["tail"], "beat", 0.0),
            )
        )

        def _key_head(s):
            h = s["head"]
            return (h.lane, h.size, h.beat, h.timeScaleGroup)

        def _key_tail(s):
            t = s["tail"]
            return (t.lane, t.size, t.beat, t.timeScaleGroup)

        while remaining:
            tail_keys = {_key_tail(s) for s in remaining}
            start_candidates = [s for s in remaining if _key_head(s) not in tail_keys]

            if start_candidates:
                start_seg = min(
                    start_candidates, key=lambda s: getattr(s["head"], "beat", 0.0)
                )
                remaining.remove(start_seg)
            else:
                start_seg = remaining.pop(0)

            chain = [start_seg]
            cur_tail_key = _key_tail(chain[-1])

            while True:
                candidates = [
                    s
                    for s in remaining
                    if _key_head(s) == cur_tail_key and s["color"] == chain[-1]["color"]
                ]
                if not candidates:
                    break
                next_seg = min(
                    candidates,
                    key=lambda s: (
                        getattr(s["head"], "beat", 0.0),
                        getattr(s["tail"], "beat", 0.0),
                    ),
                )
                remaining.remove(next_seg)
                chain.append(next_seg)
                cur_tail_key = _key_tail(next_seg)

            midpoints = []
            first_seg = chain[0]
            start_ease = _conv_ease_required(first_seg.get("ease_val"))
            sp = first_seg["start"]
            sp.ease = start_ease
            midpoints.append(sp)

            for seg in chain:
                head_ease = _conv_ease_required(seg.get("ease_val"))
                midpoints[-1].ease = head_ease
                midpoints.append(seg["tail"])

            midpoints[-1].ease = "linear"
            guides_results.append(
                Guide(midpoints=midpoints, color=color, fade=chain[0]["fade"])
            )

    notes.extend(guides_results)
    print("✔ Guides")

    # final score
    score = Score(metadata=metadata, notes=notes)
    score.sort_by_beat()
    return score
