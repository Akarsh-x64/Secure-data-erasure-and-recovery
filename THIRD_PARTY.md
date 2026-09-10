# Third-party components

## The Sleuth Kit

This project bundles The Sleuth Kit (TSK) `4.15.0`, pinned to the stable
release tag `sleuthkit-4.15.0`. The source is under `third_party/sleuthkit/`.
Recovery builds the core TSK image, volume-system, and filesystem sources as a
static `TSK::tsk` CMake target. Optional AFF, EWF, VHD, VMDK, VSLVM, BFIO,
SQLite, and crypto backends are disabled because the Recovery integration uses
`TskImageBridge` with its own read-only image callbacks and does not require
those formats.

TSK includes code under multiple licenses. The applicable notices are retained
in `third_party/sleuthkit/licenses/`, including the Apache, BSD, Common Public
License, IBM, MIT, and GNU notices distributed by TSK.

## Build

The bundled TSK build requires only a C/C++17 toolchain and CMake 3.15 or
newer. Configure and build with:

```sh
cmake -S . -B build
cmake --build build
```

Use `-DRECOVERY_ENABLE_TSK=OFF` only to intentionally build the existing
no-TSK behavior. Linux, Windows/MSVC, and macOS/AppleClang use the same source
tree and CMake targets; only Linux was exercised in this environment.

The application still has runtime/system dependencies outside TSK, including
the PhotoRec executable used by carving and the normal platform C/C++ runtime.