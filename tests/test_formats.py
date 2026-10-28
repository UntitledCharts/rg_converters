import dataclasses
import io
import json
import struct
from pathlib import Path

import pytest

from rg_converters import UnsupportedFeatureError, detect, transforms
from rg_converters import format as F
from rg_converters._rg_converters import formats as _formats
from rg_converters.core import (Bpm, FeverChance, FeverStart, Guide, GuidePoint, MetaData, Score, Single, Skill,
                                Slide, SlideEndPoint, SlideRelayPoint, SlideStartPoint, TimeScaleGroup,
                                TimeScalePoint)

TEST_FILES = Path(__file__).resolve().parent.parent / "test_files"
SUS_CHART = TEST_FILES / "88s_append.sus"
PJSK_CHART = TEST_FILES / "88s_append.pjsk.json"


def tap(beat: float, lane: float = 0, size: float = 1.5, **kw) -> Single:
    kw.setdefault("critical", False)
    kw.setdefault("timeScaleGroup", 0)
    return Single(beat=beat, lane=lane, size=size, **kw)


def start(beat: float, lane: float = 0, ease: str = "linear", critical: bool = False, tsg: int = 0,
          speed: float = 1.0) -> SlideStartPoint:
    return SlideStartPoint(beat, critical, ease, "normal", lane, 1.5, tsg, speed)


def relay(beat: float, lane: float = 0, ease: str = "linear", type: str = "tick", fake: bool = False,
          critical: bool | None = False) -> SlideRelayPoint:
    return SlideRelayPoint(beat, ease, lane, 1.5, 0, type, critical=critical, fake=fake)


def end(beat: float, lane: float = 0, critical: bool = False) -> SlideEndPoint:
    return SlideEndPoint(beat, critical, "normal", lane, 1.5, 0)


def guide(color: str, *beats: float, ease: str = "linear", fade: str = "out") -> Guide:
    return Guide(color, fade, [GuidePoint(b, ease, 0, 1.5, 0) for b in beats])


def group(*changes: tuple[float, float]) -> TimeScaleGroup:
    return TimeScaleGroup([TimeScalePoint(b, s) for b, s in changes])


def score(*notes, groups: int = 1) -> Score:
    tsgs = [group((0, 1)) for _ in range(groups)]
    return Score(MetaData("title", "artist", "designer", 0, None), [Bpm(0, 120), *tsgs, *notes])


def of(s: Score, kind: type) -> list:
    return [n for n in s.notes if isinstance(n, kind)]


# ---- the registry and the format API ----

def test_every_format_is_in_the_registry_in_order() -> None:
    assert [f.id for f in F.all()] == [i["id"] for i in _formats.all()]
    assert [f.id for f in F.all()] == ["sus", "yume_sus", "holodori_sus", "usc", "mmws", "ccmmws", "unchmmws",
                                       "pjsk", "bdon", "next_sekai", "chart_cyanvas", "untitled_sekai"]


@pytest.mark.parametrize("fmt", F.all(), ids=lambda f: f.id)
def test_python_options_match_the_registry(fmt: F.Format) -> None:
    for options, infos in ((fmt.ImportOptions, fmt.import_options), (fmt.ExportOptions, fmt.export_options)):
        fields = dataclasses.fields(options)
        assert [f.name for f in fields] == [i.name for i in infos]
        for field, info in zip(fields, infos):
            assert info.required == (field.default is dataclasses.MISSING)
            if not info.required:
                assert field.default == info.default


def test_format_attributes() -> None:
    assert F.sus.description.startswith("The classic old .sus format")
    assert F.usc.deprecated and F.sonolus_leveldata.chart_cyanvas.deprecated and F.sonolus_leveldata.untitled_sekai.deprecated
    assert not F.sus.deprecated
    assert F.holodori_sus.operations == ("load",)
    assert F.sonolus_leveldata.untitled_sekai.operations == ("export",)
    assert F.pjsk.file_types == (".pjsk", ".json", ".json.gz")
    assert F.sus.supports("layers") == "option"
    assert F.get("yume_sus") is F.yume_sus
    with pytest.raises(ValueError):
        F.get("nope")


def test_load_sources_agree() -> None:
    by_path = F.sus.load(SUS_CHART)
    assert F.sus.load(str(SUS_CHART)).combo_count == by_path.combo_count
    assert F.sus.load(SUS_CHART.read_bytes()).combo_count == by_path.combo_count
    assert F.sus.loads(SUS_CHART.read_text(encoding="utf-8")).combo_count == by_path.combo_count
    with open(SUS_CHART, encoding="utf-8") as f:
        assert F.sus.load(f).combo_count == by_path.combo_count
    with open(PJSK_CHART, "rb") as f:
        assert F.pjsk.load(f).combo_count == F.pjsk.load(PJSK_CHART).combo_count


def test_format_metadata_names_the_format() -> None:
    loaded = F.sus.load(SUS_CHART)
    assert loaded.format_metadata["format"] is F.sus
    assert transforms.shift(loaded).format_metadata["format"] is F.sus
    assert transforms.export_overlaps_score(loaded)[0].format_metadata["format"] is F.sus


def test_export_returns_the_written_score_and_leaves_the_input_alone(tmp_path: Path) -> None:
    s = score(tap(1, fake=True), tap(2))
    written = F.usc.export(tmp_path / "out.usc", s)
    assert len(of(s, Single)) == 2
    assert len(of(written, Single)) == 1
    assert F.usc.load(tmp_path / "out.usc").combo_count == 1
    buffer = io.BytesIO()
    F.mmw.mmws.export(buffer, s)
    assert F.mmw.mmws.load(buffer).combo_count == 1


def test_option_errors() -> None:
    s = score(tap(1))
    with pytest.raises(TypeError, match=r"pjsk\.ExportOptions\(music_id=\.\.\.\)"):
        F.pjsk.dumps(s)
    with pytest.raises(TypeError, match="usc.ExportOptions"):
        F.usc.dumps(s, F.sus.ExportOptions())  # type: ignore[arg-type]
    with pytest.raises(ValueError, match="no export option 'nope'"):
        _formats.dumps("usc", s, {"nope": True})
    with pytest.raises(ValueError, match="takes a bool"):
        _formats.dumps("usc", s, {"minified": 3})
    with pytest.raises(ValueError, match="can't be loaded"):
        F.sonolus_leveldata.untitled_sekai.loads(b"{}")
    with pytest.raises(ValueError, match="can't be exported"):
        F.bdon.dumps(s)


def test_text_formats_give_str_and_binary_formats_bytes() -> None:
    s = score(tap(1))
    assert isinstance(F.sus.dumps(s)[0], str)
    assert isinstance(F.mmw.mmws.dumps(s)[0], bytes)
    plain, _ = F.pjsk.dumps(s, F.pjsk.ExportOptions(music_id=7, compressed=False))
    assert json.loads(plain)["MusicId"] == 7
    assert F.pjsk.loads(plain).combo_count == 1


def test_mmws_variants_only_load_their_own() -> None:
    data, _ = F.mmw.ccmmws.dumps(score(tap(1)))
    assert F.mmw.ccmmws.loads(data).combo_count == 1
    with pytest.raises(ValueError, match="it is a .ccmmws file"):
        F.mmw.mmws.loads(data)
    with pytest.raises(ValueError, match="Not a .unchmmws file"):
        F.mmw.unchmmws.loads(data)


def test_features_used() -> None:
    s = score(
        tap(1, lane=6),
        tap(2, fake=True),
        tap(3, type="damage"),
        Slide(False, False, [start(4, ease="inout"), end(5, critical=True)]),
        guide("red", 6, 7, fade="none"),
        Skill(beat=8),
        tap(4001),
        groups=2,
    )
    ids = [f.id for f in F.features_used(s)]
    assert ids == ["extended_lanes", "layers", "measure_extensions", "fake_notes", "damage_notes", "skill_fever",
                   "extended_eases", "extended_guide_colors", "guide_fades", "critical_end_on_plain_slide"]
    assert [f.id for f in F.features_used(score(tap(1, speedRatio=2)))] == ["note_speed_ratios"]


def test_sus_skip_note_speed_ratios() -> None:
    text, _ = F.sus.dumps(score(tap(1, speedRatio=2)), F.sus.ExportOptions(allow_note_speed_ratios=True))
    assert of(F.sus.loads(text), Single)[0].speedRatio == 2
    assert of(F.sus.loads(text, F.sus.ImportOptions(skip_note_speed_ratios=True)), Single)[0].speedRatio == 1


# ---- bug fixes ----

def test_untitled_sekai_deletes_damage_and_fake_notes() -> None:
    _, written = F.sonolus_leveldata.untitled_sekai.dumps(score(tap(1, type="damage"), tap(2, fake=True), tap(3)))
    assert len(of(written, Single)) == 1


def test_mmws_without_a_timescale_group() -> None:
    s = Score(MetaData("t", "a", "d", 0, None), [Bpm(0, 120), tap(1)])
    for fmt in (F.mmw.mmws, F.mmw.ccmmws, F.mmw.unchmmws):
        data, written = fmt.dumps(s)
        assert len(of(written, TimeScaleGroup)) == 1
        assert fmt.loads(data).combo_count == 1


def test_unknown_ease_and_color_name_the_value() -> None:
    bad_ease = score(Slide(False, False, [start(1, ease="wobble"), end(2)]))
    with pytest.raises(ValueError, match="'wobble'.*beat 1"):
        F.sus.dumps(bad_ease)
    with pytest.raises(ValueError, match="'wobble'.*beat 1"):
        F.mmw.ccmmws.dumps(bad_ease)
    with pytest.raises(ValueError, match="'pink'"):
        F.sus.dumps(score(guide("pink", 1, 2)))
    with pytest.raises(ValueError, match="'pink'"):
        F.mmw.unchmmws.dumps(score(guide("pink", 1, 2)))


def test_next_sekai_checks_the_timescale_group() -> None:
    with pytest.raises(ValueError, match="timescale group 5"):
        F.sonolus_leveldata.next_sekai.dumps(score(tap(1, timeScaleGroup=5)))


def test_pjsk_deletes_fake_and_damage_notes() -> None:
    data, written = F.pjsk.dumps(score(tap(1, fake=True), tap(2, type="damage"), tap(3)),
                                 F.pjsk.ExportOptions(music_id=1))
    assert len(of(written, Single)) == 1
    assert F.pjsk.loads(data).combo_count == 1


def test_fake_relay_points() -> None:
    s = score(Slide(False, False, [start(1), relay(2, lane=2, fake=True), relay(3, type="attach", fake=True), end(4)]))
    points = of(transforms.delete_fake_notes(s), Slide)[0].connections
    assert len(points) == 3
    assert isinstance(points[1], SlideRelayPoint)
    assert points[1].lane == 2 and points[1].critical is None and not points[1].fake


def test_usc_writes_no_fake_key_on_relay_points() -> None:
    text, _ = F.usc.dumps(score(Slide(False, False, [start(1), relay(2), end(3)])))
    slide = next(o for o in json.loads(text)["usc"]["objects"] if o["type"] == "slide")
    assert all("fake" not in point for point in slide["connections"])


def test_mmws_keeps_trace_taps() -> None:
    for fmt in (F.mmw.mmws, F.mmw.ccmmws, F.mmw.unchmmws):
        back = fmt.loads(fmt.dumps(score(tap(1, trace=True)))[0])
        assert of(back, Single)[0].trace is True


def test_mmws_keeps_yellow_guides() -> None:
    back = F.mmw.mmws.loads(F.mmw.mmws.dumps(score(guide("yellow", 1, 2), guide("green", 3, 4)))[0])
    assert [g.color for g in of(back, Guide)] == ["yellow", "green"]


def test_mmws_keeps_fever_at_tick_zero() -> None:
    back = F.mmw.ccmmws.loads(F.mmw.ccmmws.dumps(score(FeverChance(beat=0), FeverStart(beat=4), tap(5)))[0])
    assert len(of(back, FeverChance)) == 1 and len(of(back, FeverStart)) == 1


def lane_extension(data: bytes) -> int:
    # CCMMWS\0, version, address table (metadata address first), then the metadata block.
    metadata = struct.unpack_from("<i", data, len(b"CCMMWS\0") + 4)[0]
    position = metadata
    for _ in range(4):
        position = data.index(b"\0", position) + 1
    position += 4
    position = data.index(b"\0", position) + 1
    return struct.unpack_from("<i", data, position)[0]


def test_mmws_lane_extension_counts_slides_and_guides() -> None:
    assert lane_extension(F.mmw.ccmmws.dumps(score(tap(1)))[0]) == 0
    assert lane_extension(F.mmw.ccmmws.dumps(score(Slide(False, False, [start(1, lane=8), end(2)])))[0]) == 4
    assert lane_extension(F.mmw.ccmmws.dumps(score(guide("green", 1, 2), Guide("green", "out", [
        GuidePoint(3, "linear", -9, 1, 0), GuidePoint(4, "linear", 0, 1, 0)])))[0]) == 4


def test_sus_measures_past_999() -> None:
    late = score(tap(4001))
    with pytest.raises(ValueError, match="allow_measure_extensions"):
        F.sus.dumps(late)
    text, _ = F.sus.dumps(late, F.sus.ExportOptions(allow_measure_extensions=True))
    assert "#MEASUREBS 1000" in text
    with pytest.raises(ValueError, match="measures up to 999"):
        F.yume_sus.dumps(late)


def test_sus_drops_extended_lanes() -> None:
    _, written = F.sus.dumps(score(tap(1, lane=6), tap(2)))
    assert len(of(written, Single)) == 1


@pytest.mark.parametrize("fmt", [F.yume_sus, F.mmw.mmws, F.pjsk], ids=lambda f: f.id)
def test_one_layer_formats_need_strip_layers(fmt: F.Format) -> None:
    two = score(tap(1), tap(2, timeScaleGroup=1), groups=2)
    extra = {"music_id": 1} if fmt is F.pjsk else {}
    with pytest.raises(ValueError, match="strip_layers"):
        fmt.dumps(two, fmt.ExportOptions(**extra))
    data, written = fmt.dumps(two, fmt.ExportOptions(strip_layers=True, **extra))
    assert len(of(written, TimeScaleGroup)) == 1
    assert {n.timeScaleGroup for n in of(written, Single)} == {0}
    assert len(of(fmt.loads(data), TimeScaleGroup)) == 1


def test_sus_layers_need_allow_layers() -> None:
    two = score(tap(1), tap(2, timeScaleGroup=1), groups=2)
    with pytest.raises(ValueError, match="allow_layers.*strip_layers transform"):
        F.sus.dumps(two)
    assert len(of(F.sus.dumps(transforms.strip_layers(two))[1], TimeScaleGroup)) == 1
    text, _ = F.sus.dumps(two, F.sus.ExportOptions(allow_layers=True))
    assert "#TIL01" in text


def test_strip_layers_transform() -> None:
    two = score(tap(1), tap(2, timeScaleGroup=1), groups=2)
    stripped = transforms.strip_layers(two)
    assert len(of(stripped, TimeScaleGroup)) == 1
    assert {n.timeScaleGroup for n in of(stripped, Single)} == {0}
    assert len(of(two, TimeScaleGroup)) == 2


def test_strip_extended_lanes_keeps_fake_slides() -> None:
    s = score(Slide(False, True, [start(1), end(2)]))
    assert of(transforms.strip_extended_lanes(s), Slide)[0].fake


def test_pjsk_replaces_extended_eases_and_colors() -> None:
    s = score(Slide(False, False, [start(1, ease="inout"), end(2)]), guide("red", 3, 4))
    back = F.pjsk.loads(F.pjsk.dumps(s, F.pjsk.ExportOptions(music_id=1))[0])
    assert of(back, Slide)[0].connections[0].ease == "out"
    assert of(back, Guide)[0].color == "yellow"


# ---- note speed ratios ----

def test_speed_ratios_become_layers() -> None:
    s = Score(MetaData("t", "a", "d", 0, None), [
        Bpm(0, 120), group((0, 1), (4, 0.5)), tap(1, speedRatio=2), tap(2)])
    for fmt in (F.sonolus_leveldata.next_sekai, F.sonolus_leveldata.chart_cyanvas, F.sonolus_leveldata.untitled_sekai, F.mmw.ccmmws, F.mmw.unchmmws):
        _, written = fmt.dumps(s)
        groups = of(written, TimeScaleGroup)
        assert len(groups) == 2, fmt.id
        assert [(c.beat, c.timeScale) for c in groups[1].changes] == [(0, 2), (4, 1)]
        fast, plain = of(written, Single)
        assert (fast.timeScaleGroup, fast.speedRatio, plain.timeScaleGroup) == (1, 1, 0)


def test_speed_ratios_without_a_group_keep_group_zero_default() -> None:
    s = Score(MetaData("t", "a", "d", 0, None), [Bpm(0, 120), tap(1, speedRatio=2), tap(2)])
    _, written = F.sonolus_leveldata.next_sekai.dumps(s)
    groups = of(written, TimeScaleGroup)
    assert [[(c.beat, c.timeScale) for c in g.changes] for g in groups] == [[], [(0, 2)]]
    assert [n.timeScaleGroup for n in of(written, Single)] == [1, 0]


def test_speed_ratios_in_one_layer_formats() -> None:
    s = score(tap(1, speedRatio=2))
    with pytest.raises(ValueError, match="note speed ratios.*strip_speed_ratios"):
        F.yume_sus.dumps(s)
    with pytest.raises(ValueError, match="allow_note_speed_ratios"):
        F.sus.dumps(s)
    assert "#TIL01" in F.sus.dumps(s, F.sus.ExportOptions(allow_layers=True))[0]
    assert of(F.sus.dumps(s, F.sus.ExportOptions(allow_note_speed_ratios=True))[1], Single)[0].speedRatio == 2
    # pjsk writes speed ratios itself.
    back = F.pjsk.loads(F.pjsk.dumps(s, F.pjsk.ExportOptions(music_id=1))[0])
    assert of(back, Single)[0].speedRatio == 2


def test_unsupported_feature_error_is_a_value_error() -> None:
    assert issubclass(UnsupportedFeatureError, ValueError)
    level = {"bgmOffset": 0, "entities": [{"archetype": "SomethingNew", "data": []}]}
    with pytest.raises(UnsupportedFeatureError):
        F.sonolus_leveldata.next_sekai.loads(json.dumps(level))


def test_detect_finds_what_each_exporter_writes() -> None:
    s = F.sus.load(SUS_CHART)
    for fmt in F.all():
        if "export" not in fmt.operations:
            continue
        options = fmt.ExportOptions(music_id=1) if fmt is F.pjsk else None  # type: ignore[call-arg]
        data, _ = fmt.dumps(s, options)
        found = detect(data.encode() if isinstance(data, str) else data)
        assert fmt.id in found or (fmt is F.sonolus_leveldata.untitled_sekai and "chart_cyanvas" in found), (fmt.id, found)


def test_holodori_sus_reports_its_bezier_count() -> None:
    loaded = F.holodori_sus.load(SUS_CHART)
    assert loaded.format_metadata["format"] is F.holodori_sus
    assert isinstance(loaded.format_metadata["bezier_count"], int)
    edited = loaded.format_metadata
    edited["note"] = "kept"
    loaded.format_metadata = edited
    assert loaded.format_metadata["note"] == "kept" and loaded.format_metadata["format"] is F.holodori_sus


def test_grouped_formats() -> None:
    from rg_converters.format.mmw import ccmmws
    from rg_converters.format.sonolus_leveldata import next_sekai

    assert ccmmws is F.mmw.ccmmws is F.get("ccmmws")
    assert next_sekai is F.sonolus_leveldata.next_sekai is F.get("next_sekai")
    assert [f.id for f in (F.mmw.mmws, F.mmw.ccmmws, F.mmw.unchmmws)] == ["mmws", "ccmmws", "unchmmws"]
    assert not hasattr(F, "ccmmws") and not hasattr(F, "next_sekai")
    with pytest.raises(TypeError, match=r"mmw\.mmws\.export takes mmw\.mmws\.ExportOptions"):
        F.mmw.mmws.dumps(score(tap(1)), F.sus.ExportOptions())  # type: ignore[arg-type]


# ---- notes without a timescale group (as sonolus-pjsekai-engine-extended reads them) ----

CC = F.sonolus_leveldata.chart_cyanvas


def group_entry(entity: dict, entry: dict | None) -> None:
    entity["data"] = [d for d in entity["data"] if d["name"] != "timeScaleGroup"]
    if entry is not None:
        entity["data"].append({"name": "timeScaleGroup", **entry})


def cyanvas_level(s: Score) -> dict:
    return json.loads(CC.dumps(s, CC.ExportOptions(compressed=False))[0])


def tap_groups(level: dict) -> list[int]:
    return [n.timeScaleGroup for n in sorted(of(CC.loads(json.dumps(level)), Single), key=lambda n: n.beat)]


def test_chart_cyanvas_notes_without_a_group_are_unscaled() -> None:
    level = cyanvas_level(Score(MetaData("t", "a", "d", 0, None), [Bpm(0, 120), group((0, 2)), *(tap(b) for b in (1, 2, 3, 4))]))
    assert level["entities"][0]["archetype"] == "Initialization"
    taps = [e for e in level["entities"] if e["archetype"] == "NormalTapNote"]
    group_entry(taps[1], None)
    group_entry(taps[2], {"value": None})
    group_entry(taps[3], {"ref": None})
    loaded = CC.loads(json.dumps(level))
    assert [[(c.beat, c.timeScale) for c in g.changes] for g in of(loaded, TimeScaleGroup)] == [[(0, 2)], []]
    assert tap_groups(level) == [0, 1, 1, 1]


def test_chart_cyanvas_group_numbers_are_entity_indices() -> None:
    level = cyanvas_level(Score(MetaData("t", "a", "d", 0, None), [Bpm(0, 120), group((0, 2)), tap(1), tap(2)]))
    index = next(i for i, e in enumerate(level["entities"]) if e["archetype"] == "TimeScaleGroup")
    taps = [e for e in level["entities"] if e["archetype"] == "NormalTapNote"]
    group_entry(taps[0], {"value": index})
    group_entry(taps[1], {"value": 0})
    assert tap_groups(level) == [0, 1]
    level["entities"].insert(0, level["entities"].pop(index))
    group_entry(taps[0], {"value": 0})
    group_entry(taps[1], None)
    assert tap_groups(level) == [0, 0]


def test_chart_cyanvas_without_groups() -> None:
    level = cyanvas_level(Score(MetaData("t", "a", "d", 0, None), [Bpm(0, 120), tap(1), tap(2)]))
    for entity in level["entities"]:
        if entity["archetype"] == "NormalTapNote":
            group_entry(entity, None)
    loaded = CC.loads(json.dumps(level))
    assert tap_groups(level) == [0, 0] and of(loaded, TimeScaleGroup) == []


def test_usc_notes_without_a_group_are_in_the_first() -> None:
    text, written = F.usc.dumps(score(tap(1), Slide(False, False, [start(2), end(3)]), guide("green", 4, 5)))
    usc = json.loads(text)
    for obj in usc["usc"]["objects"]:
        if obj["type"] == "single":
            obj["timeScaleGroup"] = None
        for point in obj.get("connections", []) + obj.get("midpoints", []):
            del point["timeScaleGroup"]
    loaded = F.usc.loads(json.dumps(usc))
    assert loaded.combo_count == written.combo_count
    assert of(loaded, Single)[0].timeScaleGroup == 0
    assert {p.timeScaleGroup for p in of(loaded, Slide)[0].connections} == {0}
