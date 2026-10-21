from ..._rg_converters import leveldata as _lvd


def detect(data, *, skip_gzip: bool = False, skip_json: bool = False):
    return _lvd.detect(data, skip_gzip, skip_json)
