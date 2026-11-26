import base64
import gzip
import io
import json
import struct
import threading
import time
import zipfile
import zlib
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

from rg_converters import DecompressedSizeError, detect, transforms
from rg_converters import format as F
from rg_converters.core import Score

TEST_FILES = Path(__file__).resolve().parent.parent / "test_files"
SUS_CHART = TEST_FILES / "88s_append.sus"
PJSK_CHART = TEST_FILES / "88s_append.pjsk.json"
NS = F.sonolus_leveldata.next_sekai
CC = F.sonolus_leveldata.chart_cyanvas
US = F.sonolus_leveldata.untitled_sekai
DEFAULT = 128 * 1024 * 1024
BOMB = DEFAULT + (1 << 20)


def padded_gzip(document: bytes, size: int) -> bytes:
    compressor = zlib.compressobj(9, zlib.DEFLATED, 31)
    parts = [compressor.compress(document)]
    left = size - len(document)
    chunk = b" " * (1 << 20)
    while left > 0:
        parts.append(compressor.compress(chunk[:left]))
        left -= len(chunk)
    parts.append(compressor.flush())
    return b"".join(parts)


def level_json() -> bytes:
    return NS.dumps(F.sus.load(SUS_CHART), NS.ExportOptions(compressed=False))[0]


def project(level: bytes, method: int = zipfile.ZIP_DEFLATED) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", method) as z:
        z.writestr(
            "project.json", json.dumps({"format": "nxsp", "level": "level.json"})
        )
        z.writestr("level.json", level)
    return buffer.getvalue()


def declare_size(archive: bytes, name: str, size: int) -> bytes:
    data = bytearray(archive)
    at = data.index(b"PK\x01\x02")
    while True:
        name_length, extra, comment = struct.unpack_from("<HHH", data, at + 28)
        if data[at + 46 : at + 46 + name_length] == name.encode():
            struct.pack_into("<I", data, at + 24, size)
            return bytes(data)
        at += 46 + name_length + extra + comment


def test_the_limit_is_an_option_defaulting_to_128_mib() -> None:
    for loaded in (F.pjsk, F.bdon, NS, CC):
        assert [
            o.default
            for o in loaded.import_options
            if o.name == "max_decompressed_size"
        ] == [DEFAULT]
    for exported in (F.pjsk, NS, CC, US):
        assert [
            o.default
            for o in exported.export_options
            if o.name == "max_decompressed_size"
        ] == [DEFAULT]
    assert issubclass(DecompressedSizeError, ValueError)
    assert F.DecompressedSizeError is DecompressedSizeError


def test_gzip_bomb_is_rejected_unless_the_limit_is_raised() -> None:
    combo = F.sus.load(SUS_CHART).combo_count
    bomb = padded_gzip(level_json(), BOMB)
    assert len(bomb) < BOMB // 100
    with pytest.raises(DecompressedSizeError, match="max_decompressed_size"):
        NS.loads(bomb)
    with pytest.raises(DecompressedSizeError):
        detect(bomb)
    raised = 2 * DEFAULT
    assert (
        NS.loads(bomb, NS.ImportOptions(max_decompressed_size=raised)).combo_count
        == combo
    )
    assert detect(bomb, max_decompressed_size=raised) == ["next_sekai"]


@pytest.mark.parametrize("encoding", ["gzip", "base64"])
def test_pjsk_gzip_is_limited(encoding: str) -> None:
    chart = F.pjsk.load(PJSK_CHART)
    document = F.pjsk.dumps(chart, F.pjsk.ExportOptions(music_id=1, compressed=False))[
        0
    ]
    data = gzip.compress(document)
    if encoding == "base64":
        data = base64.b64encode(data)
    assert F.pjsk.loads(data).combo_count == chart.combo_count
    with pytest.raises(DecompressedSizeError):
        F.pjsk.loads(
            data, F.pjsk.ImportOptions(max_decompressed_size=len(document) - 1)
        )
    with pytest.raises(DecompressedSizeError):
        detect(data, max_decompressed_size=len(document) - 1)
    exact = F.pjsk.ImportOptions(max_decompressed_size=len(document))
    assert F.pjsk.loads(data, exact).combo_count == chart.combo_count
    assert detect(data, max_decompressed_size=len(document)) == ["pjsk"]


def test_bdon_and_chart_cyanvas_gzip_are_limited() -> None:
    bdon = json.dumps({"score": {"notes": []}}).encode()
    assert isinstance(F.bdon.loads(gzip.compress(bdon)), Score)
    with pytest.raises(DecompressedSizeError):
        F.bdon.loads(
            gzip.compress(bdon),
            F.bdon.ImportOptions(max_decompressed_size=len(bdon) - 1),
        )
    level = CC.dumps(F.sus.load(SUS_CHART), CC.ExportOptions(compressed=False))[0]
    with pytest.raises(DecompressedSizeError):
        CC.loads(
            gzip.compress(level), CC.ImportOptions(max_decompressed_size=len(level) - 1)
        )
    assert CC.loads(
        gzip.compress(level), CC.ImportOptions(max_decompressed_size=len(level))
    ).combo_count


def test_detect_respects_its_limit() -> None:
    level = level_json()
    gzipped = gzip.compress(level)
    assert detect(gzipped) == ["next_sekai"]
    assert detect(gzipped, max_decompressed_size=len(level)) == ["next_sekai"]
    with pytest.raises(DecompressedSizeError):
        detect(gzipped, max_decompressed_size=len(level) - 1)
    assert detect(level, max_decompressed_size=0) == ["next_sekai"]
    archive = project(level)
    assert detect(archive) == ["next_sekai"]
    with pytest.raises(DecompressedSizeError):
        detect(archive, max_decompressed_size=len(level) - 1)
    with pytest.raises(ValueError, match="negative"):
        detect(gzipped, max_decompressed_size=-1)


def test_nxsp_entries_are_limited() -> None:
    level = level_json()
    combo = F.sus.load(SUS_CHART).combo_count
    for method in (zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED):
        archive = project(level, method)
        assert NS.loads(archive).combo_count == combo
        with pytest.raises(DecompressedSizeError, match="level.json"):
            NS.loads(archive, NS.ImportOptions(max_decompressed_size=len(level) - 1))
        assert (
            NS.loads(
                archive, NS.ImportOptions(max_decompressed_size=len(level))
            ).combo_count
            == combo
        )
    with pytest.raises(DecompressedSizeError):
        NS.loads(project(padded_gzip(level, BOMB), zipfile.ZIP_STORED))


def test_zip_with_a_lying_size_is_rejected() -> None:
    level = level_json()
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(
            "project.json", json.dumps({"format": "nxsp", "level": "level.json"})
        )
        with z.open("level.json", "w") as f:
            f.write(level)
            for _ in range(BOMB >> 20):
                f.write(b" " * (1 << 20))
    honest = buffer.getvalue()
    with pytest.raises(DecompressedSizeError, match="level.json decompresses to"):
        NS.loads(honest)
    understated = declare_size(honest, "level.json", 100)
    with pytest.raises(ValueError, match="level.json is corrupt") as small:
        NS.loads(understated, NS.ImportOptions(max_decompressed_size=2 * DEFAULT))
    assert not isinstance(small.value, DecompressedSizeError)
    assert detect(understated) == []
    overstated = declare_size(project(level), "level.json", 0xFFFFFFF0)
    with pytest.raises(DecompressedSizeError):
        NS.loads(overstated)
    with pytest.raises(DecompressedSizeError):
        detect(overstated)
    with pytest.raises(ValueError, match="level.json is corrupt"):
        NS.loads(overstated, NS.ImportOptions(max_decompressed_size=1 << 40))


def test_exports_limit_what_they_compress() -> None:
    score = F.sus.load(SUS_CHART)
    with pytest.raises(DecompressedSizeError, match="max_decompressed_size"):
        NS.dumps(score, NS.ExportOptions(max_decompressed_size=1000))
    assert (
        NS.dumps(score, NS.ExportOptions(compressed=False, max_decompressed_size=1000))[
            0
        ]
        == level_json()
    )
    gzipped = NS.dumps(score)[0]
    exact = len(gzip.decompress(gzipped))
    assert NS.dumps(score, NS.ExportOptions(max_decompressed_size=exact))[0] == gzipped
    with pytest.raises(DecompressedSizeError):
        NS.dumps(score, NS.ExportOptions(max_decompressed_size=exact - 1))
    with pytest.raises(DecompressedSizeError):
        F.pjsk.dumps(
            score,
            F.pjsk.ExportOptions(
                music_id=1, strip_layers=True, max_decompressed_size=1000
            ),
        )
    with pytest.raises(DecompressedSizeError):
        CC.dumps(score, CC.ExportOptions(max_decompressed_size=1000))
    with pytest.raises(DecompressedSizeError):
        US.dumps(score, US.ExportOptions(max_decompressed_size=1000))
    with pytest.raises(ValueError, match="negative"):
        NS.dumps(score, NS.ExportOptions(max_decompressed_size=-1))


@pytest.mark.parametrize("call", [detect, NS.loads], ids=["detect", "loads"])
def test_calls_run_without_the_gil(call: Callable[[bytes], object]) -> None:
    data = padded_gzip(level_json(), 96 * 1024 * 1024)
    started = threading.Event()
    window: list[float] = []

    def work() -> None:
        started.set()
        window.append(time.perf_counter())
        call(data)
        window.append(time.perf_counter())

    worker = threading.Thread(target=work)
    worker.start()
    started.wait()
    seen: list[float] = []
    ticks = 0
    while worker.is_alive():
        ticks += 1
        if ticks % 256 == 0:
            seen.append(time.perf_counter())
    worker.join()
    start, end = window
    quarter = (end - start) / 4
    assert any(start + quarter < t < end - quarter for t in seen)


def test_threads_can_share_a_score() -> None:
    score = F.sus.load(SUS_CHART)

    def work(_: int) -> tuple[str, int, bytes]:
        return (
            F.usc.dumps(score)[0],
            transforms.shift(score).combo_count,
            NS.dumps(score)[0],
        )

    expected = work(0)
    with ThreadPoolExecutor(8) as pool:
        assert all(r == expected for r in pool.map(work, range(32)))
