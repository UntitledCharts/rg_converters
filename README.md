# rg-converters
Convert between different mobile rhythm game file formats such as Sonolus' common LevelData types, Project Sekai's SUS and score maker JSON, hololive Dream's SUS, BanG Dream! Our Notes' JSON, and some chart editor exclusive formats. Can also be used to easily read and/or write chart data programmatically to a common Score class.

Written with a C++ core.

**Looking for the old `sonolus-level-converters`? You can install it from `https://github.com/UntitledCharts/rg_converter/tree/100e0ac4e5d2a895089b565f38800d1cc6a0b28e`.** It is no longer supported.

Requires `Python >= 3.10`. Supports free-threaded Python (`3.14t` and newer). Objects can be shared between threads.

## Installation
`pip install rg-converters`

To use the cli, install with `pip install rg-converters[cli]`

Bundled node libraries are attached to releases for a JS/TS library.

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

## Command line
```sh
rg-converters convert chart.sus chart.usc --to usc     # asks for anything left out
rg-converters detect chart.sus
rg-converters info chart.sus
```

## C library
The core is also a C library for any language with a C FFI (C#/Unity, Rust, Go, ...): [`capi/include/rg_converters.h`](capi/include/rg_converters.h) documents the API, the options of each format and the JSON model a score is read and edited through. Every [GitHub release](https://github.com/UntitledCharts/sonolus-level-converters/releases) has it prebuilt for Windows x64, Linux x64/arm64 and macOS (universal): `rg_converters-<tag>-<platform>.zip`/`.tar.gz`, each with the library and `include/rg_converters.h`. To build it yourself:
```sh
cmake -S . -B build -DRG_BUILD_PYTHON=OFF -DRG_BUILD_CAPI=ON
cmake --build build --config Release && ctest --test-dir build -C Release
```

## Node.js
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

Option names are camelCase in JS (eg. `{ skipShift: true }`).

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
