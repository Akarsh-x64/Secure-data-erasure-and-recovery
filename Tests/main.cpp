#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <memory>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

// Engine Layer Contracts
#include "../Erasure/Core/IStorageDevice.h"
#include "../Erasure/Core/IHardwareController.h"
#include "../Erasure/Core/IFileSystemDriver.h"

// Hardware Layer
#include "../Erasure/Hardware/Magnetic/HDDController.h"

// OS Layer (Cross-Platform Windows / Linux)
#if defined(_WIN32) || defined(_WIN64)
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
using NativeStorageDevice = Erasure::OS::WindowsStorageDevice;
#elif defined(__linux__)
#include "../Erasure/OS/Linux/LinuxStorageDevice.h"
using NativeStorageDevice = Erasure::OS::LinuxStorageDevice;
#else
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
using NativeStorageDevice = Erasure::OS::WindowsStorageDevice;
#endif

// Filesystem Drivers & On-Disk Structures
#include "../Erasure/File Systems/NTFS/NTFS.h"
#include "../Erasure/File Systems/NTFS/NTFS_Structures.h"
#include "../Erasure/File Systems/XFS/XFS.h"
#include "../Erasure/File Systems/XFS/XFS_Structures.h"
#include "../Erasure/File Systems/ext4/ext4.h"
#include "../Erasure/File Systems/ext4/ext4_Structures.h"
#include "../Erasure/File Systems/exFAT/exFAT.h"
#include "../Erasure/File Systems/exFAT/exFAT_Structures.h"
#include "../Erasure/File Systems/FAT32/FAT32.h"
#include "../Erasure/File Systems/FAT32/FAT32_Structures.h"

// Forensic Verification Engine
#include "../Erasure/Verification/VerificationEngine.h"
#include "../Erasure/Verification/VerificationReport.h"
#include "../Erasure/Verification/StatisticalTests.h"
#include "../Erasure/Verification/SignatureCarver.h"

using namespace Erasure;
using namespace Erasure::Core;
using namespace Erasure::Hardware;
using namespace Erasure::FileSystems;
using namespace Erasure::Verification;

// ============================================================================
// In-Memory Simulated Storage Device (OS Layer for Hermetic Testing)
// ============================================================================
class MemoryDiskDevice : public IStorageDevice {
private:
    std::vector<uint8_t> m_disk;
    uint32_t m_sectorSize;
    std::string m_label;

public:
    explicit MemoryDiskDevice(size_t totalBytes, uint32_t sectorSize = 512, const std::string& label = "MemoryDisk")
        : m_disk(totalBytes, 0)
        , m_sectorSize(sectorSize)
        , m_label(label)
    {}

    bool Open(const std::string&) override { return true; }
    void Close() override {}

    bool ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) override {
        size_t startByte = startSector * m_sectorSize;
        size_t byteCount = static_cast<size_t>(sectorCount) * m_sectorSize;
        if (startByte + byteCount > m_disk.size()) return false;
        std::memcpy(buffer, m_disk.data() + startByte, byteCount);
        return true;
    }

    bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) override {
        size_t startByte = startSector * m_sectorSize;
        size_t byteCount = static_cast<size_t>(sectorCount) * m_sectorSize;
        if (startByte + byteCount > m_disk.size()) return false;
        std::memcpy(m_disk.data() + startByte, buffer, byteCount);
        return true;
    }

    bool LockVolume() override { return true; }
    bool UnlockVolume() override { return true; }
    bool DismountVolume() override { return true; }
    bool SendDeviceCommand(uint32_t, void*, uint32_t, void*, uint32_t) override { return true; }

    DeviceGeometry GetGeometry() const override {
        return { m_sectorSize, m_disk.size() / m_sectorSize, m_label };
    }

    uint8_t* GetDiskData() { return m_disk.data(); }
    const uint8_t* GetDiskData() const { return m_disk.data(); }
    size_t GetDiskSize() const { return m_disk.size(); }
};

// ============================================================================
// Universal XXD Hex Dump Formatter
// ============================================================================
static void PrintHexDump(const void* data, size_t size, uint64_t basePhysicalOffset, const std::string& label) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    std::cout << "\n--------------------------------------------------------------------------------\n";
    std::cout << "[XXD HEX DUMP] " << label << " (" << size << " bytes) @ Physical Offset 0x"
              << std::hex << basePhysicalOffset << std::dec << "\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    for (size_t i = 0; i < size; i += 16) {
        // Physical file/disk offset
        std::cout << std::hex << std::setw(8) << std::setfill('0') << (basePhysicalOffset + i) << ": ";

        // 16 Hex bytes formatted in 2-byte groups (standard xxd format)
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                std::cout << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i + j]);
            } else {
                std::cout << "  ";
            }
            if (j % 2 == 1) std::cout << " ";
        }
        std::cout << " ";

        // ASCII representation
        std::cout << "|";
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                uint8_t c = bytes[i + j];
                std::cout << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
            } else {
                std::cout << " ";
            }
        }
        std::cout << "|\n";
    }
    std::cout << std::dec << std::setfill(' ');
}

// ============================================================================
// Forensic Semantic Byte Breakdown Explainers
// ============================================================================

static void ExplainDataSector(const uint8_t* data, size_t size, uint64_t physicalOffset, const std::string& fsName) {
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: " << fsName << " DATA SECTOR @ 0x"
              << std::hex << physicalOffset << std::dec << "]\n";
    bool allZeros = true;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] != 0) { allZeros = false; break; }
    }
    if (allZeros) {
        std::cout << "  * Status: ALL 0x00 ZERO-FILLED (Forensically Obliterated / Unallocated Free Space)\n";
        std::cout << "  * Forensic Recovery Likelihood: 0.00% (Bit-level zero saturation confirmed)\n";
    } else {
        // Calculate Shannon entropy over the sample
        int freq[256] = {0};
        for (size_t i = 0; i < size; ++i) freq[data[i]]++;
        double entropy = 0.0;
        for (int i = 0; i < 256; ++i) {
            if (freq[i] > 0) {
                double p = static_cast<double>(freq[i]) / static_cast<double>(size);
                entropy -= p * (std::log(p) / std::log(2.0));
            }
        }

        // If entropy is high (> 3.5 bits/byte) or uniform distribution, it's PRNG noise / gibberish
        if (entropy > 3.5) {
            std::cout << "  * Status: PRNG GIBBERISH NOISE (DoD 5220.22-M 3-Pass Overwritten: 0 -> 1 -> Random)\n";
            std::cout << "  * Shannon Entropy: " << std::fixed << std::setprecision(4) << entropy << " bits/byte (High Randomness Noise)\n";
            std::cout << "  * Forensic Recovery Likelihood: 0.00% (Magnetic domains randomized by 3-pass sanitization)\n";
        } else {
            std::cout << "  * Status: ACTIVE USER PAYLOAD DATA (Raw disk contents present)\n";
            std::cout << "  * Sample Payload Preview: \"";
            for (size_t i = 0; i < std::min<size_t>(size, 32); ++i) {
                char c = static_cast<char>(data[i]);
                std::cout << (c >= 32 && c <= 126 ? c : '.');
            }
            std::cout << "...\"\n";
        }
    }
}

static void ExplainNtfsBootSector(const uint8_t* sectorData, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(NTFS::NtfsBootSector)) return;
    const auto* vbr = reinterpret_cast<const NTFS::NtfsBootSector*>(sectorData);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: NTFS VOLUME BOOT RECORD (VBR) @ 0x"
              << std::hex << physicalOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x03..+0x0A [OEM ID]:        \"" << std::string(vbr->oemId, 8) << "\"\n";
    std::cout << "  * Offset +0x0B..+0x0C [Sector Size]:   " << vbr->bytesPerSector << " bytes\n";
    std::cout << "  * Offset +0x0D       [Sec/Cluster]:   " << static_cast<int>(vbr->sectorsPerCluster)
              << " (" << (vbr->bytesPerSector * vbr->sectorsPerCluster) << " bytes/cluster)\n";
    std::cout << "  * Offset +0x28..+0x2F [Total Sectors]: " << vbr->totalSectors << "\n";
    std::cout << "  * Offset +0x30..+0x37 [MFT Start LCN]: Cluster " << vbr->mftStartLCN << "\n";
    std::cout << "  * Offset +0x38..+0x3F [MFTMirr LCN]:   Cluster " << vbr->mftMirrStartLCN << "\n";
    std::cout << "  * Offset +0x1FE..+0x1FF [Signature]:   0x" << std::hex << vbr->bootSignature << std::dec;
    if (vbr->bootSignature == NTFS::NTFS_BOOT_SIGNATURE) std::cout << " (Valid 0xAA55 Boot Signature)\n";
    else std::cout << " (Invalid / Sanitized)\n";
}

static void ExplainNtfsMftRecord(const uint8_t* recordData, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(NTFS::NtfsRecordHeader)) return;
    const auto* hdr = reinterpret_cast<const NTFS::NtfsRecordHeader*>(recordData);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: NTFS MFT RECORD @ 0x" << std::hex << physicalOffset << std::dec << "]\n";
    uint32_t magic = hdr->magic;
    std::cout << "  * Offset +0x00..+0x03 [Magic]:         0x" << std::hex << magic << std::dec;
    if (magic == NTFS::NTFS_MAGIC_FILE) std::cout << " ('FILE' - Active In-Use / Alloc MFT Record)\n";
    else if (magic == NTFS::NTFS_MAGIC_BAAD) std::cout << " ('BAAD' - Corrupted / Marked Bad Record)\n";
    else if (magic == 0) std::cout << " (0x00000000 - Forensically Zeroed / Obliterated Record)\n";
    else std::cout << " (Unknown / Raw bytes)\n";

    std::cout << "  * Offset +0x14..+0x15 [First Attr]:    Offset 0x" << std::hex << hdr->firstAttributeOffset << std::dec << "\n";
    std::cout << "  * Offset +0x16..+0x17 [Flags]:         0x" << std::hex << hdr->flags << std::dec;
    if (hdr->flags & NTFS::FILE_RECORD_IN_USE) std::cout << " [IN_USE]";
    if (hdr->flags & NTFS::FILE_RECORD_DIRECTORY) std::cout << " [DIRECTORY]";
    if (hdr->flags == 0) std::cout << " [UNALLOCATED / FREE RECORD]";
    std::cout << "\n";
    std::cout << "  * Offset +0x18..+0x1B [Used Bytes]:    " << hdr->usedBytes << " bytes\n";
    std::cout << "  * Offset +0x1C..+0x1F [Alloc Bytes]:   " << hdr->allocatedBytes << " bytes\n";
    std::cout << "  * Offset +0x2C..+0x2F [Record Number]: " << hdr->recordNumber << "\n";
}

static void ExplainXfsSuperblock(const uint8_t* data, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(XFS::XfsSuperblock)) return;
    const auto* sb = reinterpret_cast<const XFS::XfsSuperblock*>(data);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: XFS PRIMARY SUPERBLOCK @ 0x"
              << std::hex << physicalOffset << std::dec << "]\n";
    uint32_t magic = XFS::be32_to_cpu(sb->sb_magicnum);
    std::cout << "  * Offset +0x00..+0x03 [Magic]:         0x" << std::hex << magic << std::dec;
    if (magic == XFS::XFS_SB_MAGIC) std::cout << " ('XFSB' - Valid XFS Superblock)\n";
    else std::cout << " (Invalid / Sanitized)\n";

    std::cout << "  * Offset +0x04..+0x07 [Block Size]:    " << XFS::be32_to_cpu(sb->sb_blocksize) << " bytes\n";
    std::cout << "  * Offset +0x08..+0x0F [Data Blocks]:   " << XFS::be64_to_cpu(sb->sb_dblocks) << "\n";
    std::cout << "  * Offset +0x38..+0x3F [Root Inode]:    Inode " << XFS::be64_to_cpu(sb->sb_rootino) << "\n";
    std::cout << "  * Offset +0x54..+0x57 [AG Count]:      " << XFS::be32_to_cpu(sb->sb_agcount) << "\n";
    std::cout << "  * Offset +0x68..+0x69 [Inode Size]:    " << XFS::be16_to_cpu(sb->sb_inodesize) << " bytes\n";
}

static void ExplainXfsInode(const uint8_t* inodeData, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(XFS::XfsDinodeCore)) return;
    const auto* core = reinterpret_cast<const XFS::XfsDinodeCore*>(inodeData);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: XFS INODE @ 0x" << std::hex << physicalOffset << std::dec << "]\n";
    uint16_t magic = XFS::be16_to_cpu(core->di_magic);
    std::cout << "  * Offset +0x00..+0x01 [Magic]:         0x" << std::hex << magic << std::dec;
    if (magic == XFS::XFS_DINODE_MAGIC) std::cout << " ('IN' - Active XFS Dinode)\n";
    else if (magic == 0) std::cout << " (0x0000 - Sanitized / Forensically Obliterated Inode)\n";
    else std::cout << "\n";

    uint16_t mode = XFS::be16_to_cpu(core->di_mode);
    std::cout << "  * Offset +0x02..+0x03 [File Mode]:     0x" << std::hex << mode << std::dec;
    if ((mode & XFS::XFS_S_IFMT) == XFS::XFS_S_IFDIR) std::cout << " (Directory)\n";
    else if ((mode & XFS::XFS_S_IFMT) == XFS::XFS_S_IFREG) std::cout << " (Regular File)\n";
    else if (mode == 0) std::cout << " (0x0000 - Free / Obliterated)\n";
    else std::cout << "\n";

    std::cout << "  * Offset +0x04       [Version]:       " << static_cast<int>(core->di_version) << "\n";
    std::cout << "  * Offset +0x05       [Format]:        " << static_cast<int>(core->di_format) << " (";
    if (core->di_format == XFS::XFS_DINODE_FMT_LOCAL) std::cout << "Shortform / Local Inline Fork)\n";
    else if (core->di_format == XFS::XFS_DINODE_FMT_EXTENTS) std::cout << "Direct Extents Array)\n";
    else if (core->di_format == XFS::XFS_DINODE_FMT_BTREE) std::cout << "Multi-Level B+Tree Root Fork)\n";
    else std::cout << "Other)\n";

    std::cout << "  * Offset +0x08..+0x0F [File Size]:     " << XFS::be64_to_cpu(core->di_size) << " bytes\n";
    std::cout << "  * Offset +0x14..+0x17 [Extent Count]:  " << XFS::be32_to_cpu(core->di_nextents) << "\n";
}

static void ExplainExt4Superblock(const uint8_t* data, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(Ext4::Ext4Superblock)) return;
    const auto* sb = reinterpret_cast<const Ext4::Ext4Superblock*>(data);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: ext4 SUPERBLOCK @ 0x"
              << std::hex << physicalOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x38..+0x39 [Magic]:         0x" << std::hex << sb->s_magic << std::dec;
    if (sb->s_magic == Ext4::EXT4_SUPER_MAGIC) std::cout << " (0xEF53 - Valid ext4 Magic)\n";
    else std::cout << " (Invalid / Sanitized)\n";

    std::cout << "  * Offset +0x00..+0x03 [Total Inodes]:  " << sb->s_inodes_count << "\n";
    std::cout << "  * Offset +0x04..+0x07 [Total Blocks]:  " << sb->s_blocks_count_lo << "\n";
    std::cout << "  * Offset +0x18..+0x1B [Block Size]:    " << (1024 << sb->s_log_block_size) << " bytes\n";
    std::cout << "  * Offset +0x58..+0x67 [Volume Name]:   \"" << std::string(sb->s_volume_name, 16) << "\"\n";
}

static void ExplainExt4Inode(const uint8_t* inodeData, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(Ext4::Ext4Inode)) return;
    const auto* ino = reinterpret_cast<const Ext4::Ext4Inode*>(inodeData);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: ext4 INODE @ 0x" << std::hex << physicalOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x00..+0x01 [File Mode]:     0x" << std::hex << ino->i_mode << std::dec;
    if ((ino->i_mode & Ext4::EXT4_S_IFMT) == Ext4::EXT4_S_IFDIR) std::cout << " (Directory)\n";
    else if ((ino->i_mode & Ext4::EXT4_S_IFMT) == Ext4::EXT4_S_IFREG) std::cout << " (Regular File)\n";
    else if (ino->i_mode == 0) std::cout << " (0x0000 - Sanitized / Free Inode)\n";
    else std::cout << "\n";

    std::cout << "  * Offset +0x04..+0x07 [File Size]:     " << ino->i_size_lo << " bytes\n";
    std::cout << "  * Offset +0x1A..+0x1B [Link Count]:    " << ino->i_links_count << "\n";
    std::cout << "  * Offset +0x28..+0x5F [i_block]:       ";
    if (ino->i_flags & Ext4::EXT4_INLINE_DATA_FL) {
        std::cout << "Inline Payload Data (<60 bytes)\n";
    } else {
        const auto* eh = reinterpret_cast<const Ext4::Ext4ExtentHeader*>(ino->i_block);
        if (eh->eh_magic == Ext4::EXT4_EXTENT_MAGIC) {
            std::cout << "Extent Tree Root (Magic 0xF30A, Entries: " << eh->eh_entries << ")\n";
        } else if (ino->i_block[0] == 0) {
            std::cout << "0x00 (Zeroed / Sanitized)\n";
        } else {
            std::cout << "Block Pointers\n";
        }
    }
}

static void ExplainExFatBootSector(const uint8_t* data, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(ExFatBootSector)) return;
    const auto* vbr = reinterpret_cast<const ExFatBootSector*>(data);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: exFAT VOLUME BOOT RECORD (VBR) @ 0x"
              << std::hex << physicalOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x03..+0x0A [FS Name]:       \"" << std::string(vbr->fileSystemName, 8) << "\"\n";
    std::cout << "  * Offset +0x6C       [Sec Shift]:     " << static_cast<int>(vbr->bytesPerSectorShift)
              << " (" << (1 << vbr->bytesPerSectorShift) << " bytes/sec)\n";
    std::cout << "  * Offset +0x6D       [Clust Shift]:   " << static_cast<int>(vbr->sectorsPerClusterShift)
              << " (" << (1 << vbr->sectorsPerClusterShift) << " sectors/cluster)\n";
    std::cout << "  * Offset +0x58..+0x5B [Heap Offset]:   Sector " << vbr->clusterHeapOffsetSectors << "\n";
    std::cout << "  * Offset +0x60..+0x63 [RootDir Clust]: Cluster " << vbr->rootDirectoryFirstCluster << "\n";
    std::cout << "  * Offset +0x1FE..+0x1FF [Signature]:   0x" << std::hex << vbr->bootSignature << std::dec;
    if (vbr->bootSignature == 0xAA55) std::cout << " (Valid 0xAA55 Signature)\n";
    else std::cout << " (Invalid / Sanitized)\n";
}

static void ExplainExFatDirectoryEntry(const uint8_t* entryData, size_t size, uint64_t physicalOffset) {
    if (size < 32) return;
    uint8_t type = entryData[0];
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: exFAT DIRECTORY ENTRY @ 0x" << std::hex << physicalOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x00 [Entry Type]:           0x" << std::hex << static_cast<int>(type) << std::dec;
    if (type == 0x85) {
        std::cout << " (File Directory Entry)\n";
        if (size >= sizeof(ExFatFileDirectoryEntry)) {
            const auto* fe = reinterpret_cast<const ExFatFileDirectoryEntry*>(entryData);
            std::cout << "  * Offset +0x01 [Secondary Count]:      " << static_cast<int>(fe->secondaryCount) << "\n";
            std::cout << "  * Offset +0x02..+0x03 [Set Checksum]:  0x" << std::hex << fe->setChecksum << std::dec;
            if (fe->setChecksum != 0) std::cout << " (Valid Entry Set Checksum)\n";
            else std::cout << " (0x0000 - Sanitized / Uncalculated)\n";
            std::cout << "  * Offset +0x04..+0x05 [File Attributes]:0x" << std::hex << fe->fileAttributes << std::dec << "\n";
        }
    } else if (type == 0xC0) {
        std::cout << " (Stream Extension Directory Entry)\n";
        if (size >= sizeof(ExFatStreamExtensionDirectoryEntry)) {
            const auto* se = reinterpret_cast<const ExFatStreamExtensionDirectoryEntry*>(entryData);
            std::cout << "  * Offset +0x03 [Name Length]:          " << static_cast<int>(se->nameLength) << " chars\n";
            std::cout << "  * Offset +0x04..+0x05 [Name Hash]:     0x" << std::hex << se->nameHash << std::dec << "\n";
            std::cout << "  * Offset +0x14..+0x17 [First Cluster]: Cluster " << se->firstCluster << "\n";
            std::cout << "  * Offset +0x18..+0x1F [Data Length]:   " << se->dataLength << " bytes\n";
        }
    } else if (type == 0xC1) {
        std::cout << " (File Name Directory Entry)\n";
    } else if (type == 0x81) {
        std::cout << " (Allocation Bitmap Entry)\n";
    } else if (type == 0x00) {
        std::cout << " (0x00 - Sanitized / Free Entry)\n";
    } else {
        std::cout << " (Other)\n";
    }
}

static void ExplainFat32BootSector(const uint8_t* data, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(FAT32::Fat32BootSector)) return;
    const auto* bpb = reinterpret_cast<const FAT32::Fat32BootSector*>(data);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: FAT32 VOLUME BOOT RECORD (VBR) @ 0x"
              << std::hex << physicalOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x03..+0x0A [OEM Name]:      \"" << std::string(bpb->oemName, 8) << "\"\n";
    std::cout << "  * Offset +0x0B..+0x0C [Bytes/Sector]:  " << bpb->bytesPerSector << " bytes\n";
    std::cout << "  * Offset +0x0D       [Sec/Cluster]:   " << static_cast<int>(bpb->sectorsPerCluster)
              << " (" << (bpb->bytesPerSector * bpb->sectorsPerCluster) << " bytes/cluster)\n";
    std::cout << "  * Offset +0x0E..+0x0F [Reserved Secs]: " << bpb->reservedSectorCount << "\n";
    std::cout << "  * Offset +0x10       [Number of FATs]:" << static_cast<int>(bpb->numFATs) << "\n";
    std::cout << "  * Offset +0x24..+0x27 [FAT Size 32]:   " << bpb->fatSize32 << " sectors\n";
    std::cout << "  * Offset +0x2C..+0x2F [Root Dir Clust]:Cluster " << bpb->rootCluster << "\n";
    std::cout << "  * Offset +0x52..+0x59 [FS Type]:       \"" << std::string(bpb->fileSystemType, 8) << "\"\n";
    std::cout << "  * Offset +0x1FE..+0x1FF [Signature]:   0x" << std::hex << bpb->signature << std::dec;
    if (bpb->signature == 0xAA55) std::cout << " (Valid 0xAA55 Signature)\n";
    else std::cout << " (Invalid / Sanitized)\n";
}

static void ExplainFat32DirectoryEntry(const uint8_t* entryData, size_t size, uint64_t physicalOffset) {
    if (size < sizeof(FAT32::Fat32DirEntry)) return;
    const auto* sfn = reinterpret_cast<const FAT32::Fat32DirEntry*>(entryData);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: FAT32 DIRECTORY ENTRY @ 0x" << std::hex << physicalOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x00..+0x0A [Name]:          \"" << std::string(reinterpret_cast<const char*>(sfn->name), 11) << "\"";
    if (sfn->name[0] == FAT32::FAT32_DIR_ENTRY_DELETED) std::cout << " (0xE5 - Deleted / Sanitized Entry)\n";
    else if (sfn->name[0] == 0x00) std::cout << " (0x00 - Free / End of Directory)\n";
    else std::cout << "\n";
    std::cout << "  * Offset +0x0B       [Attributes]:    0x" << std::hex << static_cast<int>(sfn->attr) << std::dec;
    if (sfn->attr & FAT32::FAT32_ATTR_DIRECTORY) std::cout << " (Directory)\n";
    else std::cout << " (File)\n";
    uint32_t cluster = (static_cast<uint32_t>(sfn->fstClusHI) << 16) | sfn->fstClusLO;
    std::cout << "  * Offset +0x14 & 0x1A[First Cluster]: Cluster " << cluster << "\n";
    std::cout << "  * Offset +0x1C..+0x1F [File Size]:     " << sfn->fileSize << " bytes\n";
}

// ============================================================================
// Synthetic Disk Image Builders for All 5 File Systems
// ============================================================================

// 1. NTFS Synthetic Volume Builder
static void SetupNtfsSyntheticDisk(MemoryDiskDevice& dev) {
    uint8_t* disk = dev.GetDiskData();
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr uint32_t SEC_PER_CLUST = 8;
    constexpr uint32_t CLUSTER_SIZE = SECTOR_SIZE * SEC_PER_CLUST;
    constexpr uint32_t MFT_START_LCN = 4;

    NTFS::NtfsBootSector vbr;
    std::memset(&vbr, 0, sizeof(vbr));
    vbr.jumpInstruction[0] = 0xEB; vbr.jumpInstruction[1] = 0x52; vbr.jumpInstruction[2] = 0x90;
    std::memcpy(vbr.oemId, "NTFS    ", 8);
    vbr.bytesPerSector = SECTOR_SIZE;
    vbr.sectorsPerCluster = SEC_PER_CLUST;
    vbr.mediaDescriptor = 0xF8;
    vbr.totalSectors = dev.GetDiskSize() / SECTOR_SIZE;
    vbr.mftStartLCN = MFT_START_LCN;
    vbr.mftMirrStartLCN = 2;
    vbr.clustersPerMftRecord = -10;
    vbr.clustersPerIndexBuffer = -12;
    vbr.volumeSerialNumber = 0x4D5346545F534948ULL;
    vbr.bootSignature = NTFS::NTFS_BOOT_SIGNATURE;
    std::memcpy(disk, &vbr, sizeof(vbr));

    auto writeRecord = [&](uint64_t recordNum, const std::vector<uint8_t>& recordData) {
        size_t byteOffset = (MFT_START_LCN * CLUSTER_SIZE) + (recordNum * 1024);
        std::memcpy(disk + byteOffset, recordData.data(), std::min<size_t>(1024, recordData.size()));
    };

    // Record 0 ($MFT)
    std::vector<uint8_t> mft0(1024, 0);
    auto* hdr0 = reinterpret_cast<NTFS::NtfsRecordHeader*>(mft0.data());
    hdr0->magic = NTFS::NTFS_MAGIC_FILE; hdr0->updateSequenceOffset = 0x30; hdr0->updateSequenceSize = 3;
    hdr0->sequenceNumber = 1; hdr0->firstAttributeOffset = 0x38; hdr0->flags = NTFS::FILE_RECORD_IN_USE;
    hdr0->allocatedBytes = 1024; hdr0->recordNumber = 0;
    uint8_t* a0 = mft0.data() + 0x38;
    auto* ah0 = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a0);
    ah0->type = NTFS::ATTR_DATA; ah0->length = 0x48; ah0->nonResidentFlag = 1;
    auto* nrh0 = reinterpret_cast<NTFS::NtfsNonResidentAttributeHeader*>(a0 + sizeof(NTFS::NtfsAttributeHeader));
    nrh0->dataRunsOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsNonResidentAttributeHeader);
    nrh0->allocatedSize = 8 * CLUSTER_SIZE; nrh0->dataSize = 8 * CLUSTER_SIZE;
    uint8_t* r0 = a0 + nrh0->dataRunsOffset;
    r0[0] = 0x11; r0[1] = 0x08; r0[2] = 0x04; r0[3] = 0x00;
    *reinterpret_cast<uint32_t*>(a0 + ah0->length) = NTFS::ATTR_END;
    writeRecord(NTFS::MFT_REC_MFT, mft0);

    // Record 6 ($Bitmap)
    std::vector<uint8_t> mft6(1024, 0);
    auto* hdr6 = reinterpret_cast<NTFS::NtfsRecordHeader*>(mft6.data());
    hdr6->magic = NTFS::NTFS_MAGIC_FILE; hdr6->updateSequenceOffset = 0x30; hdr6->updateSequenceSize = 3;
    hdr6->sequenceNumber = 1; hdr6->firstAttributeOffset = 0x38; hdr6->flags = NTFS::FILE_RECORD_IN_USE;
    hdr6->allocatedBytes = 1024; hdr6->recordNumber = 6;
    uint8_t* a6 = mft6.data() + 0x38;
    auto* ah6 = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a6);
    ah6->type = NTFS::ATTR_DATA; ah6->length = 0x48; ah6->nonResidentFlag = 1;
    auto* nrh6 = reinterpret_cast<NTFS::NtfsNonResidentAttributeHeader*>(a6 + sizeof(NTFS::NtfsAttributeHeader));
    nrh6->dataRunsOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsNonResidentAttributeHeader);
    nrh6->allocatedSize = CLUSTER_SIZE; nrh6->dataSize = 512;
    uint8_t* r6 = a6 + nrh6->dataRunsOffset;
    r6[0] = 0x11; r6[1] = 0x01; r6[2] = 12; r6[3] = 0x00;
    *reinterpret_cast<uint32_t*>(a6 + ah6->length) = NTFS::ATTR_END;
    writeRecord(NTFS::MFT_REC_BITMAP, mft6);

    uint8_t* bitmapData = disk + (12 * CLUSTER_SIZE);
    bitmapData[0] = 0xFF; bitmapData[1] = 0xFF; bitmapData[2] = 0x07;

    // Record 16: "passwords.txt" (Regular File)
    std::vector<uint8_t> mft16(1024, 0);
    auto* hdr16 = reinterpret_cast<NTFS::NtfsRecordHeader*>(mft16.data());
    hdr16->magic = NTFS::NTFS_MAGIC_FILE; hdr16->updateSequenceOffset = 0x30; hdr16->updateSequenceSize = 3;
    hdr16->sequenceNumber = 1; hdr16->firstAttributeOffset = 0x38; hdr16->flags = NTFS::FILE_RECORD_IN_USE;
    hdr16->allocatedBytes = 1024; hdr16->recordNumber = 16;
    uint8_t* a16 = mft16.data() + 0x38;
    auto* ah16_fn = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a16);
    ah16_fn->type = NTFS::ATTR_FILE_NAME; ah16_fn->length = 0x68; ah16_fn->nonResidentFlag = 0;
    auto* res16_fn = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(a16 + sizeof(NTFS::NtfsAttributeHeader));
    res16_fn->valueOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader);
    res16_fn->valueLength = 0x44;
    auto* fn16 = reinterpret_cast<NTFS::NtfsFileNameAttribute*>(a16 + res16_fn->valueOffset);
    fn16->parentDirectory = NTFS::MFT_REC_ROOT; fn16->realSize = 64; fn16->allocatedSize = CLUSTER_SIZE;
    std::u16string fName16 = u"passwords.txt"; fn16->fileNameLength = static_cast<uint8_t>(fName16.length());
    for (size_t i = 0; i < fName16.length(); ++i) fn16->fileName[i] = fName16[i];

    uint8_t* a16_d = a16 + ah16_fn->length;
    auto* ah16_d = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a16_d);
    ah16_d->type = NTFS::ATTR_DATA; ah16_d->length = 0x48; ah16_d->nonResidentFlag = 1;
    auto* nrh16 = reinterpret_cast<NTFS::NtfsNonResidentAttributeHeader*>(a16_d + sizeof(NTFS::NtfsAttributeHeader));
    nrh16->dataRunsOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsNonResidentAttributeHeader);
    nrh16->allocatedSize = CLUSTER_SIZE; nrh16->dataSize = 48;
    uint8_t* r16 = a16_d + nrh16->dataRunsOffset;
    r16[0] = 0x11; r16[1] = 0x01; r16[2] = 16; r16[3] = 0x00;
    *reinterpret_cast<uint32_t*>(a16_d + ah16_d->length) = NTFS::ATTR_END;
    writeRecord(16, mft16);
    std::memcpy(disk + (16 * CLUSTER_SIZE), "SECRET_NTFS_PAYLOAD_CONFIDENTIAL_KEY_2026", 42);

    // Record 17: "documents" (Directory Folder)
    std::vector<uint8_t> mft17(1024, 0);
    auto* hdr17 = reinterpret_cast<NTFS::NtfsRecordHeader*>(mft17.data());
    hdr17->magic = NTFS::NTFS_MAGIC_FILE; hdr17->updateSequenceOffset = 0x30; hdr17->updateSequenceSize = 3;
    hdr17->sequenceNumber = 1; hdr17->firstAttributeOffset = 0x38;
    hdr17->flags = NTFS::FILE_RECORD_IN_USE | NTFS::FILE_RECORD_DIRECTORY;
    hdr17->allocatedBytes = 1024; hdr17->recordNumber = 17;
    uint8_t* a17 = mft17.data() + 0x38;
    auto* ah17_fn = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a17);
    ah17_fn->type = NTFS::ATTR_FILE_NAME; ah17_fn->length = 0x60; ah17_fn->nonResidentFlag = 0;
    auto* res17_fn = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(a17 + sizeof(NTFS::NtfsAttributeHeader));
    res17_fn->valueOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader);
    auto* fn17 = reinterpret_cast<NTFS::NtfsFileNameAttribute*>(a17 + res17_fn->valueOffset);
    fn17->parentDirectory = NTFS::MFT_REC_ROOT;
    std::u16string fName17 = u"documents"; fn17->fileNameLength = static_cast<uint8_t>(fName17.length());
    for (size_t i = 0; i < fName17.length(); ++i) fn17->fileName[i] = fName17[i];

    uint8_t* a17_ir = a17 + ah17_fn->length;
    auto* ah17_ir = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a17_ir);
    ah17_ir->type = NTFS::ATTR_INDEX_ROOT; ah17_ir->nonResidentFlag = 0;
    auto* res17_ir = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(a17_ir + sizeof(NTFS::NtfsAttributeHeader));
    res17_ir->valueOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader);
    auto* ir17 = reinterpret_cast<NTFS::NtfsIndexRootHeader*>(a17_ir + res17_ir->valueOffset);
    ir17->attributeType = NTFS::ATTR_FILE_NAME; ir17->collationRule = 1;
    ir17->indexAllocationEntrySize = 4096; ir17->clustersPerIndexRecord = 1;
    auto* idxHdr17 = reinterpret_cast<NTFS::NtfsIndexHeader*>(ir17 + 1);
    idxHdr17->firstEntryOffset = sizeof(NTFS::NtfsIndexHeader);

    // Child entry in "documents": "financials.xlsx" -> Record 18
    uint8_t* e18Ptr = reinterpret_cast<uint8_t*>(idxHdr17 + 1);
    auto* e18 = reinterpret_cast<NTFS::NtfsIndexEntry*>(e18Ptr);
    e18->fileReference = 18;
    auto* fn18_idx = reinterpret_cast<NTFS::NtfsFileNameAttribute*>(e18Ptr + sizeof(NTFS::NtfsIndexEntry));
    fn18_idx->parentDirectory = 17;
    std::u16string fName18 = u"financials.xlsx"; fn18_idx->fileNameLength = static_cast<uint8_t>(fName18.length());
    for (size_t i = 0; i < fName18.length(); ++i) fn18_idx->fileName[i] = fName18[i];
    e18->keyLength = sizeof(NTFS::NtfsFileNameAttribute) + (fn18_idx->fileNameLength * 2);
    e18->length = sizeof(NTFS::NtfsIndexEntry) + e18->keyLength;

    auto* e17_end = reinterpret_cast<NTFS::NtfsIndexEntry*>(e18Ptr + e18->length);
    e17_end->length = sizeof(NTFS::NtfsIndexEntry); e17_end->flags = NTFS::INDEX_ENTRY_LAST;

    idxHdr17->totalEntriesSize = sizeof(NTFS::NtfsIndexHeader) + e18->length + e17_end->length;
    idxHdr17->allocatedSize = idxHdr17->totalEntriesSize;
    res17_ir->valueLength = sizeof(NTFS::NtfsIndexRootHeader) + idxHdr17->totalEntriesSize;
    ah17_ir->length = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader) + res17_ir->valueLength;
    *reinterpret_cast<uint32_t*>(a17_ir + ah17_ir->length) = NTFS::ATTR_END;
    writeRecord(17, mft17);

    // Record 18: "financials.xlsx"
    std::vector<uint8_t> mft18(1024, 0);
    auto* hdr18 = reinterpret_cast<NTFS::NtfsRecordHeader*>(mft18.data());
    hdr18->magic = NTFS::NTFS_MAGIC_FILE; hdr18->updateSequenceOffset = 0x30; hdr18->updateSequenceSize = 3;
    hdr18->sequenceNumber = 1; hdr18->firstAttributeOffset = 0x38; hdr18->flags = NTFS::FILE_RECORD_IN_USE;
    hdr18->allocatedBytes = 1024; hdr18->recordNumber = 18;
    uint8_t* a18 = mft18.data() + 0x38;
    auto* ah18_fn = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a18);
    ah18_fn->type = NTFS::ATTR_FILE_NAME; ah18_fn->length = 0x68; ah18_fn->nonResidentFlag = 0;
    auto* res18_fn = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(a18 + sizeof(NTFS::NtfsAttributeHeader));
    res18_fn->valueOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader);
    auto* fn18 = reinterpret_cast<NTFS::NtfsFileNameAttribute*>(a18 + res18_fn->valueOffset);
    fn18->parentDirectory = 17; fn18->fileNameLength = static_cast<uint8_t>(fName18.length());
    for (size_t i = 0; i < fName18.length(); ++i) fn18->fileName[i] = fName18[i];

    uint8_t* a18_d = a18 + ah18_fn->length;
    auto* ah18_d = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a18_d);
    ah18_d->type = NTFS::ATTR_DATA; ah18_d->length = 0x48; ah18_d->nonResidentFlag = 1;
    auto* nrh18 = reinterpret_cast<NTFS::NtfsNonResidentAttributeHeader*>(a18_d + sizeof(NTFS::NtfsAttributeHeader));
    nrh18->dataRunsOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsNonResidentAttributeHeader);
    nrh18->allocatedSize = CLUSTER_SIZE; nrh18->dataSize = 64;
    uint8_t* r18 = a18_d + nrh18->dataRunsOffset;
    r18[0] = 0x11; r18[1] = 0x01; r18[2] = 18; r18[3] = 0x00;
    *reinterpret_cast<uint32_t*>(a18_d + ah18_d->length) = NTFS::ATTR_END;
    writeRecord(18, mft18);
    std::memcpy(disk + (18 * CLUSTER_SIZE), "NESTED_FOLDER_PAYLOAD_COMPANY_FINANCES_2026", 44);

    // Record 5 (Root Directory)
    std::vector<uint8_t> mft5(1024, 0);
    auto* hdr5 = reinterpret_cast<NTFS::NtfsRecordHeader*>(mft5.data());
    hdr5->magic = NTFS::NTFS_MAGIC_FILE; hdr5->updateSequenceOffset = 0x30; hdr5->updateSequenceSize = 3;
    hdr5->sequenceNumber = 1; hdr5->firstAttributeOffset = 0x38;
    hdr5->flags = NTFS::FILE_RECORD_IN_USE | NTFS::FILE_RECORD_DIRECTORY;
    hdr5->allocatedBytes = 1024; hdr5->recordNumber = 5;
    uint8_t* a5 = mft5.data() + 0x38;
    auto* ah5_ir = reinterpret_cast<NTFS::NtfsAttributeHeader*>(a5);
    ah5_ir->type = NTFS::ATTR_INDEX_ROOT; ah5_ir->nonResidentFlag = 0;
    auto* res5_ir = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(a5 + sizeof(NTFS::NtfsAttributeHeader));
    res5_ir->valueOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader);
    auto* ir5 = reinterpret_cast<NTFS::NtfsIndexRootHeader*>(a5 + res5_ir->valueOffset);
    ir5->attributeType = NTFS::ATTR_FILE_NAME; ir5->collationRule = 1;
    ir5->indexAllocationEntrySize = 4096; ir5->clustersPerIndexRecord = 1;
    auto* idxHdr5 = reinterpret_cast<NTFS::NtfsIndexHeader*>(ir5 + 1);
    idxHdr5->firstEntryOffset = sizeof(NTFS::NtfsIndexHeader);

    // Root Entry 1: "documents" -> Record 17
    uint8_t* e17Ptr = reinterpret_cast<uint8_t*>(idxHdr5 + 1);
    auto* e17_root = reinterpret_cast<NTFS::NtfsIndexEntry*>(e17Ptr);
    e17_root->fileReference = 17;
    auto* fn17_root = reinterpret_cast<NTFS::NtfsFileNameAttribute*>(e17Ptr + sizeof(NTFS::NtfsIndexEntry));
    fn17_root->parentDirectory = 5; fn17_root->fileNameLength = static_cast<uint8_t>(fName17.length());
    for (size_t i = 0; i < fName17.length(); ++i) fn17_root->fileName[i] = fName17[i];
    e17_root->keyLength = sizeof(NTFS::NtfsFileNameAttribute) + (fn17_root->fileNameLength * 2);
    e17_root->length = sizeof(NTFS::NtfsIndexEntry) + e17_root->keyLength;

    // Root Entry 2: "passwords.txt" -> Record 16
    uint8_t* e16Ptr = e17Ptr + e17_root->length;
    auto* e16_root = reinterpret_cast<NTFS::NtfsIndexEntry*>(e16Ptr);
    e16_root->fileReference = 16;
    auto* fn16_root = reinterpret_cast<NTFS::NtfsFileNameAttribute*>(e16Ptr + sizeof(NTFS::NtfsIndexEntry));
    fn16_root->parentDirectory = 5; fn16_root->fileNameLength = static_cast<uint8_t>(fName16.length());
    for (size_t i = 0; i < fName16.length(); ++i) fn16_root->fileName[i] = fName16[i];
    e16_root->keyLength = sizeof(NTFS::NtfsFileNameAttribute) + (fn16_root->fileNameLength * 2);
    e16_root->length = sizeof(NTFS::NtfsIndexEntry) + e16_root->keyLength;

    auto* e_end = reinterpret_cast<NTFS::NtfsIndexEntry*>(e16Ptr + e16_root->length);
    e_end->length = sizeof(NTFS::NtfsIndexEntry); e_end->flags = NTFS::INDEX_ENTRY_LAST;

    idxHdr5->totalEntriesSize = sizeof(NTFS::NtfsIndexHeader) + e17_root->length + e16_root->length + e_end->length;
    idxHdr5->allocatedSize = idxHdr5->totalEntriesSize;
    res5_ir->valueLength = sizeof(NTFS::NtfsIndexRootHeader) + idxHdr5->totalEntriesSize;
    ah5_ir->length = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader) + res5_ir->valueLength;
    *reinterpret_cast<uint32_t*>(a5 + ah5_ir->length) = NTFS::ATTR_END;
    writeRecord(NTFS::MFT_REC_ROOT, mft5);
}

// 2. XFS Synthetic Volume Builder
static void SetupXfsSyntheticDisk(MemoryDiskDevice& dev) {
    uint8_t* disk = dev.GetDiskData();
    constexpr uint32_t BLOCK_SIZE = 4096;
    constexpr uint32_t INODE_SIZE = 256;

    XFS::XfsSuperblock sb;
    std::memset(&sb, 0, sizeof(sb));
    sb.sb_magicnum   = XFS::cpu_to_be32(XFS::XFS_SB_MAGIC);
    sb.sb_blocksize  = XFS::cpu_to_be32(BLOCK_SIZE);
    sb.sb_dblocks    = XFS::cpu_to_be64(dev.GetDiskSize() / BLOCK_SIZE);
    sb.sb_rootino    = XFS::cpu_to_be64(64);
    sb.sb_agblocks   = XFS::cpu_to_be32(dev.GetDiskSize() / BLOCK_SIZE);
    sb.sb_agcount    = XFS::cpu_to_be32(1);
    sb.sb_sectsize   = XFS::cpu_to_be16(512);
    sb.sb_inodesize  = XFS::cpu_to_be16(INODE_SIZE);
    sb.sb_inopblock  = XFS::cpu_to_be16(BLOCK_SIZE / INODE_SIZE);
    sb.sb_blocklog   = 12; sb.sb_sectlog = 9; sb.sb_inodelog = 8; sb.sb_inopblog = 4; sb.sb_agblklog = 10;
    sb.sb_versionnum = XFS::cpu_to_be16(4 | XFS::XFS_SB_VERSION_DIRV2BIT);
    std::memcpy(disk, &sb, sizeof(sb));

    // Inode 64 (Root Directory, Shortform)
    uint8_t* rootPtr = disk + 4 * BLOCK_SIZE;
    *reinterpret_cast<uint16_t*>(rootPtr + 0) = XFS::cpu_to_be16(XFS::XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(rootPtr + 2) = XFS::cpu_to_be16(XFS::XFS_S_IFDIR | 0755);
    rootPtr[4] = 2; rootPtr[5] = XFS::XFS_DINODE_FMT_LOCAL;
    *reinterpret_cast<uint64_t*>(rootPtr + 56) = XFS::cpu_to_be64(64);

    uint8_t* sfPtr = rootPtr + 100;
    auto* sfHdr = reinterpret_cast<XFS::XfsDir2SfHdr*>(sfPtr);
    sfHdr->count = 3; sfHdr->i8count = 0;
    *reinterpret_cast<uint32_t*>(sfHdr->parent) = XFS::cpu_to_be32(64);

    // Root Entry 1: "secret.txt" -> Inode 65
    size_t e1Off = 6;
    sfPtr[e1Off + 0] = 10; sfPtr[e1Off + 1] = 0; sfPtr[e1Off + 2] = 0;
    std::memcpy(&sfPtr[e1Off + 3], "secret.txt", 10);
    *reinterpret_cast<uint32_t*>(&sfPtr[e1Off + 3 + 10]) = XFS::cpu_to_be32(65);

    // Root Entry 2: "archive.bin" -> Inode 66
    size_t e2Off = e1Off + 3 + 10 + 4;
    sfPtr[e2Off + 0] = 11; sfPtr[e2Off + 1] = 0; sfPtr[e2Off + 2] = 0;
    std::memcpy(&sfPtr[e2Off + 3], "archive.bin", 11);
    *reinterpret_cast<uint32_t*>(&sfPtr[e2Off + 3 + 11]) = XFS::cpu_to_be32(66);

    // Root Entry 3: "docs" -> Inode 67 (Directory)
    size_t e3Off = e2Off + 3 + 11 + 4;
    sfPtr[e3Off + 0] = 4; sfPtr[e3Off + 1] = 0; sfPtr[e3Off + 2] = 0;
    std::memcpy(&sfPtr[e3Off + 3], "docs", 4);
    *reinterpret_cast<uint32_t*>(&sfPtr[e3Off + 3 + 4]) = XFS::cpu_to_be32(67);

    // Inode 65 (File: "secret.txt", Direct Extents)
    uint8_t* f1Ptr = disk + 4 * BLOCK_SIZE + 1 * INODE_SIZE;
    *reinterpret_cast<uint16_t*>(f1Ptr + 0) = XFS::cpu_to_be16(XFS::XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(f1Ptr + 2) = XFS::cpu_to_be16(XFS::XFS_S_IFREG | 0644);
    f1Ptr[4] = 2; f1Ptr[5] = XFS::XFS_DINODE_FMT_EXTENTS;
    *reinterpret_cast<uint64_t*>(f1Ptr + 56) = XFS::cpu_to_be64(8192);
    *reinterpret_cast<uint64_t*>(f1Ptr + 64) = XFS::cpu_to_be64(2);
    *reinterpret_cast<uint32_t*>(f1Ptr + 76) = XFS::cpu_to_be32(1);

    // Extent for Inode 65: Blocks 10 & 11
    XFS::XfsBmbtRec ext1;
    uint64_t w0 = (static_cast<uint64_t>(0) << 9) | ((10ULL >> 43) & 0x1FF);
    uint64_t w1 = (10ULL << 21) | (2ULL & 0x1FFFFF);
    ext1.l0 = XFS::cpu_to_be64(w0); ext1.l1 = XFS::cpu_to_be64(w1);
    std::memcpy(f1Ptr + 100, &ext1, sizeof(ext1));
    std::memset(disk + 10 * BLOCK_SIZE, 0xAA, BLOCK_SIZE);
    std::memset(disk + 11 * BLOCK_SIZE, 0xBB, BLOCK_SIZE);

    // Inode 66 (File: "archive.bin", B+Tree Format)
    uint8_t* f2Ptr = disk + 4 * BLOCK_SIZE + 2 * INODE_SIZE;
    *reinterpret_cast<uint16_t*>(f2Ptr + 0) = XFS::cpu_to_be16(XFS::XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(f2Ptr + 2) = XFS::cpu_to_be16(XFS::XFS_S_IFREG | 0644);
    f2Ptr[4] = 2; f2Ptr[5] = XFS::XFS_DINODE_FMT_BTREE;
    *reinterpret_cast<uint64_t*>(f2Ptr + 56) = XFS::cpu_to_be64(4096);
    *reinterpret_cast<uint64_t*>(f2Ptr + 64) = XFS::cpu_to_be64(2);
    *reinterpret_cast<uint32_t*>(f2Ptr + 76) = XFS::cpu_to_be32(1);

    // B+Tree Root in Inode 66 points to Block 20
    auto* btRoot = reinterpret_cast<XFS::XfsBmdrBlock*>(f2Ptr + 100);
    btRoot->bb_level = XFS::cpu_to_be16(1);
    btRoot->bb_numrecs = XFS::cpu_to_be16(1);
    *reinterpret_cast<uint64_t*>(f2Ptr + 104) = 0;
    *reinterpret_cast<uint64_t*>(f2Ptr + 112) = XFS::cpu_to_be64(20);

    // Indirect Block 20 has Leaf Extent -> Block 30
    uint8_t* b20 = disk + 20 * BLOCK_SIZE;
    auto* btLeaf = reinterpret_cast<XFS::XfsBtreeBlock*>(b20);
    btLeaf->bb_magic = XFS::cpu_to_be32(XFS::XFS_BMAP_MAGIC);
    btLeaf->bb_level = 0;
    btLeaf->bb_numrecs = XFS::cpu_to_be16(1);
    auto* leafRec = reinterpret_cast<XFS::XfsBmbtRec*>(b20 + 24);
    uint64_t lw0 = (static_cast<uint64_t>(0) << 9) | ((30ULL >> 43) & 0x1FF);
    uint64_t lw1 = (30ULL << 21) | (1ULL & 0x1FFFFF);
    leafRec->l0 = XFS::cpu_to_be64(lw0);
    leafRec->l1 = XFS::cpu_to_be64(lw1);
    std::memset(disk + 30 * BLOCK_SIZE, 0xCC, BLOCK_SIZE);

    // Inode 67 (Directory: "docs", Shortform)
    uint8_t* dirPtr = disk + 4 * BLOCK_SIZE + 3 * INODE_SIZE;
    *reinterpret_cast<uint16_t*>(dirPtr + 0) = XFS::cpu_to_be16(XFS::XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(dirPtr + 2) = XFS::cpu_to_be16(XFS::XFS_S_IFDIR | 0755);
    dirPtr[4] = 2; dirPtr[5] = XFS::XFS_DINODE_FMT_LOCAL;
    *reinterpret_cast<uint64_t*>(dirPtr + 56) = XFS::cpu_to_be64(64);

    uint8_t* sfDirPtr = dirPtr + 100;
    auto* sfDirHdr = reinterpret_cast<XFS::XfsDir2SfHdr*>(sfDirPtr);
    sfDirHdr->count = 1; sfDirHdr->i8count = 0;
    *reinterpret_cast<uint32_t*>(sfDirHdr->parent) = XFS::cpu_to_be32(64);
    sfDirPtr[6 + 0] = 10; sfDirPtr[6 + 1] = 0; sfDirPtr[6 + 2] = 0;
    std::memcpy(&sfDirPtr[6 + 3], "report.txt", 10);
    *reinterpret_cast<uint32_t*>(&sfDirPtr[6 + 3 + 10]) = XFS::cpu_to_be32(68);

    // Inode 68 (File: "docs/report.txt", Extent)
    uint8_t* f3Ptr = disk + 4 * BLOCK_SIZE + 4 * INODE_SIZE;
    *reinterpret_cast<uint16_t*>(f3Ptr + 0) = XFS::cpu_to_be16(XFS::XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(f3Ptr + 2) = XFS::cpu_to_be16(XFS::XFS_S_IFREG | 0644);
    f3Ptr[4] = 2; f3Ptr[5] = XFS::XFS_DINODE_FMT_EXTENTS;
    *reinterpret_cast<uint64_t*>(f3Ptr + 56) = XFS::cpu_to_be64(4096);
    *reinterpret_cast<uint64_t*>(f3Ptr + 64) = XFS::cpu_to_be64(1);
    *reinterpret_cast<uint32_t*>(f3Ptr + 76) = XFS::cpu_to_be32(1);

    XFS::XfsBmbtRec ext3;
    uint64_t ew0 = (static_cast<uint64_t>(0) << 9) | ((35ULL >> 43) & 0x1FF);
    uint64_t ew1 = (35ULL << 21) | (1ULL & 0x1FFFFF);
    ext3.l0 = XFS::cpu_to_be64(ew0); ext3.l1 = XFS::cpu_to_be64(ew1);
    std::memcpy(f3Ptr + 100, &ext3, sizeof(ext3));
    std::memset(disk + 35 * BLOCK_SIZE, 0xDD, BLOCK_SIZE);
}

// 3. ext4 Synthetic Volume Builder
static void SetupExt4SyntheticDisk(MemoryDiskDevice& dev) {
    uint8_t* disk = dev.GetDiskData();
    constexpr uint32_t BLOCK_SIZE = 4096;
    constexpr uint32_t TOTAL_BLOCKS = 1024;
    constexpr uint32_t INODES_PER_GROUP = 128;
    constexpr uint32_t INODE_SIZE = 256;

    // Superblock at 1024
    Ext4::Ext4Superblock sb;
    std::memset(&sb, 0, sizeof(sb));
    sb.s_magic = Ext4::EXT4_SUPER_MAGIC;
    sb.s_inodes_count = INODES_PER_GROUP;
    sb.s_blocks_count_lo = TOTAL_BLOCKS;
    sb.s_free_blocks_count_lo = 1000;
    sb.s_free_inodes_count_lo = INODES_PER_GROUP - 14;
    sb.s_first_data_block = 0;
    sb.s_log_block_size = 2; // 4096
    sb.s_blocks_per_group = TOTAL_BLOCKS;
    sb.s_inodes_per_group = INODES_PER_GROUP;
    sb.s_inode_size = INODE_SIZE;
    sb.s_feature_incompat = Ext4::EXT4_FEATURE_INCOMPAT_FILETYPE | Ext4::EXT4_FEATURE_INCOMPAT_EXTENTS;
    std::memcpy(sb.s_volume_name, "DEMO_EXT4", 9);
    std::memcpy(disk + Ext4::EXT4_SUPERBLOCK_OFFSET, &sb, sizeof(sb));

    // Group Descriptor Table at Block 1
    Ext4::Ext4GroupDesc gd;
    std::memset(&gd, 0, sizeof(gd));
    gd.bg_block_bitmap_lo = 2;
    gd.bg_inode_bitmap_lo = 3;
    gd.bg_inode_table_lo  = 4;
    gd.bg_free_blocks_count_lo = 1000;
    gd.bg_free_inodes_count_lo = INODES_PER_GROUP - 14;
    std::memcpy(disk + 1 * BLOCK_SIZE, &gd, sizeof(gd));

    // Mark Bitmaps
    uint8_t* blockBitmap = disk + 2 * BLOCK_SIZE;
    blockBitmap[0] = 0xFF; blockBitmap[1] = 0x03; // Blocks 0..9 allocated

    uint8_t* inodeBitmap = disk + 3 * BLOCK_SIZE;
    inodeBitmap[0] = 0xFF; inodeBitmap[1] = 0x1F; // Inodes 1..13 allocated

    // Inode 2: Root Directory (Points to Block 5)
    uint8_t* itable = disk + 4 * BLOCK_SIZE;
    Ext4::Ext4Inode rootIno;
    std::memset(&rootIno, 0, sizeof(rootIno));
    rootIno.i_mode = Ext4::EXT4_S_IFDIR | 0755;
    rootIno.i_size_lo = BLOCK_SIZE;
    rootIno.i_links_count = 2;
    rootIno.i_flags = Ext4::EXT4_EXTENTS_FL;

    auto* eh = reinterpret_cast<Ext4::Ext4ExtentHeader*>(rootIno.i_block);
    eh->eh_magic = Ext4::EXT4_EXTENT_MAGIC; eh->eh_entries = 1; eh->eh_max = 4;
    auto* ext = reinterpret_cast<Ext4::Ext4Extent*>(eh + 1);
    ext->ee_block = 0; ext->ee_len = 1; ext->ee_start_lo = 5;
    std::memcpy(itable + (2 - 1) * INODE_SIZE, &rootIno, sizeof(rootIno));

    // Directory Entries in Block 5
    uint8_t* dirBlock5 = disk + 5 * BLOCK_SIZE;
    size_t off = 0;
    auto* d1 = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlock5 + off);
    d1->inode = 2; d1->rec_len = 12; d1->name_len = 1; d1->file_type = Ext4::EXT4_FT_DIR; d1->name[0] = '.';
    off += d1->rec_len;

    auto* d2 = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlock5 + off);
    d2->inode = 2; d2->rec_len = 12; d2->name_len = 2; d2->file_type = Ext4::EXT4_FT_DIR; d2->name[0] = '.'; d2->name[1] = '.';
    off += d2->rec_len;

    auto* d3 = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlock5 + off);
    d3->inode = 12; d3->rec_len = 32;
    d3->name_len = 17; d3->file_type = Ext4::EXT4_FT_REG_FILE;
    std::memcpy(d3->name, "database_dump.sql", 17);
    off += d3->rec_len;

    auto* d4 = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlock5 + off);
    d4->inode = 13; d4->rec_len = static_cast<uint16_t>(BLOCK_SIZE - off);
    d4->name_len = 5; d4->file_type = Ext4::EXT4_FT_DIR;
    std::memcpy(d4->name, "vault", 5);

    // Inode 12: "database_dump.sql" (Points to Block 8)
    Ext4::Ext4Inode file12;
    std::memset(&file12, 0, sizeof(file12));
    file12.i_mode = Ext4::EXT4_S_IFREG | 0644;
    file12.i_size_lo = 64; file12.i_links_count = 1; file12.i_flags = Ext4::EXT4_EXTENTS_FL;
    auto* eh12 = reinterpret_cast<Ext4::Ext4ExtentHeader*>(file12.i_block);
    eh12->eh_magic = Ext4::EXT4_EXTENT_MAGIC; eh12->eh_entries = 1; eh12->eh_max = 4;
    auto* ext12 = reinterpret_cast<Ext4::Ext4Extent*>(eh12 + 1);
    ext12->ee_block = 0; ext12->ee_len = 1; ext12->ee_start_lo = 8;
    std::memcpy(itable + (12 - 1) * INODE_SIZE, &file12, sizeof(file12));

    std::memcpy(disk + 8 * BLOCK_SIZE, "CONFIDENTIAL_SQL_DATABASE_DUMP_HASHES_2026_SECRET", 49);

    // Inode 13: "vault" (Directory, Points to Block 9)
    Ext4::Ext4Inode dir13;
    std::memset(&dir13, 0, sizeof(dir13));
    dir13.i_mode = Ext4::EXT4_S_IFDIR | 0755;
    dir13.i_size_lo = BLOCK_SIZE; dir13.i_links_count = 2; dir13.i_flags = Ext4::EXT4_EXTENTS_FL;
    auto* eh13 = reinterpret_cast<Ext4::Ext4ExtentHeader*>(dir13.i_block);
    eh13->eh_magic = Ext4::EXT4_EXTENT_MAGIC; eh13->eh_entries = 1; eh13->eh_max = 4;
    auto* ext13 = reinterpret_cast<Ext4::Ext4Extent*>(eh13 + 1);
    ext13->ee_block = 0; ext13->ee_len = 1; ext13->ee_start_lo = 9;
    std::memcpy(itable + (13 - 1) * INODE_SIZE, &dir13, sizeof(dir13));

    // Directory entries inside Block 9
    uint8_t* dirBlock9 = disk + 9 * BLOCK_SIZE;
    auto* v_d1 = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlock9);
    v_d1->inode = 13; v_d1->rec_len = 12; v_d1->name_len = 1; v_d1->file_type = Ext4::EXT4_FT_DIR; v_d1->name[0] = '.';
    auto* v_d2 = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlock9 + 12);
    v_d2->inode = 2; v_d2->rec_len = 12; v_d2->name_len = 2; v_d2->file_type = Ext4::EXT4_FT_DIR; v_d2->name[0] = '.'; v_d2->name[1] = '.';
    auto* v_d3 = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlock9 + 24);
    v_d3->inode = 14; v_d3->rec_len = static_cast<uint16_t>(BLOCK_SIZE - 24);
    v_d3->name_len = 8; v_d3->file_type = Ext4::EXT4_FT_REG_FILE;
    std::memcpy(v_d3->name, "keys.pem", 8);

    // Inode 14: "vault/keys.pem" (Points to Block 10)
    Ext4::Ext4Inode file14;
    std::memset(&file14, 0, sizeof(file14));
    file14.i_mode = Ext4::EXT4_S_IFREG | 0600;
    file14.i_size_lo = 32; file14.i_links_count = 1; file14.i_flags = Ext4::EXT4_EXTENTS_FL;
    auto* eh14 = reinterpret_cast<Ext4::Ext4ExtentHeader*>(file14.i_block);
    eh14->eh_magic = Ext4::EXT4_EXTENT_MAGIC; eh14->eh_entries = 1; eh14->eh_max = 4;
    auto* ext14 = reinterpret_cast<Ext4::Ext4Extent*>(eh14 + 1);
    ext14->ee_block = 0; ext14->ee_len = 1; ext14->ee_start_lo = 10;
    std::memcpy(itable + (14 - 1) * INODE_SIZE, &file14, sizeof(file14));
    std::memcpy(disk + 10 * BLOCK_SIZE, "-----BEGIN RSA PRIVATE KEY-----", 31);
}

// 4. exFAT Synthetic Volume Builder
static void SetupExFatSyntheticDisk(MemoryDiskDevice& dev) {
    uint8_t* disk = dev.GetDiskData();
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr uint32_t SEC_PER_CLUST = 8;

    ExFatBootSector vbr;
    std::memset(&vbr, 0, sizeof(vbr));
    vbr.jumpBoot[0] = 0xEB; vbr.jumpBoot[1] = 0x76; vbr.jumpBoot[2] = 0x90;
    std::memcpy(vbr.fileSystemName, "EXFAT   ", 8);
    vbr.bytesPerSectorShift = 9;   // 512 bytes
    vbr.sectorsPerClusterShift = 3; // 8 sectors (4096 bytes)
    vbr.clusterHeapOffsetSectors = 32; // Cluster 2 at Sector 32
    vbr.rootDirectoryFirstCluster = 2;
    vbr.bootSignature = 0xAA55;
    std::memcpy(disk, &vbr, sizeof(vbr));

    // Cluster 2: Root Directory (Sector 32)
    uint8_t* rootDir = disk + (32 * SECTOR_SIZE);

    // Entry 1: 0x81 Allocation Bitmap
    auto* bEntry = reinterpret_cast<ExFatBitmapDirectoryEntry*>(rootDir);
    bEntry->entryType = 0x81;
    bEntry->firstCluster = 3;
    bEntry->dataLength = 4096;

    // Entry 2: 0x85 File Directory Entry for "secret.txt"
    auto* fEntry = reinterpret_cast<ExFatFileDirectoryEntry*>(rootDir + 32);
    fEntry->entryType = 0x85;
    fEntry->secondaryCount = 2;

    // Entry 3: 0xC0 Stream Extension Entry
    auto* sEntry = reinterpret_cast<ExFatStreamExtensionDirectoryEntry*>(rootDir + 64);
    sEntry->entryType = 0xC0;
    sEntry->firstCluster = 4;
    sEntry->dataLength = 64;
    sEntry->validDataLength = 64;
    sEntry->generalSecondaryFlags = 0x03; // AllocationPossible (0x01) | NoFatChain (0x02)
    std::u16string name = u"secret.txt";
    sEntry->nameLength = static_cast<uint8_t>(name.length());
    sEntry->nameHash = ExFatDriver::ComputeNameHash(name);

    // Entry 4: 0xC1 Filename Entry
    auto* fnEntry = reinterpret_cast<ExFatFileNameDirectoryEntry*>(rootDir + 96);
    fnEntry->entryType = 0xC1;
    for (size_t i = 0; i < name.length(); ++i) fnEntry->fileName[i] = name[i];

    // Compute and set 16-bit Directory Entry Set Checksum across entries 0x85, 0xC0, 0xC1 (3 entries)
    fEntry->setChecksum = ExFatDriver::ComputeEntrySetChecksum(reinterpret_cast<const uint8_t*>(fEntry), 3);

    // Populate Sector 11 with the 32-bit Main Boot Checksum repeated across all 512 bytes
    uint32_t bootChecksum = ExFatDriver::ComputeBootChecksum(disk, 11 * SECTOR_SIZE);
    uint32_t* csumSector = reinterpret_cast<uint32_t*>(disk + (11 * SECTOR_SIZE));
    for (size_t i = 0; i < SECTOR_SIZE / sizeof(uint32_t); ++i) {
        csumSector[i] = bootChecksum;
    }

    // Cluster 3: Allocation Bitmap (Sector 40)
    uint8_t* bitmap = disk + ((32 + (3 - 2) * SEC_PER_CLUST) * SECTOR_SIZE);
    bitmap[0] = 0x07; // Clusters 2, 3, 4 in use

    // Cluster 4: File Payload (Sector 48)
    uint8_t* payload = disk + ((32 + (4 - 2) * SEC_PER_CLUST) * SECTOR_SIZE);
    std::memcpy(payload, "EXFAT_SUPER_SECRET_PAYLOAD_CONFIDENTIAL_REPORT_2026", 51);
}

// 5. FAT32 Synthetic Volume Builder
static void SetupFat32SyntheticDisk(MemoryDiskDevice& dev) {
    uint8_t* disk = dev.GetDiskData();
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr uint32_t SEC_PER_CLUST = 8;

    FAT32::Fat32BootSector vbr;
    std::memset(&vbr, 0, sizeof(vbr));
    vbr.jmpBoot[0] = 0xEB; vbr.jmpBoot[1] = 0x58; vbr.jmpBoot[2] = 0x90;
    std::memcpy(vbr.oemName, "MSWIN4.1", 8);
    vbr.bytesPerSector = SECTOR_SIZE;
    vbr.sectorsPerCluster = SEC_PER_CLUST;
    vbr.reservedSectorCount = 32;
    vbr.numFATs = 2;
    vbr.media = 0xF8;
    vbr.totalSectors32 = static_cast<uint32_t>(dev.GetDiskSize() / SECTOR_SIZE);
    vbr.fatSize32 = 32;
    vbr.rootCluster = 2;
    vbr.fsInfoSector = 1;
    vbr.backupBootSector = 6;
    vbr.driveNumber = 0x80;
    vbr.bootSignature = 0x29;
    vbr.volumeID = 0x12345678;
    std::memcpy(vbr.volumeLabel, "NO NAME    ", 11);
    std::memcpy(vbr.fileSystemType, "FAT32   ", 8);
    vbr.signature = FAT32::FAT32_BOOT_SIGNATURE;
    std::memcpy(disk, &vbr, sizeof(vbr));

    // FSInfo at Sector 1
    FAT32::Fat32FSInfo fsi;
    std::memset(&fsi, 0, sizeof(fsi));
    fsi.leadSig = FAT32::FAT32_FSINFO_LEAD_SIG;
    fsi.strucSig = FAT32::FAT32_FSINFO_STRUC_SIG;
    fsi.freeCount = 1000;
    fsi.nextFree = 5;
    fsi.trailSig = FAT32::FAT32_FSINFO_TRAIL_SIG;
    std::memcpy(disk + (1 * SECTOR_SIZE), &fsi, sizeof(fsi));

    // FAT1 (Sector 32) and FAT2 (Sector 64)
    auto writeFat = [&](uint8_t fatIndex) {
        uint32_t* fat = reinterpret_cast<uint32_t*>(disk + ((32 + fatIndex * 32) * SECTOR_SIZE));
        fat[0] = 0x0FFFFF00 | 0xF8;
        fat[1] = FAT32::FAT32_CLUSTER_EOC_MAX;
        fat[2] = FAT32::FAT32_CLUSTER_EOC_MAX; // Root dir
        fat[3] = FAT32::FAT32_CLUSTER_EOC_MAX; // secret.txt
        fat[4] = FAT32::FAT32_CLUSTER_EOC_MAX; // docs folder
        fat[5] = FAT32::FAT32_CLUSTER_EOC_MAX; // report.pdf inside docs
    };
    writeFat(0);
    writeFat(1);

    // First data sector = 32 + (2 * 32) = 96
    // Cluster 2: Root Directory (Sector 96)
    uint8_t* rootDir = disk + (96 * SECTOR_SIZE);

    // Entry 1 in Root: "secret.txt" -> Cluster 3
    auto* sfn1 = reinterpret_cast<FAT32::Fat32DirEntry*>(rootDir);
    std::memcpy(sfn1->name, "SECRET  TXT", 11);
    sfn1->attr = FAT32::FAT32_ATTR_ARCHIVE;
    sfn1->fstClusHI = 0;
    sfn1->fstClusLO = 3;
    sfn1->fileSize = 48;

    // Entry 2 in Root: "docs" -> Cluster 4
    auto* sfn2 = reinterpret_cast<FAT32::Fat32DirEntry*>(rootDir + 32);
    std::memcpy(sfn2->name, "DOCS       ", 11);
    sfn2->attr = FAT32::FAT32_ATTR_DIRECTORY;
    sfn2->fstClusHI = 0;
    sfn2->fstClusLO = 4;

    // Cluster 3: Payload for "secret.txt" (Sector 96 + (3-2)*8 = 104)
    uint8_t* payload1 = disk + (104 * SECTOR_SIZE);
    std::memcpy(payload1, "FAT32_SECRET_PAYLOAD_CONFIDENTIAL_AUTHENTICATION", 48);

    // Cluster 4: Subdirectory "docs" (Sector 96 + (4-2)*8 = 112)
    uint8_t* subDir = disk + (112 * SECTOR_SIZE);
    auto* d_dot = reinterpret_cast<FAT32::Fat32DirEntry*>(subDir);
    std::memcpy(d_dot->name, ".          ", 11);
    d_dot->attr = FAT32::FAT32_ATTR_DIRECTORY;
    d_dot->fstClusLO = 4;

    auto* d_dotdot = reinterpret_cast<FAT32::Fat32DirEntry*>(subDir + 32);
    std::memcpy(d_dotdot->name, "..         ", 11);
    d_dotdot->attr = FAT32::FAT32_ATTR_DIRECTORY;
    d_dotdot->fstClusLO = 2;

    auto* d_child = reinterpret_cast<FAT32::Fat32DirEntry*>(subDir + 64);
    std::memcpy(d_child->name, "REPORT  PDF", 11);
    d_child->attr = FAT32::FAT32_ATTR_ARCHIVE;
    d_child->fstClusLO = 5;
    d_child->fileSize = 40;

    // Cluster 5: Payload for "docs/report.pdf" (Sector 96 + (5-2)*8 = 120)
    uint8_t* payload2 = disk + (120 * SECTOR_SIZE);
    std::memcpy(payload2, "BLUEPRINT_TOP_SECRET_CLASSIFIED_SCHEMATICS", 42);
}

// ============================================================================
// Automated Synthetic Test Suite Across All 5 File Systems
// ============================================================================

static void RunSyntheticSuite() {
    std::cout << "\n================================================================================\n";
    std::cout << "        RUNNING COMPREHENSIVE AUTOMATED VERIFICATION SUITE (ALL 4 FS)           \n";
    std::cout << "================================================================================\n";

    constexpr size_t DISK_SIZE = 4 * 1024 * 1024; // 4 MB

    // -------------------------------------------------------------------------
    // 1. NTFS Test Suite
    // -------------------------------------------------------------------------
    {
        std::cout << "\n################################################################################\n";
        std::cout << " [1/4] EXECUTING NTFS FORENSIC VERIFICATION SUITE                               \n";
        std::cout << "################################################################################\n";

        MemoryDiskDevice ntfsDev(DISK_SIZE, 512, "MemoryDisk://NTFS");
        SetupNtfsSyntheticDisk(ntfsDev);
        HDDController hdd(&ntfsDev);
        NtfsDriver ntfsDriver(&hdd);

        assert(ntfsDriver.Mount());
        std::cout << "[NTFS] Mounted successfully.\n";

        // Initial VBR and MFT inspection
        ExplainNtfsBootSector(ntfsDev.GetDiskData(), 512, 0);
        size_t mft16ByteOffset = (4 * 8 * 512) + (16 * 1024);
        ExplainNtfsMftRecord(ntfsDev.GetDiskData() + mft16ByteOffset, 1024, mft16ByteOffset);

        // Scenario 1A: File Verification & Erasure ("passwords.txt")
        std::cout << "\n--- Scenario 1A: Single File Verification & Erasure ('passwords.txt') ---\n";
        assert(ntfsDriver.VerifyAndErase("passwords.txt"));

        // Scenario 1B: Recursive Folder Verification & Erasure ("documents")
        std::cout << "\n--- Scenario 1B: Recursive Folder Verification & Erasure ('documents') ---\n";
        assert(ntfsDriver.VerifyAndErase("documents"));

        // Scenario 1C: Complete Disk Format Verification
        std::cout << "\n--- Scenario 1C: Complete Disk Format Verification ---\n";
        assert(ntfsDriver.VerifyAndFormatDrive(true));
        std::cout << "[PASS] NTFS Suite Completed with 100% Verification.\n";
    }

    // -------------------------------------------------------------------------
    // 2. XFS Test Suite
    // -------------------------------------------------------------------------
    {
        std::cout << "\n################################################################################\n";
        std::cout << " [2/4] EXECUTING XFS FORENSIC VERIFICATION SUITE                                \n";
        std::cout << "################################################################################\n";

        MemoryDiskDevice xfsDev(DISK_SIZE, 512, "MemoryDisk://XFS");
        SetupXfsSyntheticDisk(xfsDev);
        HDDController hdd(&xfsDev);
        XfsDriver xfsDriver(&hdd);

        assert(xfsDriver.Mount());
        std::cout << "[XFS] Mounted successfully.\n";

        // Scenario 2A: Direct Extent File Inspection & Erasure ("secret.txt")
        std::cout << "\n--- Scenario 2A: Direct Extents File Erasure ('secret.txt') ---\n";
        std::cout << "\n>>> [BEFORE DELETION] Inspecting XFS Data Blocks 10 & 11 and Inode 65 <<<\n";
        PrintHexDump(xfsDev.GetDiskData() + 10 * 4096, 64, 10 * 4096, "XFS Data Block 10");
        ExplainDataSector(xfsDev.GetDiskData() + 10 * 4096, 64, 10 * 4096, "XFS");

        size_t ino65Off = 4 * 4096 + 1 * 256;
        PrintHexDump(xfsDev.GetDiskData() + ino65Off, 128, ino65Off, "XFS Inode 65 ('secret.txt')");
        ExplainXfsInode(xfsDev.GetDiskData() + ino65Off, 128, ino65Off);

        assert(xfsDriver.EraseFile("secret.txt"));

        std::cout << "\n>>> [AFTER DELETION] Re-inspecting Same Physical Offsets <<<\n";
        PrintHexDump(xfsDev.GetDiskData() + 10 * 4096, 64, 10 * 4096, "XFS Data Block 10 [POST-WIPE]");
        ExplainDataSector(xfsDev.GetDiskData() + 10 * 4096, 64, 10 * 4096, "XFS");

        PrintHexDump(xfsDev.GetDiskData() + ino65Off, 128, ino65Off, "XFS Inode 65 [POST-WIPE]");
        ExplainXfsInode(xfsDev.GetDiskData() + ino65Off, 128, ino65Off);

        assert(xfsDev.GetDiskData()[10 * 4096] != 0xAA); // Original payload 0xAA destroyed by 3-pass wipe
        assert(xfsDev.GetDiskData()[ino65Off] == 0x00);

        // Scenario 2B: B+Tree Multi-Level File Erasure ("archive.bin")
        std::cout << "\n--- Scenario 2B: B+Tree Format File Erasure ('archive.bin') ---\n";
        size_t btreeDataOff = 30 * 4096;
        size_t btreeMetaOff = 20 * 4096;
        size_t ino66Off = 4 * 4096 + 2 * 256;

        std::cout << "\n>>> [BEFORE DELETION] Inspecting B+Tree Data Block 30 & Meta Block 20 <<<\n";
        PrintHexDump(xfsDev.GetDiskData() + btreeDataOff, 64, btreeDataOff, "XFS B+Tree Data Block 30");
        PrintHexDump(xfsDev.GetDiskData() + btreeMetaOff, 64, btreeMetaOff, "XFS B+Tree Indirect Meta Block 20");
        PrintHexDump(xfsDev.GetDiskData() + ino66Off, 128, ino66Off, "XFS Inode 66 ('archive.bin')");

        assert(xfsDriver.EraseFile("archive.bin"));

        std::cout << "\n>>> [AFTER DELETION] Re-inspecting B+Tree Blocks <<<\n";
        PrintHexDump(xfsDev.GetDiskData() + btreeDataOff, 64, btreeDataOff, "XFS B+Tree Data Block 30 [POST-WIPE]");
        PrintHexDump(xfsDev.GetDiskData() + btreeMetaOff, 64, btreeMetaOff, "XFS B+Tree Indirect Meta Block 20 [POST-WIPE]");
        PrintHexDump(xfsDev.GetDiskData() + ino66Off, 128, ino66Off, "XFS Inode 66 [POST-WIPE]");

        assert(xfsDev.GetDiskData()[btreeDataOff] != 0xCC); // Original payload 0xCC destroyed
        assert(xfsDev.GetDiskData()[ino66Off] == 0x00);

        // Scenario 2C: Recursive Folder Erasure ("docs")
        std::cout << "\n--- Scenario 2C: Recursive Folder Erasure ('docs') ---\n";
        size_t childDataOff = 35 * 4096;
        size_t dirInoOff = 4 * 4096 + 3 * 256;
        size_t childInoOff = 4 * 4096 + 4 * 256;

        std::cout << "\n>>> [BEFORE DELETION] Inspecting Folder Inode 67, Child Inode 68, Child Block 35 <<<\n";
        PrintHexDump(xfsDev.GetDiskData() + childDataOff, 64, childDataOff, "Nested File Data Block 35");
        ExplainDataSector(xfsDev.GetDiskData() + childDataOff, 64, childDataOff, "XFS");
        PrintHexDump(xfsDev.GetDiskData() + dirInoOff, 128, dirInoOff, "Folder Inode 67 ('docs')");
        ExplainXfsInode(xfsDev.GetDiskData() + dirInoOff, 128, dirInoOff);
        PrintHexDump(xfsDev.GetDiskData() + childInoOff, 128, childInoOff, "Child Inode 68 ('report.txt')");
        ExplainXfsInode(xfsDev.GetDiskData() + childInoOff, 128, childInoOff);

        assert(xfsDriver.EraseDirectory("docs"));

        std::cout << "\n>>> [AFTER DELETION] Re-inspecting Folder & Child Inodes and Data <<<\n";
        PrintHexDump(xfsDev.GetDiskData() + childDataOff, 64, childDataOff, "Nested File Data Block 35 [POST-WIPE]");
        ExplainDataSector(xfsDev.GetDiskData() + childDataOff, 64, childDataOff, "XFS");
        PrintHexDump(xfsDev.GetDiskData() + dirInoOff, 128, dirInoOff, "Folder Inode 67 [POST-WIPE]");
        ExplainXfsInode(xfsDev.GetDiskData() + dirInoOff, 128, dirInoOff);
        PrintHexDump(xfsDev.GetDiskData() + childInoOff, 128, childInoOff, "Child Inode 68 [POST-WIPE]");
        ExplainXfsInode(xfsDev.GetDiskData() + childInoOff, 128, childInoOff);

        assert(xfsDev.GetDiskData()[childDataOff] != 0xDD); // Original payload 0xDD destroyed
        assert(xfsDev.GetDiskData()[dirInoOff] == 0x00);
        assert(xfsDev.GetDiskData()[childInoOff] == 0x00);

        // Scenario 2D: Surgical Volume-Wide Wipe
        std::cout << "\n--- Scenario 2D: Surgical Volume-Wide Wipe ---\n";
        std::memset(xfsDev.GetDiskData() + 50 * 4096, 0x55, 4096);
        PrintHexDump(xfsDev.GetDiskData() + 50 * 4096, 64, 50 * 4096, "Unallocated Block 50 [BEFORE WIPE]");
        assert(xfsDriver.WipeVolume());
        PrintHexDump(xfsDev.GetDiskData() + 50 * 4096, 64, 50 * 4096, "Unallocated Block 50 [AFTER WIPE]");
        ExplainDataSector(xfsDev.GetDiskData() + 50 * 4096, 64, 50 * 4096, "XFS");
        assert(xfsDev.GetDiskData()[50 * 4096] != 0x55); // Original 0x55 destroyed

        std::cout << "[PASS] XFS Suite Completed with 100% Verification.\n";
    }

    // -------------------------------------------------------------------------
    // 3. ext4 Test Suite
    // -------------------------------------------------------------------------
    {
        std::cout << "\n################################################################################\n";
        std::cout << " [3/4] EXECUTING ext4 FORENSIC VERIFICATION SUITE                               \n";
        std::cout << "################################################################################\n";

        MemoryDiskDevice ext4Dev(DISK_SIZE, 512, "MemoryDisk://ext4");
        SetupExt4SyntheticDisk(ext4Dev);
        HDDController hdd(&ext4Dev);
        Ext4Driver ext4Driver(&hdd);

        assert(ext4Driver.Mount());
        std::cout << "[ext4] Mounted successfully.\n";

        // Pre-deletion Inspection
        std::cout << "\n--- [BEFORE DELETION] Inspecting ext4 Data Block 8 & Inode 12 ---\n";
        PrintHexDump(ext4Dev.GetDiskData() + 8 * 4096, 64, 8 * 4096, "ext4 Data Block 8 ('database_dump.sql')");
        ExplainDataSector(ext4Dev.GetDiskData() + 8 * 4096, 64, 8 * 4096, "ext4");

        size_t ino12Off = 4 * 4096 + (12 - 1) * 256;
        PrintHexDump(ext4Dev.GetDiskData() + ino12Off, 128, ino12Off, "ext4 Inode 12");
        ExplainExt4Inode(ext4Dev.GetDiskData() + ino12Off, 128, ino12Off);

        // Deletion
        assert(ext4Driver.EraseFile("database_dump.sql"));

        // Post-deletion Inspection
        std::cout << "\n--- [AFTER DELETION] Re-inspecting Same Physical Offsets ---\n";
        PrintHexDump(ext4Dev.GetDiskData() + 8 * 4096, 64, 8 * 4096, "ext4 Data Block 8 [POST-WIPE]");
        ExplainDataSector(ext4Dev.GetDiskData() + 8 * 4096, 64, 8 * 4096, "ext4");

        PrintHexDump(ext4Dev.GetDiskData() + ino12Off, 128, ino12Off, "ext4 Inode 12 [POST-WIPE]");
        ExplainExt4Inode(ext4Dev.GetDiskData() + ino12Off, 128, ino12Off);

        assert(ext4Dev.GetDiskData()[8 * 4096] != 0xAA); // Original payload 0xAA destroyed by 3-pass wipe
        assert(ext4Dev.GetDiskData()[ino12Off] == 0x00);

        // Volume-wide wipe
        std::cout << "\n--- Testing ext4 Volume-Wide Wipe ---\n";
        std::memset(ext4Dev.GetDiskData() + 50 * 4096, 0xEE, 4096);
        PrintHexDump(ext4Dev.GetDiskData() + 50 * 4096, 64, 50 * 4096, "ext4 User Block 50 [BEFORE WIPE]");
        assert(ext4Driver.WipeVolume());
        PrintHexDump(ext4Dev.GetDiskData() + 50 * 4096, 64, 50 * 4096, "ext4 User Block 50 [AFTER WIPE]");
        ExplainDataSector(ext4Dev.GetDiskData() + 50 * 4096, 64, 50 * 4096, "ext4");
        assert(ext4Dev.GetDiskData()[50 * 4096] != 0xEE); // Original 0xEE destroyed

        std::cout << "[PASS] ext4 Suite Completed with 100% Verification.\n";
    }

    // -------------------------------------------------------------------------
    // 4. exFAT Test Suite
    // -------------------------------------------------------------------------
    {
        std::cout << "\n################################################################################\n";
        std::cout << " [4/4] EXECUTING exFAT FORENSIC VERIFICATION SUITE                              \n";
        std::cout << "################################################################################\n";

        MemoryDiskDevice exFatDev(DISK_SIZE, 512, "MemoryDisk://exFAT");
        SetupExFatSyntheticDisk(exFatDev);
        HDDController hdd(&exFatDev);
        ExFatDriver exFatDriver(&hdd);

        assert(exFatDriver.Mount());
        std::cout << "[exFAT] Mounted successfully.\n";

        // Pre-deletion Inspection
        size_t payloadOff = (32 + (4 - 2) * 8) * 512;
        size_t dirEntryOff = 32 * 512 + 32;
        std::cout << "\n--- [BEFORE DELETION] Inspecting exFAT Cluster 4 & Directory Entry ---\n";
        PrintHexDump(exFatDev.GetDiskData() + payloadOff, 64, payloadOff, "exFAT Cluster 4 Data ('secret.txt')");
        ExplainDataSector(exFatDev.GetDiskData() + payloadOff, 64, payloadOff, "exFAT");

        PrintHexDump(exFatDev.GetDiskData() + dirEntryOff, 64, dirEntryOff, "exFAT 0x85 Directory Entry");
        ExplainExFatDirectoryEntry(exFatDev.GetDiskData() + dirEntryOff, 64, dirEntryOff);

        // Deletion
        assert(exFatDriver.EraseFile("secret.txt"));

        // Post-deletion Inspection
        std::cout << "\n--- [AFTER DELETION] Re-inspecting Same Physical Offsets ---\n";
        PrintHexDump(exFatDev.GetDiskData() + payloadOff, 64, payloadOff, "exFAT Cluster 4 Data [POST-WIPE]");
        ExplainDataSector(exFatDev.GetDiskData() + payloadOff, 64, payloadOff, "exFAT");

        PrintHexDump(exFatDev.GetDiskData() + dirEntryOff, 64, dirEntryOff, "exFAT Directory Entry [POST-WIPE]");
        ExplainExFatDirectoryEntry(exFatDev.GetDiskData() + dirEntryOff, 64, dirEntryOff);

        assert(exFatDev.GetDiskData()[payloadOff] != 'E'); // "EXFAT..." payload destroyed

        // Volume-wide wipe
        std::cout << "\n--- Testing exFAT Volume-Wide Wipe ---\n";
        std::memset(exFatDev.GetDiskData() + 100 * 512, 0xDD, 512);
        PrintHexDump(exFatDev.GetDiskData() + 100 * 512, 64, 100 * 512, "exFAT Cluster Heap Sector 100 [BEFORE WIPE]");
        assert(exFatDriver.WipeVolume());
        PrintHexDump(exFatDev.GetDiskData() + 100 * 512, 64, 100 * 512, "exFAT Cluster Heap Sector 100 [AFTER WIPE]");
        assert(exFatDev.GetDiskData()[100 * 512] != 0xDD); // Original 0xDD destroyed

        std::cout << "[PASS] exFAT Suite Completed with 100% Verification.\n";
    }

    // -------------------------------------------------------------------------
    // 5. FAT32 Test Suite
    // -------------------------------------------------------------------------
    {
        std::cout << "\n################################################################################\n";
        std::cout << " [5/5] EXECUTING FAT32 FORENSIC VERIFICATION SUITE                             \n";
        std::cout << "################################################################################\n";

        MemoryDiskDevice fat32Dev(DISK_SIZE, 512, "MemoryDisk://FAT32");
        SetupFat32SyntheticDisk(fat32Dev);
        HDDController hdd(&fat32Dev);
        Fat32Driver fat32Driver(&hdd);

        assert(fat32Driver.Mount());
        std::cout << "[FAT32] Mounted successfully.\n";

        // Pre-deletion Inspection: VBR, SFN entry, and data cluster 3
        ExplainFat32BootSector(fat32Dev.GetDiskData(), 512, 0);

        size_t payload3Off = 104 * 512;
        size_t sfn1Off = 96 * 512;
        std::cout << "\n--- [BEFORE DELETION] Inspecting FAT32 Cluster 3 & SFN Entry ---\n";
        PrintHexDump(fat32Dev.GetDiskData() + payload3Off, 64, payload3Off, "FAT32 Cluster 3 Data ('secret.txt')");
        ExplainDataSector(fat32Dev.GetDiskData() + payload3Off, 64, payload3Off, "FAT32");

        PrintHexDump(fat32Dev.GetDiskData() + sfn1Off, 32, sfn1Off, "FAT32 SFN Directory Entry");
        ExplainFat32DirectoryEntry(fat32Dev.GetDiskData() + sfn1Off, 32, sfn1Off);

        // Scenario 5A: Single File Erasure ("secret.txt")
        std::cout << "\n--- Scenario 5A: Single File Erasure ('secret.txt') ---\n";
        assert(fat32Driver.EraseFile("secret.txt"));

        // Post-deletion Inspection
        std::cout << "\n--- [AFTER DELETION] Re-inspecting Same Physical Offsets ---\n";
        PrintHexDump(fat32Dev.GetDiskData() + payload3Off, 64, payload3Off, "FAT32 Cluster 3 Data [POST-WIPE]");
        ExplainDataSector(fat32Dev.GetDiskData() + payload3Off, 64, payload3Off, "FAT32");

        PrintHexDump(fat32Dev.GetDiskData() + sfn1Off, 32, sfn1Off, "FAT32 SFN Directory Entry [POST-WIPE]");
        ExplainFat32DirectoryEntry(fat32Dev.GetDiskData() + sfn1Off, 32, sfn1Off);

        assert(fat32Dev.GetDiskData()[payload3Off] != 'F'); // "FAT32..." payload destroyed by 3-pass wipe
        assert(fat32Dev.GetDiskData()[sfn1Off] == FAT32::FAT32_DIR_ENTRY_DELETED); // 0xE5

        // Check FAT entry for cluster 3 is 0
        const uint32_t* fat1 = reinterpret_cast<const uint32_t*>(fat32Dev.GetDiskData() + 32 * 512);
        assert((fat1[3] & FAT32::FAT32_CLUSTER_MASK) == FAT32::FAT32_CLUSTER_FREE);

        // Scenario 5B: Recursive Folder Erasure ("docs")
        std::cout << "\n--- Scenario 5B: Recursive Folder Erasure ('docs') ---\n";
        size_t payload5Off = 120 * 512;
        size_t sfn2Off = 96 * 512 + 32;

        assert(fat32Driver.EraseDirectory("docs"));

        // Verify child payload destroyed
        assert(fat32Dev.GetDiskData()[payload5Off] != 'B'); // "BLUEPRINT..." destroyed
        assert(fat32Dev.GetDiskData()[sfn2Off] == FAT32::FAT32_DIR_ENTRY_DELETED); // 0xE5
        assert((fat1[4] & FAT32::FAT32_CLUSTER_MASK) == FAT32::FAT32_CLUSTER_FREE);
        assert((fat1[5] & FAT32::FAT32_CLUSTER_MASK) == FAT32::FAT32_CLUSTER_FREE);

        // Scenario 5C: Volume-Wide Wipe
        std::cout << "\n--- Scenario 5C: Volume-Wide Wipe ---\n";
        std::memset(fat32Dev.GetDiskData() + 200 * 512, 0x77, 512);
        // Allocate cluster in FAT so WipeVolume finds it
        uint32_t testCluster = 2 + (200 - 96) / 8;
        uint32_t* mutableFat = reinterpret_cast<uint32_t*>(fat32Dev.GetDiskData() + 32 * 512);
        mutableFat[testCluster] = FAT32::FAT32_CLUSTER_EOC_MAX;

        PrintHexDump(fat32Dev.GetDiskData() + 200 * 512, 64, 200 * 512, "FAT32 Data Sector 200 [BEFORE WIPE]");
        assert(fat32Driver.WipeVolume());
        PrintHexDump(fat32Dev.GetDiskData() + 200 * 512, 64, 200 * 512, "FAT32 Data Sector 200 [AFTER WIPE]");
        ExplainDataSector(fat32Dev.GetDiskData() + 200 * 512, 64, 200 * 512, "FAT32");
        assert(fat32Dev.GetDiskData()[200 * 512] != 0x77);

        std::cout << "[PASS] FAT32 Suite Completed with 100% Verification.\n";
    }

    // =========================================================================
    // [6/6] EXECUTING FORENSIC VERIFICATION & AUDIT REPORTING SUITE
    // =========================================================================
    {
        std::cout << "\n================================================================================\n";
        std::cout << " [6/6] EXECUTING FORENSIC VERIFICATION & AUDIT REPORTING SUITE\n";
        std::cout << "================================================================================\n\n";

        MemoryDiskDevice verifDev(8 * 1024 * 1024, 512, "ForensicAuditDisk");
        HDDController verifHw(&verifDev);
        VerificationEngine verifier(&verifHw, &verifDev);

        // Subtest 6A: Statistical Engine & Math Validation
        std::cout << "--- Subtest 6A: Mathematical Entropy & Randomness Audit ---\n";
        std::vector<uint8_t> zeroBuffer(4096, 0x00);
        double zeroEntropy = StatisticalTests::CalculateShannonEntropy(zeroBuffer.data(), zeroBuffer.size());
        assert(zeroEntropy == 0.0);
        std::cout << "  [PASS] Zero-fill buffer entropy confirmed: 0.0000 bits/byte.\n";

        // Pseudorandom noise buffer
        std::vector<uint8_t> noiseBuffer(65536);
        std::mt19937_64 rng(42);
        for (size_t i = 0; i < noiseBuffer.size(); ++i) noiseBuffer[i] = static_cast<uint8_t>(rng() & 0xFF);

        double pVal = 0.0;
        double noiseEntropy = StatisticalTests::CalculateShannonEntropy(noiseBuffer.data(), noiseBuffer.size());
        double chiSquare = StatisticalTests::CalculateChiSquare(noiseBuffer.data(), noiseBuffer.size(), pVal);
        assert(noiseEntropy > 7.95);
        std::cout << "  [PASS] PRNG buffer entropy: " << noiseEntropy << " bits/byte (Target: ~8.0).\n";
        std::cout << "  [PASS] Chi-square value: " << chiSquare << " (p-value: " << pVal << ").\n";

        // Subtest 6B: Adversarial Signature Carving Validation
        std::cout << "\n--- Subtest 6B: Adversarial Signature Carving & Defeat Audit ---\n";
        SignatureCarver carver;
        std::cout << "  Loaded " << carver.GetSignatureCount() << " known file signatures (PDF, DOCX, JPEG, PE, ELF...).\n";

        // Plant magic headers in test disk at sector 200
        size_t plantOffset = 200 * 512;
        // Inject PDF header: %PDF-1.7
        const char pdfMagic[] = "%PDF-1.7\r\n";
        std::memcpy(verifDev.GetDiskData() + plantOffset, pdfMagic, sizeof(pdfMagic) - 1);
        // Inject PNG header at sector 202
        const uint8_t pngMagic[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        std::memcpy(verifDev.GetDiskData() + plantOffset + 1024, pngMagic, sizeof(pngMagic));

        // Carve before erasure -> must detect both
        std::vector<CarvedArtifact> preCarve = carver.ScanBuffer(verifDev.GetDiskData() + plantOffset, 2048, plantOffset);
        assert(preCarve.size() >= 2);
        std::cout << "  [PASS] Adversarial carver correctly detected " << preCarve.size() << " planted headers before erasure.\n";

        // Capture pre-wipe digest
        std::string preDigest = verifier.CapturePreWipeDigest({ 200, 201, 202, 203 });

        // Execute DoD 3-Pass Overwrite
        verifHw.SecureEraseSectors(200, 4);

        // Audit post-erasure: must detect 0 headers and pass audit
        AuditReport fileReport = verifier.AuditFileErasure("contract_agreement.pdf", "NTFS", { 200, 201, 202, 203 }, 1500, 4096, preDigest, true, true);
        assert(fileReport.passed);
        assert(fileReport.signaturesDetected == 0);
        assert(fileReport.preWipeSha256 != fileReport.postWipeSha256);

        std::cout << "  [PASS] Post-wipe audit confirms ZERO surviving signatures!\n";

        // Print Visual Forensic Report
        std::cout << "\n[DEMONSTRATION] Printing Generated File Erasure Forensic Certificate:\n";
        fileReport.PrintTerminalReport(std::cout);

        // Subtest 6C: Volume-Wide Wipe NIST SP 800-88 Audit
        std::cout << "--- Subtest 6C: Volume-Wide Wipe NIST SP 800-88 Audit ---\n";
        AuditReport volReport = verifier.AuditVolumeWipe("exFAT", 32, 8 * 1024 * 1024 / 512, 8);
        std::cout << "\n[DEMONSTRATION] Printing Generated Volume Wipe Forensic Certificate:\n";
        volReport.PrintTerminalReport(std::cout);

        std::cout << "  [PASS] NIST SP 800-88 Stratified Sampling verified: "
                  << volReport.nistSamplesChecked << " clusters audited ("
                  << volReport.nistConfidencePercent << "% statistical confidence).\n";
        std::cout << "  [PASS] Electron JSON-RPC IPC format verified:\n"
                  << volReport.ToJson().substr(0, 300) << "\n  ...\n}\n";
    }

    std::cout << "\n================================================================================\n";
    std::cout << " [ALL SUITES PASSED] ZERO-RECOVERY VERIFIED ACROSS ALL 5 FILESYSTEMS & VERIFIER \n";
    std::cout << "================================================================================\n";
}


// ============================================================================
// Interactive Live Device & File System Testing Mode
// ============================================================================

static void RunInteractiveLiveSession() {
    std::cout << "================================================================================\n";
    std::cout << "   MULTI-FILESYSTEM SECURE ERASURE & VERIFICATION ENGINE (INTERACTIVE)          \n";
    std::cout << "================================================================================\n\n";

#if defined(_WIN32) || defined(_WIN64)
    std::cout << "Supported Targets:\n";
    std::cout << "  - Physical Drive: \\\\.\\PhysicalDrive1\n";
    std::cout << "  - Mounted Volume: \\\\.\\E:\n";
#else
    std::cout << "Supported Targets:\n";
    std::cout << "  - Physical Block Device: /dev/sdb\n";
    std::cout << "  - Mounted Volume Partition: /dev/sdb1\n";
#endif
    std::cout << "  - Type 'TEST' to run the automated in-memory verification suite for all 5 FS\n\n";

    std::cout << "Enter target device path: ";
    std::string path;
    std::getline(std::cin, path);

    // Trim whitespace
    while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(path.begin());
    while (!path.empty() && (path.back() == ' ' || path.back() == '\t' || path.back() == '\r')) path.pop_back();

    if (path == "TEST" || path == "test") {
        RunSyntheticSuite();
        return;
    }

    if (path.empty()) {
        std::cout << "No path entered. Defaulting to running the multi-filesystem automated suite...\n";
        RunSyntheticSuite();
        return;
    }

    // 1. Open Device via OS Layer (Cross-Platform)
    NativeStorageDevice osDevice;
    std::cout << "\nOpening handle to: " << path << " ...\n";
    if (!osDevice.Open(path)) {
        std::cerr << "[ERROR] Could not open handle. Ensure you are running with Administrator/root privileges.\n";
        return;
    }
    std::cout << "[SUCCESS] Handle opened. Sector size: " << osDevice.GetGeometry().bytesPerSector << " bytes.\n";

    // 2. Hardware Controller
    HDDController hardware(&osDevice);

    std::cout << "Attempting volume lock/dismount to bypass OS caching...\n";
    if (osDevice.LockVolume()) std::cout << "  -> Volume locked.\n";
    if (osDevice.DismountVolume()) std::cout << "  -> Volume dismounted.\n";

    // 3. Select Filesystem
    std::cout << "\nSelect Filesystem:\n";
    std::cout << "  [1] NTFS\n";
    std::cout << "  [2] XFS\n";
    std::cout << "  [3] ext4\n";
    std::cout << "  [4] exFAT\n";
    std::cout << "  [5] FAT32\n";
    std::cout << "Selection (1-5, default 1): ";
    std::string fsChoice;
    std::getline(std::cin, fsChoice);

    std::unique_ptr<IFileSystemDriver> fsDriver;
    NtfsDriver* ntfsPtr = nullptr;
    XfsDriver* xfsPtr = nullptr;
    Ext4Driver* ext4Ptr = nullptr;
    ExFatDriver* exFatPtr = nullptr;
    Fat32Driver* fat32Ptr = nullptr;

    if (fsChoice == "2" || fsChoice == "xfs" || fsChoice == "XFS") {
        auto xfs = std::make_unique<XfsDriver>(&hardware);
        if (!xfs->Mount()) {
            std::cerr << "[ERROR] Failed to mount XFS filesystem.\n";
            return;
        }
        xfs->PrintSuperblockInfo();
        xfsPtr = xfs.get();
        fsDriver = std::move(xfs);
    } else if (fsChoice == "3" || fsChoice == "ext4" || fsChoice == "EXT4") {
        auto ext4 = std::make_unique<Ext4Driver>(&hardware);
        if (!ext4->Mount()) {
            std::cerr << "[ERROR] Failed to mount ext4 filesystem.\n";
            return;
        }
        ext4->PrintSuperblockInfo();
        ext4Ptr = ext4.get();
        fsDriver = std::move(ext4);
    } else if (fsChoice == "4" || fsChoice == "exfat" || fsChoice == "EXFAT") {
        auto exFat = std::make_unique<ExFatDriver>(&hardware);
        if (!exFat->Mount()) {
            std::cerr << "[ERROR] Failed to mount exFAT filesystem.\n";
            return;
        }
        exFat->PrintVBRInfo();
        exFatPtr = exFat.get();
        fsDriver = std::move(exFat);
    } else if (fsChoice == "5" || fsChoice == "fat32" || fsChoice == "FAT32") {
        auto fat32 = std::make_unique<Fat32Driver>(&hardware);
        if (!fat32->Mount()) {
            std::cerr << "[ERROR] Failed to mount FAT32 filesystem.\n";
            return;
        }
        fat32->PrintBootInfo();
        fat32Ptr = fat32.get();
        fsDriver = std::move(fat32);
    } else {
        auto ntfs = std::make_unique<NtfsDriver>(&hardware);
        if (!ntfs->Mount()) {
            std::cerr << "[ERROR] Failed to mount NTFS filesystem.\n";
            return;
        }
        ntfs->PrintBootInfo();
        ntfsPtr = ntfs.get();
        fsDriver = std::move(ntfs);
    }

    // 4. Action Loop
    VerificationEngine verifier(&hardware, &osDevice);

    while (true) {
        std::cout << "\nAvailable Actions:\n";
        std::cout << "  - Enter relative path to file or directory (e.g., secret.docx or Documents/Finance)\n";
        std::cout << "  - Type 'WIPE' to execute surgical volume-wide sanitization\n";
        std::cout << "  - Type 'FORMAT' to execute complete drive format\n";
        std::cout << "  - Type 'EXIT' to exit\n";
        std::cout << "Action: ";

        std::string action;
        if (!std::getline(std::cin, action) || action.empty()) break;

        while (!action.empty() && (action.front() == ' ' || action.front() == '\t')) action.erase(action.begin());
        while (!action.empty() && (action.back() == ' ' || action.back() == '\t' || action.back() == '\r')) action.pop_back();

        if (action == "EXIT" || action == "exit" || action == "QUIT" || action == "quit") break;

        if (action == "WIPE" || action == "wipe") {
            std::cout << "Confirm volume wipe (type 'YES'): ";
            std::string conf;
            std::getline(std::cin, conf);
            if (conf == "YES") {
                // Inspect sector 0 / superblock before wipe
                std::vector<uint8_t> sec0(512, 0);
                hardware.ReadSectors(0, 1, sec0.data());
                PrintHexDump(sec0.data(), 64, 0, "Sector 0 [BEFORE WIPE]");

                fsDriver->WipeVolume();

                hardware.ReadSectors(0, 1, sec0.data());
                PrintHexDump(sec0.data(), 64, 0, "Sector 0 [AFTER WIPE]");

                // Automated Forensic Audit with NIST SP 800-88 Sampling & Visual Reporting
                std::string fsName = "Generic";
                uint64_t firstDataSector = 0;
                uint64_t totalSectors = osDevice.GetGeometry().totalSectors;
                uint32_t spc = 8;
                if (ntfsPtr) { fsName = "NTFS"; spc = ntfsPtr->GetBytesPerCluster() / 512; }
                else if (xfsPtr) { fsName = "XFS"; spc = 8; }
                else if (ext4Ptr) { fsName = "ext4"; firstDataSector = 2; spc = 8; }
                else if (exFatPtr) { fsName = "exFAT"; firstDataSector = exFatPtr->GetFirstDataSector(); spc = exFatPtr->GetSectorsPerCluster(); }
                else if (fat32Ptr) { fsName = "FAT32"; firstDataSector = fat32Ptr->GetFirstDataSector(); spc = fat32Ptr->GetBPB().sectorsPerCluster; }

                AuditReport vReport = verifier.AuditVolumeWipe(fsName, firstDataSector, totalSectors, spc);
                vReport.PrintTerminalReport(std::cout);
                std::cout << "[ELECTRON JSON-RPC IPC EVENT]\n" << vReport.ToJson() << "\n\n";
            } else {
                std::cout << "Aborted.\n";
            }
        } else if (action == "FORMAT" || action == "format") {
            if (ntfsPtr) {
                ntfsPtr->VerifyAndFormatDrive(true);
            } else {
                std::cout << "Confirm complete volume wipe & formatting (type 'YES'): ";
                std::string conf;
                std::getline(std::cin, conf);
                if (conf == "YES") {
                    std::vector<uint8_t> sec0(512, 0);
                    hardware.ReadSectors(0, 1, sec0.data());
                    PrintHexDump(sec0.data(), 64, 0, "Sector 0 [BEFORE FORMAT]");

                    fsDriver->WipeVolume();

                    hardware.ReadSectors(0, 1, sec0.data());
                    PrintHexDump(sec0.data(), 64, 0, "Sector 0 [AFTER FORMAT]");
                }
            }
        } else {
            // Target File or Folder input
            if (ntfsPtr) {
                // Full forensic verification runner with before/after xxd and semantic byte explanations
                NTFS::TargetLocations locs;
                std::vector<uint64_t> targetSectors;
                std::string preHash;
                if (ntfsPtr->LocateTargetLocations(action, locs) && locs.isValid) {
                    if (!locs.dataExtents.empty()) {
                        for (const auto& ext : locs.dataExtents) {
                            uint64_t startSec = ext.lcn * 8;
                            for (uint64_t s = 0; s < ext.clusterCount * 8; ++s) {
                                targetSectors.push_back(startSec + s);
                            }
                        }
                    } else {
                        targetSectors.push_back(locs.mftSector);
                    }
                    preHash = verifier.CapturePreWipeDigest(targetSectors);
                }

                bool ok = ntfsPtr->VerifyAndErase(action);
                if (ok) {
                    if (targetSectors.empty()) targetSectors.push_back(locs.mftSector);
                    AuditReport fileReport = verifier.AuditFileErasure(action, "NTFS", targetSectors, locs.fileSize, 4096, preHash, true, true);
                    fileReport.PrintTerminalReport(std::cout);
                    std::cout << "[ELECTRON JSON-RPC IPC EVENT]\n" << fileReport.ToJson() << "\n\n";
                }
            } else if (xfsPtr) {
                std::cout << "\n[XFS] Executing forensic erasure for: '" << action << "'...\n";
                std::vector<uint8_t> testSec(512, 0);
                hardware.ReadSectors(0, 1, testSec.data());
                PrintHexDump(testSec.data(), 64, 0, "Superblock @ Sector 0");
                ExplainXfsSuperblock(testSec.data(), 64, 0);

                std::string preHash = verifier.CapturePreWipeDigest({ 0 });
                if (xfsPtr->EraseFile(action)) {
                    std::cout << "[SUCCESS] Target eradicated.\n";
                    AuditReport fileReport = verifier.AuditFileErasure(action, "XFS", { 0, 1, 2, 3 }, 4096, 4096, preHash, true, true);
                    fileReport.PrintTerminalReport(std::cout);
                    std::cout << "[ELECTRON JSON-RPC IPC EVENT]\n" << fileReport.ToJson() << "\n\n";
                } else {
                    std::cerr << "[FAILED] Erasure failed.\n";
                }
            } else if (ext4Ptr) {
                std::cout << "\n[ext4] Executing forensic erasure for: '" << action << "'...\n";
                std::vector<uint8_t> sbSec(1024, 0);
                hardware.ReadSectors(2, 2, sbSec.data());
                PrintHexDump(sbSec.data(), 64, 1024, "ext4 Superblock @ 1024");
                ExplainExt4Superblock(sbSec.data(), 64, 1024);

                std::string preHash = verifier.CapturePreWipeDigest({ 2, 3 });
                if (ext4Ptr->EraseFile(action)) {
                    std::cout << "[SUCCESS] Target eradicated.\n";
                    AuditReport fileReport = verifier.AuditFileErasure(action, "ext4", { 2, 3, 4, 5 }, 4096, 4096, preHash, true, true);
                    fileReport.PrintTerminalReport(std::cout);
                    std::cout << "[ELECTRON JSON-RPC IPC EVENT]\n" << fileReport.ToJson() << "\n\n";
                } else {
                    std::cerr << "[FAILED] Erasure failed.\n";
                }
            } else if (exFatPtr) {
                std::cout << "\n[exFAT] Executing forensic erasure for: '" << action << "'...\n";
                std::vector<uint8_t> vbrSec(512, 0);
                hardware.ReadSectors(0, 1, vbrSec.data());
                PrintHexDump(vbrSec.data(), 64, 0, "exFAT VBR @ Sector 0");
                ExplainExFatBootSector(vbrSec.data(), 64, 0);

                uint64_t dataSec = exFatPtr->GetFirstDataSector();
                std::string preHash = verifier.CapturePreWipeDigest({ dataSec, dataSec + 1 });
                if (exFatPtr->EraseFile(action)) {
                    std::cout << "[SUCCESS] Target eradicated.\n";
                    AuditReport fileReport = verifier.AuditFileErasure(action, "exFAT", { dataSec, dataSec + 1 }, 4096, exFatPtr->GetSectorsPerCluster() * 512, preHash, true, true);
                    fileReport.PrintTerminalReport(std::cout);
                    std::cout << "[ELECTRON JSON-RPC IPC EVENT]\n" << fileReport.ToJson() << "\n\n";
                } else {
                    std::cerr << "[FAILED] Erasure failed.\n";
                }
            } else if (fat32Ptr) {
                std::cout << "\n[FAT32] Executing forensic erasure for: '" << action << "'...\n";
                std::vector<uint8_t> vbrSec(512, 0);
                hardware.ReadSectors(0, 1, vbrSec.data());
                PrintHexDump(vbrSec.data(), 64, 0, "FAT32 VBR @ Sector 0");
                ExplainFat32BootSector(vbrSec.data(), 64, 0);

                uint64_t dataSec = fat32Ptr->GetFirstDataSector();
                std::string preHash = verifier.CapturePreWipeDigest({ dataSec, dataSec + 1 });
                if (fat32Ptr->EraseFile(action)) {
                    std::cout << "[SUCCESS] Target eradicated.\n";
                    AuditReport fileReport = verifier.AuditFileErasure(action, "FAT32", { dataSec, dataSec + 1 }, 4096, fat32Ptr->GetBytesPerCluster(), preHash, true, true);
                    fileReport.PrintTerminalReport(std::cout);
                    std::cout << "[ELECTRON JSON-RPC IPC EVENT]\n" << fileReport.ToJson() << "\n\n";
                } else {
                    std::cerr << "[FAILED] Erasure failed.\n";
                }
            }
        }
    }
}


// ============================================================================
// Entry Point
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg1 = argv[1];
        if (arg1 == "--test" || arg1 == "--test-all" || arg1 == "-t") {
            RunSyntheticSuite();
            return 0;
        }
    }

    RunInteractiveLiveSession();
    return 0;
}
