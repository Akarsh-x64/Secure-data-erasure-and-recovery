#pragma once

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "NTFS_Structures.h"
#include <vector>
#include <string>
#include <utility>

namespace Erasure {
namespace FileSystems {

/**
 * @brief High-performance, forensic-grade Secure Deletion Driver for the NTFS Filesystem.
 *
 * Adheres strictly to the project's 3-Layer Decoupled Architecture:
 *   [Layer 1: OS / IStorageDevice] -> [Layer 2: Hardware / IHardwareController] -> [Layer 3: Filesystem / NtfsDriver]
 *
 * Features:
 *   1. Dynamic geometry computation from VBR without hardcoded assumptions.
 *   2. Master File Table ($MFT) navigation across multi-fragment extent runlists.
 *   3. Dual data model support: Resident file payloads (< ~700B) & Non-resident Runlists.
 *   4. Directory B-tree traversal supporting both $INDEX_ROOT and $INDEX_ALLOCATION ("INDX" blocks).
 *   5. Recursive folder erasure eradicating all nested files, subfolders, index blocks, and MFT records.
 *   6. Drive & Volume formatting: surgical WipeVolume and pristine FormatDrive writing fresh VBR/MFT tables.
 *   7. Full xxd-compatible hex dump verification before and after deletion with annotated byte explanations.
 *   8. Zero direct Win32/POSIX syscalls: all I/O is strictly routed through IHardwareController.
 */
class NtfsDriver : public Core::IFileSystemDriver {
private:
    // =========================================================================
    // Core Dependencies & Cached Filesystem Geometry
    // =========================================================================
    Core::IHardwareController* m_hardware;

    NTFS::NtfsBootSector m_vbr;
    uint32_t m_bytesPerSector;
    uint32_t m_sectorsPerCluster;
    uint32_t m_bytesPerCluster;
    uint32_t m_mftRecordSize;
    uint32_t m_indexBlockSize;
    uint64_t m_totalSectors;

    // Dynamic extent mapping for $MFT itself (MFT can be fragmented across the disk)
    std::vector<NTFS::NtfsExtent> m_mftExtents;

    // =========================================================================
    // Internal Geometry & Low-Level Helpers
    // =========================================================================
    uint64_t SectorToByteOffset(uint64_t sector) const;

    bool ReadSectors(uint64_t startSector, uint32_t count, void* buffer) const;
    bool WriteSectors(uint64_t startSector, uint32_t count, const void* buffer);

    // Applies NTFS update sequence (fixup array) to protected 512-byte sector chunks
    bool ApplyFixup(uint8_t* buffer, size_t bufferSize) const;

    // Encodes NTFS update sequence (fixup array) before writing to raw sectors
    bool EncodeFixup(uint8_t* buffer, size_t bufferSize) const;

    // Decodes variable-length runlist bytes into a vector of physical (LCN, count) extents
    bool DecodeRunList(const uint8_t* runlist, size_t maxLen, std::vector<NTFS::NtfsExtent>& outExtents) const;

    // =========================================================================
    // MFT Record I/O and On-Disk Sanitization
    // =========================================================================
    uint64_t MftRecordToSector(uint64_t recordNum, uint32_t& outOffsetInSector) const;
    bool ReadMftRecord(uint64_t recordNum, std::vector<uint8_t>& outRecord) const;
    bool WriteMftRecord(uint64_t recordNum, const std::vector<uint8_t>& inRecord);
    bool WipeMftRecordOnDisk(uint64_t recordNum);

    // =========================================================================
    // Attribute Extractors
    // =========================================================================
    const uint8_t* FindAttribute(const std::vector<uint8_t>& recordBuffer, uint32_t attrType, const std::string& name = "") const;
    bool GetFileAllocatedExtents(const std::vector<uint8_t>& recordBuffer,
                                std::vector<NTFS::NtfsExtent>& outExtents,
                                bool& outIsResident,
                                std::vector<uint8_t>& outResidentData,
                                uint64_t& outFileSize) const;

    // =========================================================================
    // Volume Allocation Bitmap ($Bitmap - Record 6) & $MFT Bitmap (Record 0)
    // =========================================================================
    bool ClearClusterBitmapBit(uint64_t lcn);
    bool ClearMftRecordBitmapBit(uint64_t recordNum);
    bool ReadClusterBitmapByte(uint64_t lcn, uint8_t& outByte, uint64_t& outSector, uint32_t& outOffsetInSector, uint8_t& outBitMask) const;

    // =========================================================================
    // Directory Traversal & Index B-Tree Parsing
    // =========================================================================
    std::vector<std::string> TokenizePath(const std::string& path) const;
    static std::string Utf16ToUtf8(const char16_t* utf16Str, size_t length);
    static bool EqualsIgnoreCase(const std::string& a, const std::string& b);

    bool SearchIndexBlock(const uint8_t* blockData, size_t blockSize,
                          const std::string& targetName,
                          uint64_t& outChildRef, bool& outIsDir,
                          uint64_t& outChildVcn, size_t& outEntryOffset,
                          uint32_t& outEntryLen) const;

    bool FindEntryInDirectory(uint64_t dirRecordNum, const std::string& targetName,
                              uint64_t& outChildRecordNum, bool& outIsDir,
                              uint64_t& outIndexSector, uint32_t& outIndexOffset,
                              uint32_t& outEntrySize) const;

    bool ScrubDirectoryEntry(uint64_t dirRecordNum, const std::string& targetName);

    bool ListDirectoryContents(uint64_t dirRecordNum,
                               std::vector<std::pair<std::string, uint64_t>>& outEntries,
                               std::vector<bool>& outIsDir) const;

    // Recursive directory eraser
    bool EraseDirectoryRecursive(uint64_t dirRecordNum);

public:
    explicit NtfsDriver(Core::IHardwareController* hardware);
    ~NtfsDriver() override = default;

    // Core IFileSystemDriver overrides
    bool Mount() override;
    bool EraseFile(const std::string& relativePath) override;
    bool WipeVolume() override;

    // Extended Deletion & Formatting Operations
    bool EraseDirectory(const std::string& relativePath);
    bool FormatDrive(bool fullDriveSanitize = false);

    // =========================================================================
    // Forensic Verification & Byte Explanation Suite
    // =========================================================================
    static void PrintHexDump(const void* data, size_t size, uint64_t basePhysicalOffset, const std::string& label);
    void ExplainMftRecordBytes(const uint8_t* recordData, size_t size, uint64_t baseOffset) const;
    void ExplainDirectoryEntryBytes(const uint8_t* entryData, size_t size, uint64_t baseOffset) const;
    void ExplainDataSectorBytes(const uint8_t* data, size_t size, uint64_t baseOffset) const;
    void ExplainBitmapBytes(uint8_t byteVal, uint8_t mask, uint64_t cluster, uint64_t baseOffset) const;

    bool LocateTargetLocations(const std::string& relativePath, NTFS::TargetLocations& outLocs) const;

    // High-level verification runners (intakes file, folder, or disk, performs before/after dumps and byte breakdown)
    bool VerifyAndErase(const std::string& targetPath);
    bool VerifyAndFormatDrive(bool fullDriveSanitize = false);

    // Diagnostics
    void PrintBootInfo() const;
    const NTFS::NtfsBootSector& GetVBR() const { return m_vbr; }
    uint32_t GetBytesPerSector() const { return m_bytesPerSector; }
    uint32_t GetBytesPerCluster() const { return m_bytesPerCluster; }
    uint32_t GetMftRecordSize() const { return m_mftRecordSize; }
    uint64_t ClusterToSector(uint64_t lcn) const;
};

} // namespace FileSystems
} // namespace Erasure
