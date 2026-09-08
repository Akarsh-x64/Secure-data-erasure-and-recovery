# ==============================================================================
# Multi-Filesystem Secure Erasure & Recovery Engine
# Cross-Platform Makefile (Linux & Windows MinGW/GCC)
# ==============================================================================

CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread

COMMON_SRCS = \
	Erasure/Hardware/Magnetic/HDDController.cpp \
	"Erasure/File Systems/NTFS/NTFS.cpp" \
	"Erasure/File Systems/XFS/XFS.cpp" \
	"Erasure/File Systems/ext4/ext4.cpp" \
	"Erasure/File Systems/exFAT/exFAT.cpp" \
	"Erasure/File Systems/FAT32/FAT32.cpp" \
	Erasure/Verification/StatisticalTests.cpp \
	Erasure/Verification/SignatureCarver.cpp \
	Erasure/Verification/VerificationReport.cpp \
	Erasure/Verification/VerificationEngine.cpp

LINUX_SRCS = \
	Tests/main_linux.cpp \
	Erasure/OS/Linux/LinuxStorageDevice.cpp \
	$(COMMON_SRCS)

WIN_SRCS = \
	Tests/main.cpp \
	Erasure/OS/Windows/WindowsStorageDevice.cpp \
	$(COMMON_SRCS)

.PHONY: all linux windows clean

all: linux

# Linux Native Build
linux: Tests/main_linux

Tests/main_linux: $(LINUX_SRCS)
	@echo "[CXX] Compiling Linux Native Suite ($@)..."
	$(CXX) $(CXXFLAGS) $(LINUX_SRCS) -o Tests/main_linux
	@echo "[SUCCESS] Built Tests/main_linux successfully."

# Windows MinGW Build
windows: Tests/main.exe

Tests/main.exe: $(WIN_SRCS)
	@echo "[CXX] Compiling Windows Native Suite ($@)..."
	$(CXX) $(CXXFLAGS) $(WIN_SRCS) -o Tests/main.exe
	@echo "[SUCCESS] Built Tests/main.exe successfully."

clean:
	rm -f Tests/main_linux Tests/main.exe
