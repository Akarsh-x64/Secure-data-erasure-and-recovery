#pragma once

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "exFAT_Structures.h"
#include <vector>
#include <string>

namespace Erasure {
namespace FileSystems {

class ExFatDriver : public Core::IFileSystemDriver {
private:
    Core::IHardwareController* m_hardware;
    
    // Cached map info
    ExFatBootSector m_vbr;
    uint32_t m_bytesPerSector;
    uint32_t m_sectorsPerCluster;

    // Bitmap cache
    uint32_t m_bitmapFirstCluster;
    uint64_t m_bitmapDataLength;

    // Helpers to calculate physical offsets
    uint64_t ClusterToSector(uint32_t cluster) const;
    
    // Low level FAT and Bitmap manipulators
    uint32_t ReadFatEntry(uint32_t cluster) const;
    bool WriteFatEntry(uint32_t cluster, uint32_t value);
    bool ClearBitmapBit(uint32_t cluster);

    // Recursive Traversal Helpers
    std::vector<std::wstring> TokenizePath(const std::wstring& path) const;
    std::vector<uint32_t> GetClusterChain(uint32_t startCluster, uint64_t dataLength, bool noFatChain) const;
    
    struct SearchResult {
        bool found;
        bool isDirectory;
        uint32_t firstCluster;
        uint64_t dataLength;
        bool noFatChain;
        size_t entryIndex;      // Memory offset to the 0x85 entry so we can wipe it
    };

    SearchResult FindEntryInDirectory(const std::vector<uint32_t>& dirClusters, const std::wstring& targetName, std::vector<uint8_t>& outDirBuffer) const;

public:
    explicit ExFatDriver(Core::IHardwareController* hardware);
    ~ExFatDriver() override = default;

    bool Mount() override;
    bool EraseFile(const std::string& relativePath) override;
    bool WipeVolume() override;

    // Test function to verify VBR parsing
    void PrintVBRInfo() const;

    // =========================================================================
    // Checksum & Hash Algorithms (Microsoft exFAT Specification Compliant)
    // =========================================================================

    /**
     * @brief Computes 16-bit upcased file name hash for 0xC0 Stream Extension entry (Spec §6.3.5.1).
     * @param name UTF-16 file name.
     * @return 16-bit hash.
     */
    static uint16_t ComputeNameHash(const std::u16string& name);

    /**
     * @brief Computes 16-bit Directory Entry Set Checksum for 0x85 primary entry (Spec §6.3.3.1).
     * @param entrySet Pointer to continuous buffer of 32-byte entries in the set.
     * @param entryCount Number of 32-byte entries in the set (1 primary + N secondary).
     * @return 16-bit checksum.
     */
    static uint16_t ComputeEntrySetChecksum(const uint8_t* entrySet, size_t entryCount);

    /**
     * @brief Computes 32-bit Boot Region Checksum over Sectors 0..10 (Spec §3.1.9).
     * @param bootSectors Pointer to buffer holding Sectors 0..10.
     * @param byteCount Number of bytes (typically 11 * bytesPerSector).
     * @return 32-bit checksum.
     */
    static uint32_t ComputeBootChecksum(const uint8_t* bootSectors, size_t byteCount);

    /**
     * @brief Validates if the Sector 11 checksum matches the calculated checksum of Sectors 0..10.
     */
    static bool VerifyBootChecksum(const uint8_t* bootRegion, size_t sectorSize);

    /**
     * @brief Validates if the 0x85 setChecksum matches the calculated checksum of the entry set.
     */
    static bool VerifyEntrySetChecksum(const uint8_t* entrySet, size_t entryCount);
};

} // namespace FileSystems
} // namespace Erasure

