# rg-converters
Convert between different mobile rhythm game file formats such as Sonolus' common LevelData types, Project Sekai's SUS and score maker JSON, hololive Dream's SUS, BanG Dream! Our Notes' JSON, and some chart editor exclusive formats. Can also be used to easily read and/or write chart data programmatically to a common Score class.

Written with a C++ core.

**Looking for the old `sonolus-level-converters`? You can install it from `https://github.com/UntitledCharts/rg_converter/tree/100e0ac4e5d2a895089b565f38800d1cc6a0b28e`.** It is no longer supported.

Requires `Python >= 3.10`

## Installation
`pip install rg-converters`

# Supported Files
### Fully Supported
Any file type listed here supports conversions between and from.
- `.usc` the old Sonolus standard (__DEPRECATED FORMAT__)
- `.sus` - PJSK-based sus format (with some extensions)
- `.mmws/.ccmmws/.unchmmws` - MMW and popular fork (chart editors) file formats
- `.pjsk/.json/.json.gz` - PJSK internal score maker class json
- `next_sekai LevelData` (`.nsld/.json.gz`) - Sonolus' Next SEKAI (and Next RUSH) engine LevelData. Loading covers what the exporter writes (extended lanes, guide colors, fades and alpha keyframes, fake notes, timescale groups, ...); anything else raises `UnsupportedFeatureError` unless `error_on_unsupported_feature=False`. A slide or guide changing kind part way (color, critical, fake) becomes a new line from that point, and attached anchors are dropped unless they hold an alpha keyframe. With `attempt_attached_note_convert=True` (the default) a judged note riding a slide or guide becomes a note of its own, the line stopping a 480th of a beat before it and starting again a 480th after (such charts don't round-trip).
- Guide alpha: `GuidePoint.alpha` is an optional keyframe (0-1); a guide's alpha runs linearly in time between its keyframes. A guide without keyframes fades by `Guide.fade` (`in` / `out` / `none`), which formats without alpha (SUS, USC) use.

NOTE: many file types use json internally. We include a detection function that can be used if necessary.

### Loading-Only Support
- `.sus` - hololive Dream's unique .sus format
- `.bdon/.json/.json.gz` - BDON (BanG Dream! Our Notes) chart json

### Exporting-Only Support
- `untitled_sekai LevelData` - Sonolus' Untitled SEKAI engine LevelData (__DEPRECATED FORMAT__)
- `chart_cyanvas LevelData` - Sonolus' Chart Cyanvas engine LevelData (__DEPRECATED FORMAT__)

# Credit
[susc-converter](https://github.com/Kyonkrnk/susc-converter/) for the base

[sonolus-pjsekai-js](https://github.com/hyeon2006/sonolus-pjsekai-js/blob/main/lib/src/usc/revert.ts) for the uscToUsc converter

[@hyeon2006](https://github.com/hyeon2006) for the help

[@YarNix](https://github.com/YarNix) for the help and MMWS loaders/exporters

[@qwewqa](https://github.com/qwewqa) for the Next Sekai exporter