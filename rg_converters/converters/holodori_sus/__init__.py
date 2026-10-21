from ..._rg_converters import holodori_sus as _h


def loads(data, *, count_bezier: bool = False):
    score, bezier = _h.loads(data)
    return (score, bezier) if count_bezier else score


def load(fp, *, count_bezier: bool = False):
    return loads(fp.read(), count_bezier=count_bezier)
