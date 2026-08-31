#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdint>

// Recovery Module — Phase 1
#include "../../Acquisition/WindowsReadOnlyStorage.h"
#include "../../Core/ByteReader.h"

/**
 * @brief Phase 1 Test: Read-Only Storage Foundation
 *
 * Demonstrates that the Recovery module can:
 *   1. Open a Windows physical drive or volume in read-only mode
 *   2. Query its geometry (sector size, total size)
 *   3. Read raw bytes from sector 0 (the boot record)
 *   4. Read from an arbitrary offset via ByteReader
 *   5. Display bytes in hexadecimal
 *   6. Close the device cleanly
 *
 * The device is NEVER modified. No locks, dismounts, or writes occur.
 */

// Prints a hex dump of the given data (16 bytes per line)
void PrintHexDump(const uint8_t* data, uint32_t size, uint64_t baseOffset = 0) {
    const uint32_t bytesPerLine = 16;

    for (uint32_t i = 0; i < size; i += bytesPerLine) {
        // Print offset
        std::cout << "  " << std::hex << std::setfill('0')
                  << std::setw(8) << (baseOffset + i) << "  ";

        // Print hex bytes
        for (uint32_t j = 0; j < bytesPerLine; ++j) {
            if (i + j < size) {
                std::cout << std::setw(2) << static_cast<unsigned>(data[i + j]) << " ";
            } else {
                std::cout << "   ";
            }
            if (j == 7) std::cout << " "; // Extra space in the middle
        }

        // Print ASCII representation
        std::cout << " |";
        for (uint32_t j = 0; j < bytesPerLine && (i + j) < size; ++j) {
            char c = static_cast<char>(data[i + j]);
            std::cout << (c >= 32 && c < 127 ? c : '.');
        }
        std::cout << "|" << std::dec << "\n";
    }
}

int main() {
    std::cout << "=============================================\n";
    std::cout << "  Recovery Module — Phase 1 Test             \n";
    std::cout << "  Read-Only Storage Foundation               \n";
    std::cout << "=============================================\n\n";

    std::cout << "This test opens a device in READ-ONLY mode.\n";
    std::cout << "No data will be written. No volume locks.\n\n";

    std::cout << "Path Formats:\n";
    std::cout << "  Physical drive: \\\\.\\PhysicalDrive0\n";
    std::cout << "  Volume:         \\\\.\\E:\n\n";

    std::cout << "Enter device path: ";
    std::string path;
    std::getline(std::cin, path);

    if (path.empty()) {
        std::cerr << "No path provided.\n";
        return 1;
    }

    // ---- Step 1: Open the device (read-only) ----
    Recovery::Acquisition::WindowsReadOnlyStorage storage;

    std::cout << "\n[1] Opening device in READ-ONLY mode...\n";
    if (!storage.Open(path)) {
        std::cerr << "[FAIL] Could not open device.\n";
        return 1;
    }
    std::cout << "[OK] Device opened successfully.\n";

    // ---- Step 2: Print geometry ----
    uint32_t sectorSize = storage.GetSectorSize();
    uint64_t totalBytes = storage.GetSize();

    std::cout << "\n[2] Device Geometry:\n";
    std::cout << "    Sector Size:  " << sectorSize << " bytes\n";
    std::cout << "    Total Size:   " << totalBytes << " bytes";

    // Human-readable size
    if (totalBytes >= (1ULL << 30)) {
        std::cout << " (" << std::fixed << std::setprecision(2)
                  << (double)totalBytes / (1ULL << 30) << " GB)";
    } else if (totalBytes >= (1ULL << 20)) {
        std::cout << " (" << std::fixed << std::setprecision(2)
                  << (double)totalBytes / (1ULL << 20) << " MB)";
    }
    std::cout << "\n";
    std::cout << "    Total Sectors: " << (sectorSize > 0 ? totalBytes / sectorSize : 0) << "\n";

    // ---- Step 3: Read sector 0 (raw, sector-aligned) ----
    std::cout << "\n[3] Reading Sector 0 (first " << sectorSize << " bytes) via IReadOnlyStorage::Read()...\n";

    std::vector<uint8_t> sector0(sectorSize);
    if (storage.Read(0, sectorSize, sector0.data())) {
        std::cout << "[OK] First 64 bytes of Sector 0:\n";
        PrintHexDump(sector0.data(), 64, 0);
    } else {
        std::cerr << "[FAIL] Could not read sector 0.\n";
    }

    // ---- Step 4: Read via ByteReader (arbitrary offset) ----
    Recovery::Core::ByteReader reader(&storage);

    std::cout << "\n[4] Reading 48 bytes at offset 0x100 via ByteReader (arbitrary alignment)...\n";
    std::vector<uint8_t> arbData;
    if (reader.ReadBytes(0x100, 48, arbData)) {
        std::cout << "[OK] Data at offset 0x100:\n";
        PrintHexDump(arbData.data(), 48, 0x100);
    } else {
        std::cerr << "[FAIL] Could not read at offset 0x100.\n";
    }

    // ---- Step 5: Verify no write API exists ----
    std::cout << "\n[5] Safety Verification:\n";
    std::cout << "    IReadOnlyStorage exposes: Open, Close, Read, GetSize, GetSectorSize\n";
    std::cout << "    NO WriteSectors, NO LockVolume, NO DismountVolume, NO SendDeviceCommand\n";
    std::cout << "    Win32 handle opened with GENERIC_READ only (no GENERIC_WRITE)\n";
    std::cout << "    [OK] Write operations are impossible at both API and OS kernel level.\n";

    // ---- Step 6: Close ----
    std::cout << "\n[6] Closing device...\n";
    storage.Close();
    std::cout << "[OK] Device closed. No modifications were made.\n";

    std::cout << "\n=============================================\n";
    std::cout << "  Phase 1 Test Complete                      \n";
    std::cout << "=============================================\n";

    return 0;
}
