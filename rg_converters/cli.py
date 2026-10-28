import sys
from typing import Optional

__all__ = ["main"]


def main(argv: Optional[list[str]] = None) -> None:
    try:
        from ._cli import run
    except ModuleNotFoundError as missing:
        if missing.name not in ("typer", "rich"):
            raise
        print("The rg-converters command line needs its extras: pip install rg-converters[cli]", file=sys.stderr)
        raise SystemExit(1) from None
    raise SystemExit(run(sys.argv[1:] if argv is None else argv))


if __name__ == "__main__":
    main()
