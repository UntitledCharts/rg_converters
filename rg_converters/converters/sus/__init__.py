from ..._rg_converters import sus as _sus
from ..._pathio import write_text

detect = _sus.detect


def load(fp):
    return _sus.loads(fp.read())


def export(path, score, allow_layers: bool = False, allow_extended_lanes: bool = False,
           delete_damage: bool = True, keep_note_speed_ratios: bool = False,
           measure_extensions: bool = False, skip_shift: bool = False):
    text = _sus.dumps(score, allow_layers, allow_extended_lanes, delete_damage,
                      keep_note_speed_ratios, measure_extensions, skip_shift)
    write_text(path, text)
