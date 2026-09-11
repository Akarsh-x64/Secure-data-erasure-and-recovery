#!/usr/bin/env python3
"""Small smoke test for the Recovery Python extension."""

import argparse
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
EXTENSION_DIR = REPO_ROOT / "build" / "recovery-python" / "Recovery"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image_path", type=Path)
    parser.add_argument("output_dir", type=Path)
    arguments = parser.parse_args()

    if not list(EXTENSION_DIR.glob("recovery*.so")):
        raise SystemExit(f"No recovery extension found in {EXTENSION_DIR}")
    sys.path.insert(0, str(EXTENSION_DIR))

    import recovery

    metadata_output = arguments.output_dir / "metadata"
    carving_output = arguments.output_dir / "carving"

    metadata_result = recovery.recover_metadata(
        str(arguments.image_path), str(metadata_output)
    )
    carving_result = recovery.recover_carving(
        str(arguments.image_path), str(carving_output)
    )

    print(f"recover_metadata: {metadata_result}")
    print(f"recover_carving: {carving_result}")
    return 0 if metadata_result and carving_result else 1


if __name__ == "__main__":
    raise SystemExit(main())