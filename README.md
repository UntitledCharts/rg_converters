# rg-converters
Convert between different mobile rhythm game file formats such as Sonolus' common LevelData types, Project Sekai's SUS and score maker JSON, hololive Dream's SUS, BanG Dream! Our Notes' JSON, and some chart editor exclusive formats. Can also be used to easily read and/or write chart data programmatically to a common Score class.

Written with a C++ core.

**Looking for the old `sonolus-level-converters`? You can install it from `https://github.com/UntitledCharts/rg_converter/tree/100e0ac4e5d2a895089b565f38800d1cc6a0b28e`.** It is no longer supported.

Requires `Python >= 3.10`. Supports free-threaded Python (`3.14t` and newer). Objects can be shared between threads as long as you don't modify a note in one thread while another thread is converting a `Score` that contains it.

## Installation
`pip install rg-converters` (add `[cli]` for the command line: `pip install rg-converters[cli]`)

## Usage
```python
from rg_converters import detect, transforms
from rg_converters.format import pjsk, sus, yume_sus
from rg_converters.format.mmw import ccmmws
from rg_converters.format.sonolus_leveldata import next_sekai

score = sus.load("chart.sus")                   # a path, the file's bytes, or an open file
score = transforms.shift(score)                 # transforms return a new Score
written = yume_sus.export("yume.sus", score)    # exports return the Score as written
pjsk.export("chart.pjsk", score, pjsk.ExportOptions(music_id=1))
text, written = sus.dumps(score, sus.ExportOptions(allow_layers=True))   # the file's contents instead
ccmmws.export("chart.ccmmws", score)
next_sekai.export("level.json", score, next_sekai.ExportOptions(compressed=False))

detect("chart.sus")                             # the format ids it could be, e.g. ['sus', 'yume_sus']
```
A `Score` is an ordinary object: read and edit its `notes` and `metadata` freely. Exporters and `transforms` never modify the score passed in; they work on a copy and return it, and the result shares nothing with the input. `score.format_metadata` says what the score was loaded from (`"format"`) and anything else the loader found (holodori_sus: `"bezier_count"`).

The MikuMikuWorld formats (`mmws`, `ccmmws`, `unchmmws`) are grouped in `rg_converters.format.mmw` and the LevelData ones (`next_sekai`, `chart_cyanvas`, `untitled_sekai`) in `rg_converters.format.sonolus_leveldata`; the rest are in `rg_converters.format` itself. Every format has the same shape: `load`/`loads`, `export`/`dumps`, its own `ImportOptions`/`ExportOptions`, and `id`, `description`, `deprecated`, `operations`, `file_types` and `features` (how it holds each feature: `yes`, `option`, `lossy` or `no`). `format.all()` lists them all, `format.get(id)` finds one by its `detect` id, `format.features_used(score)` says what a chart needs. An export raises `ValueError` when the chart can't be written as asked, saying which option or transform fixes it (for instance layers in a format that has one: pass `strip_layers=True` or apply `transforms.strip_layers`).

## Command line
```sh
rg-converters convert chart.sus chart.usc --to usc     # asks for anything left out
rg-converters detect chart.sus
rg-converters info chart.sus
```
The input format always comes from the file's content (never its name) and is confirmed before loading; `--from` forces one. `convert` takes options as `-i NAME=VALUE` (import) and `-e NAME=VALUE` (export), applies `--transform NAME` (a bare `--transform` picks from a list), and warns about features the output format can't fully keep. `--yes`, `--quiet` and `--json` are for scripts. Exit codes: 0 done, 1 unexpected error, 2 bad usage, 3 format not detected or unsupported, 4 load failed, 5 export or write failed, 130 aborted.

### Upgrading from 2.x
- `rg_converters.converters` is replaced by `rg_converters.format` (see Usage), and options are passed as `ImportOptions`/`ExportOptions`:
  - `sus`: `delete_damage=True` is now `allow_damage=False`, `keep_note_speed_ratios` is `allow_note_speed_ratios`, `measure_extensions` is `allow_measure_extensions`. `allow_extended_lanes` is gone: sus export always drops what's outside its 12 lanes (the lanes past them belong to skill and fever). New import option `skip_note_speed_ratios`.
  - `mmws`, `ccmmws` and `unchmmws` are separate formats in `format.mmw`; each loads only its own variant. The LevelData formats are in `format.sonolus_leveldata`.
  - `pjsk` takes `music_id` (required) and `compressed` (`False` writes plain JSON). `pjsk.load_raw` is gone.
  - LevelData `as_compressed` is `compressed`. `holodori_sus`'s `count_bezier` is gone: the count is in `score.format_metadata["bezier_count"]`.
  - `UnsupportedFeatureError` and `BdonFormatError` (both `ValueError`s) are in `rg_converters`.
  - Text formats are written exactly as produced (`\n` line endings on every OS).
- Layers: formats with one layer (`yume_sus`, `mmws`, `pjsk`) raise on more unless `strip_layers=True`; `sus` raises unless `allow_layers=True`. Note speed ratios become layers in formats that can't hold them (all but `usc`, `pjsk`, and `sus` with `allow_note_speed_ratios`), keeping how the notes move.
- Fixed exports: `sus` raises on measures past 999 instead of losing them (unless `allow_measure_extensions`); `pjsk` and `untitled_sekai` delete fake and damage notes instead of writing them as real ones, and `pjsk` replaces extended eases and guide colours like the other exporters; MMWS keeps trace taps and fever at tick 0, `.mmws` keeps yellow guides, and the lane-extension header counts slides and guides; `next_sekai` raises on a note in a missing timescale group; an unknown ease or guide colour raises a `ValueError` naming it.
- `transforms.delete_fake_notes` turns a fake relay point inside a real slide into a hidden one (it kept them before), and `strip_extended_lanes` keeps a slide's fake flag.
- `Score` methods that changed the score (`shift`, `cut`, `delete_fake_notes`, `strip_extended_lanes`, ...) are now functions in `rg_converters.transforms` that return a new `Score`: `score.shift()` becomes `score = transforms.shift(score)`. `cut` returns `(score, combo_before, (start_tick, end_tick))`.
- `convert_holodori_events` takes and returns a `Score` (`transforms.convert_holodori_events`).
- Every `export(...)` returns the `Score` it wrote and no longer modifies its input (some exporters used to delete fake notes or add a BPM to it in place).
- A slide may hold a single start point and no end. Formats that need an end get an unjudged one a 480th of a beat after it (`transforms.close_open_slides`).
- `detect(...)` returns a list of the format ids the data could be, most likely first (`[]` when none): `sus`, `yume_sus`, `holodori_sus`, `usc`, `mmws`, `ccmmws`, `unchmmws`, `pjsk`, `bdon`, `next_sekai`, `chart_cyanvas`, `untitled_sekai`. More than one means the content can't tell them apart (e.g. `['sus', 'yume_sus']`).
- The PJSK `sus` loader keeps a directional with no note under it, as the game does: a flick note, or a tap when it only sets an ease.
- USC export deletes fake notes (USC has none; they used to be written as real notes), and writes `critical`/`trace` as `false` on singles that don't set them, so the file loads back.

## C library and Node.js
The core is also a C library for any language with a C FFI (C#/Unity, Rust, Go, ...): [`capi/include/rg_converters.h`](capi/include/rg_converters.h) documents the API, the options of each format and the JSON model a score is read and edited through. Every [GitHub release](https://github.com/UntitledCharts/sonolus-level-converters/releases) has it prebuilt for Windows x64, Linux x64/arm64 and macOS (universal): `rg_converters-<tag>-<platform>.zip`/`.tar.gz`, each with the library and `include/rg_converters.h`. To build it yourself:
```sh
cmake -S . -B build -DRG_BUILD_PYTHON=OFF -DRG_BUILD_CAPI=ON
cmake --build build --config Release && ctest --test-dir build -C Release
```

For Node.js, install the package attached to a release (it isn't on npm; the prebuilt libraries are included):
```sh
npm install https://github.com/UntitledCharts/sonolus-level-converters/releases/download/<tag>/rg-converters-<version>.tgz
```
```js
const fs = require("node:fs");
const { load, exportScore, transforms, Score } = require("rg-converters");

const score = load("sus", fs.readFileSync("chart.sus"));     // or formats.sus.load(...)
const { data, score: written } = exportScore("yume_sus", transforms.shift(score));

const model = score.toJSON();                                // the whole score as plain objects
model.metadata.title = "New title";
const edited = Score.fromJSON(model);
```
Both use the same format ids as `detect`; option names are camelCase in JS (`{ skipShift: true }`).

# Supported Files
### Fully Supported
Any file type listed here supports conversions between and from.
- `.usc` the old Sonolus standard (__DEPRECATED FORMAT__)
- `.sus` - PJSK-based sus format (with some extensions)
- `.sus` - Yume's sus format (`yume_sus`). Loads the way the game does (standalone flicks, no skill/fever, a single `#TIL00`); exporting strips what Yume can't play (extended lanes, skill/fever, volume); layers need `strip_layers=True` and measures past 999 can't be written.
- `.mmws/.ccmmws/.unchmmws` - MMW and popular fork (chart editors) file formats
- `.pjsk/.json/.json.gz` - PJSK internal score maker class json
- `next_sekai LevelData` (`.nsld/.json.gz`) - Sonolus' Next SEKAI (and Next RUSH) engine LevelData. Loading can only load things we support (extended lanes, guide colors, fades, fake notes, timescale groups, ...); anything else raises `UnsupportedFeatureError` unless `ImportOptions(error_on_unsupported_feature=False)` (such as Dynamic Stages).
- `chart_cyanvas LevelData` - Sonolus' Chart Cyanvas engine LevelData (__DEPRECATED FORMAT__)

NOTE: many file types use json internally. We include a detection function that can be used if necessary.

### Loading-Only Support
- `.sus` - hololive Dream's unique .sus format
- `.bdon/.json/.json.gz` - BDON (BanG Dream! Our Notes) chart json

### Exporting-Only Support
- `untitled_sekai LevelData` - Sonolus' Untitled SEKAI engine LevelData (__DEPRECATED FORMAT__)

# Credit
[susc-converter](https://github.com/Kyonkrnk/susc-converter/) for the base

[sonolus-pjsekai-js](https://github.com/hyeon2006/sonolus-pjsekai-js/blob/main/lib/src/usc/revert.ts) for the uscToUsc converter

[@hyeon2006](https://github.com/hyeon2006) for the help

[@YarNix](https://github.com/YarNix) for the help and MMWS loaders/exporters

[@qwewqa](https://github.com/qwewqa) for the Next Sekai exporter
