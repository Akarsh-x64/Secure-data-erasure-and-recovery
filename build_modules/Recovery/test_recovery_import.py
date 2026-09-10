#!/usr/bin/env python3
"""Import smoke test for the compiled Recovery extension."""

import argparse
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("extension_dir", type=Path)
    arguments = parser.parse_args()

    extension_dir = arguments.extension_dir.resolve()
    extensions = list(extension_dir.glob("recovery*.so"))
    if not extensions:
        raise SystemExit(f"No recovery extension found in {extension_dir}")

    sys.path.insert(0, str(extension_dir))
    import recovery

    assert callable(recovery.recover_metadata)
    assert callable(recovery.recover_carving)
    print(f"Imported: {recovery.__file__}")
    print("Recovery binding import: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())