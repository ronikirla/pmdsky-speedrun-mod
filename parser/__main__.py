"""Module entry point: ``python -m parser SAV``."""

import sys

from .cli import main

if __name__ == "__main__":
    sys.exit(main())
