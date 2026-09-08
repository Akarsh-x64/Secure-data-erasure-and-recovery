#pragma once

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "FAT32_Structures.h"
#include <vector>
#include <string>
#include <utility>

namespace Erasure {
namespace FileSystems {

/**
 * @brief High-performance, forensic-grade Secure Deletion Driver for the FAT32 Filesystem.
 *
 * Adheres strictly to the project's 3-Layer Decoupled Architecture:
 *   [Layer 1: OS / IStorageDevice] -> [Layer 2: Hardware / IHardwareController] -> [Layer 3: Filesystem / Fat32Driver]
 *
 * Key Capabilities:
 *   1. Dynamic geometry extraction from VBR & Extended BPB (Sector 0) without hardcoded offsets.
 *   2. Dual FAT table synchronization: updates primary FAT1 and mirror FAT2 (preserving upper 4 bits).
 *   3. 28-bit cluster chain traversal and physical 3-Pass DoD 5220.22-M sector sanitization.
 *   4. SFN (8.3) and VFAT LFN directory entry eradication stamping 0xE5 and zeroing all metadata.
 *   5. Recursive folder erasure destroying nested files, subfolders, and directory cluster chains.
 *   6. Surgical WipeVolume and clean FormatDrive rewriting pristine VBR, FSInfo, FATs, and Root Dir.
 */
class Fat32Driver : public Core::IFileSystemDriver {
private:
    // =========================================================================
    // Core Dependencies & Cached Geometry
    // =========================================================================
    Core::IHardwareController* m_hardware;

    FAT32::Fat32BootSector m_bpb;
    FAT32::Fat32FSInfo     m_fsInfo;

    uint32_t m_bytesPerSector;
    uint32_t m_sectorsPerCluster;
    uint32_t m_bytesPerCluster;
    uint32_t m_firstDataSector;
    uint32_t m_totalClusters;
    uint32_t m_fatStartSector;
    uint32_t m_fatSizeSectors;
    uint8_t  m_numFATs;
    uint32_t m_rootCluster;
    uint16_t m_fsInfoSector;
    bool     m_isMounted;

    // =========================================================================
    // Geometry & Low-Level Helpers
    // =========================================================================
    uint64_t ClusterToSector(uint32_t cluster) const;
    bool ReadSectors(uint64_t startSector, uint32_t count, void* buffer) const;
    bool WriteSectors(uint64_t startSector, uint32_t count, const void* buffer);

    // =========================================================================
    // FAT Table Management
    // =========================================================================
    uint32_t ReadFatEntry(uint32_t cluster) const;
    bool WriteFatEntry(uint32_t cluster, uint32_t value);
    std::vector<uint32_t> GetClusterChain(uint32_t startCluster) const;
    bool FreeClusterChain(const std::vector<uint32_t>& chain);

    // =========================================================================
    // FSInfo Management
    // =========================================================================
    bool ReadFSInfo();
    bool WriteFSInfo();
    bool UpdateFSInfo(int32_t freeClusterDelta, uint32_t nextFreeHint);

    // =========================================================================
    // Directory Traversal & SFN/LFN Helpers
    // =========================================================================
    std::vector<std::string> TokenizePath(const std::string& path) const;
    static uint8_t ComputeSfnChecksum(const uint8_t* pFcbName);
    static std::string SfnToString(const uint8_t* name11);
    static std::string Utf16ToUtf8(const char16_t* utf16Str, size_t length);
    static bool EqualsIgnoreCase(const std::string& a, const std::string& b);

    struct DirSearchResult {
        bool found = false;
        bool isDirectory = false;
        uint32_t firstCluster = 0;
        uint32_t fileSize = 0;
        uint32_t dirCluster = 0;           // Cluster containing the SFN entry
        size_t   entryOffsetInCluster = 0; // Byte offset within dirCluster
        size_t   lfnCount = 0;             // Number of preceding LFN entries in chain
        std::string fileName;
    };

    DirSearchResult FindEntryInDirectory(uint32_t dirCluster, const std::string& targetName) const;
    bool SanitizeDirectoryEntry(const DirSearchResult& result);
    bool ListDirectoryContents(uint32_t dirCluster,
                               std::vector<std::pair<std::string, uint32_t>>& outEntries,
                               std::vector<bool>& outIsDir) const;

    // Recursive directory eradication
    bool EraseDirectoryRecursive(uint32_t dirCluster);

public:
    explicit Fat32Driver(Core::IHardwareController* hardware);
    ~Fat32Driver() override = default;

    // Core IFileSystemDriver overrides
    bool Mount() override;
    bool EraseFile(const std::string& relativePath) override;
    bool WipeVolume() override;

    // Extended Deletion & Formatting Operations
    bool EraseDirectory(const std::string& relativePath);
    bool FormatDrive(bool fullDriveSanitize = false);

    // Diagnostics & Inspection
    void PrintBootInfo() const;
    const FAT32::Fat32BootSector& GetBPB() const { return m_bpb; }
    const FAT32::Fat32FSInfo& GetFSInfo() const { return m_fsInfo; }
    uint32_t GetBytesPerSector() const { return m_bytesPerSector; }
    uint32_t GetBytesPerCluster() const { return m_bytesPerCluster; }
    uint32_t GetFirstDataSector() const { return m_firstDataSector; }
    uint32_t GetRootCluster() const { return m_rootCluster; }
    uint32_t GetTotalClusters() const { return m_totalClusters; }
};

} // namespace FileSystems
} // namespace Erasure
