#!/usr/bin/env bash
# ==============================================================================
# Linux Native Build Script for Multi-Filesystem Secure Erasure & Verification
# Compiles main_linux with GCC / Clang (C++17)
# ==============================================================================

set -e

# Change to repository root directory
cd "$(dirname "$0")/.."

echo "======================================================================"
echo " Building Multi-Filesystem Erasure & Verification Suite (Linux Native)"
echo "======================================================================"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pthread"

SOURCES=(
    "Tests/main_linux.cpp"
    "Erasure/OS/Linux/LinuxStorageDevice.cpp"
    "Erasure/Hardware/Magnetic/HDDController.cpp"
    "Erasure/File Systems/NTFS/NTFS.cpp"
    "Erasure/File Systems/XFS/XFS.cpp"
    "Erasure/File Systems/ext4/ext4.cpp"
    "Erasure/File Systems/exFAT/exFAT.cpp"
    "Erasure/File Systems/FAT32/FAT32.cpp"
    "Erasure/File Systems/ext2/ext2.cpp"
    "Erasure/File Systems/ext3/ext3.cpp"
    "Erasure/Verification/StatisticalTests.cpp"
    "Erasure/Verification/SignatureCarver.cpp"
    "Erasure/Verification/VerificationReport.cpp"
    "Erasure/Verification/VerificationEngine.cpp"
)

OUTPUT="Tests/main_linux"

echo "Compiler: ${CXX}"
echo "Flags:    ${CXXFLAGS}"
echo "Output:   ${OUTPUT}"
echo "Compiling..."

"${CXX}" ${CXXFLAGS} "${SOURCES[@]}" -o "${OUTPUT}"

echo "----------------------------------------------------------------------"
echo "[BUILD SUCCESS] Binary generated: ${OUTPUT}"
echo "Usage:"
echo "  Run automated synthetic test suite:"
echo "    ./${OUTPUT} --test"
echo "  Run interactive live mode on physical/loop block device (requires root):"
echo "    sudo ./${OUTPUT}"
echo "======================================================================"
