import json, os
import zipfile
import tempfile
import shutil
from pathlib import Path

from typing import TypedDict, List, Optional, Tuple, Dict, Any


def extract_file(zf: zipfile.ZipFile, src: str, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with zf.open(src) as f_src, open(dst, "wb") as f_dst:
        shutil.copyfileobj(f_src, f_dst)


class Level(TypedDict):
    data: Dict[str, Any]
    score: Path
    audio: Optional[Path]
    preview: Optional[Path]
    cover: Optional[Path]


def load_levels_from_scp(
    scp_path: Path,
) -> Tuple[List[Level], tempfile.TemporaryDirectory]:
    """
    Load all levels into a temporary folder and return metadata + file paths.
    Caller is responsible for cleaning up the tempdir (.cleanup()).
    """
    tmpdir = tempfile.TemporaryDirectory()
    tmp_root = Path(tmpdir.name)

    results = []

    with zipfile.ZipFile(scp_path, "r") as zf:
        try:
            with zf.open("sonolus/levels/list") as f:
                levels_data = json.load(f)
        except KeyError:
            print(f"No levels list found in {scp_path}")
            tmpdir.cleanup()
            return [], tmpdir

        for item in levels_data["items"]:
            level_name = item["name"]

            out = {
                "data": item,
                "audio": None,
                "preview": None,
                "cover": None,
            }

            if "data" in item:
                data_url = item["data"]["url"].lstrip("/")
                score_path = tmp_root / f"{level_name}_score"
                extract_file(zf, data_url, score_path)
                out["score"] = score_path
            else:
                raise KeyError("Missing score file.")

            if "bgm" in item:
                bgm_url = item["bgm"]["url"].lstrip("/")
                audio_path = tmp_root / f"{level_name}_bgm"
                extract_file(zf, bgm_url, audio_path)
                out["audio"] = audio_path

            if "preview" in item:
                preview_url = item["preview"]["url"].lstrip("/")
                preview_path = tmp_root / f"{level_name}_preview"
                extract_file(zf, preview_url, preview_path)
                out["preview"] = preview_path

            if "cover" in item:
                cover_url = item["cover"]["url"].lstrip("/")
                cover_path = tmp_root / f"{level_name}_cover"
                extract_file(zf, cover_url, cover_path)
                out["cover"] = cover_path

            results.append(out)

    return results, tmpdir


def replace_first_level(input_scp: str, output_scp: str, leveldata_file: str):
    with zipfile.ZipFile(input_scp, "r") as z_in:
        with zipfile.ZipFile(
            output_scp, "w", compression=zipfile.ZIP_DEFLATED
        ) as z_out:
            file = None
            for item in z_in.infolist():
                if (
                    item.filename.startswith("sonolus/levels/")
                    and os.path.basename(item.filename) not in ("info", "list")
                    and not item.filename.endswith("/")
                ):
                    with z_in.open(item) as f:
                        try:
                            data = json.load(f)
                            file = data.get("item", {}).get("data")
                            break
                        except Exception:
                            continue
            if not file:
                raise KeyError("No level file found")

            for item in z_in.infolist():
                with z_in.open(item) as f:
                    if item.filename == f"sonolus/repository/{file['hash']}":
                        z_out.writestr(item, open(leveldata_file, "rb").read())
                    else:
                        z_out.writestr(item.filename, f.read())
