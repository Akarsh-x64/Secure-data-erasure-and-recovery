#include "MBRParser.h"

#include <cstring>
#include <sstream>
#include <iomanip>

namespace Recovery {
namespace Partitions {

// MBR constants
static constexpr uint64_t MBR_OFFSET            = 0;
static constexpr uint32_t MBR_SIZE              = 512;
static constexpr uint32_t MBR_PARTITION_TABLE    = 0x1BE;  // Byte offset of first partition entry
static constexpr uint32_t MBR_ENTRY_SIZE         = 16;     // Bytes per partition entry
static constexpr uint32_t MBR_ENTRY_COUNT        = 4;      // Maximum primary partitions
static constexpr uint32_t MBR_SIGNATURE_OFFSET   = 510;    // Byte offset of boot signature
static constexpr uint8_t  MBR_SIG_BYTE0          = 0x55;
static constexpr uint8_t  MBR_SIG_BYTE1          = 0xAA;
static constexpr uint8_t  MBR_BOOT_INDICATOR     = 0x80;
static constexpr uint8_t  MBR_TYPE_EMPTY         = 0x00;
static constexpr uint8_t  MBR_TYPE_PROTECTIVE    = 0xEE;   // GPT protective MBR

// Reads a little-endian uint32 from a byte buffer
static uint32_t ReadLE32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
         | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16)
         | (static_cast<uint32_t>(data[3]) << 24);
}

MBRParser::MBRParser()
    : m_hasProtectiveMBR(false) {}

bool MBRParser::CanParse(Core::ByteReader& reader) {
    // Need at least 512 bytes for an MBR
    if (reader.GetSize() < MBR_SIZE) {
        return false;
    }

    // Read the signature bytes at offset 510-511
    uint8_t sig[2] = {0, 0};
    if (!reader.ReadBytes(MBR_SIGNATURE_OFFSET, 2, sig)) {
        return false;
    }

    return (sig[0] == MBR_SIG_BYTE0 && sig[1] == MBR_SIG_BYTE1);
}

std::vector<Core::PartitionInfo>
MBRParser::Parse(Core::ByteReader& reader, uint32_t sectorSize) {
    std::vector<Core::PartitionInfo> results;
    m_hasProtectiveMBR = false;

    if (sectorSize == 0) {
        return results;
    }

    // Read the entire MBR (first 512 bytes)
    uint8_t mbr[MBR_SIZE];
    std::memset(mbr, 0, sizeof(mbr));

    if (!reader.ReadBytes(MBR_OFFSET, MBR_SIZE, mbr)) {
        return results;
    }

    // Validate signature
    if (mbr[MBR_SIGNATURE_OFFSET] != MBR_SIG_BYTE0 ||
        mbr[MBR_SIGNATURE_OFFSET + 1] != MBR_SIG_BYTE1) {
        return results;
    }

    // Parse the 4 partition entries
    uint32_t validIndex = 0;

    for (uint32_t i = 0; i < MBR_ENTRY_COUNT; ++i) {
        const uint8_t* entry = mbr + MBR_PARTITION_TABLE + (i * MBR_ENTRY_SIZE);

        uint8_t  bootIndicator = entry[0];
        uint8_t  typeByte      = entry[4];
        uint32_t startLBA      = ReadLE32(entry + 8);
        uint32_t numSectors    = ReadLE32(entry + 12);

        // Skip empty partition entries
        if (typeByte == MBR_TYPE_EMPTY) {
            continue;
        }

        // Detect GPT protective MBR
        if (typeByte == MBR_TYPE_PROTECTIVE) {
            m_hasProtectiveMBR = true;
            // Do NOT add this as a normal partition — it's a GPT indicator
            continue;
        }

        Core::PartitionInfo info;
        info.index       = validIndex++;
        info.scheme      = Core::PartitionScheme::MBR;
        info.startLBA    = static_cast<uint64_t>(startLBA);
        info.sectorCount = static_cast<uint64_t>(numSectors);

        // Compute byte offsets using uint64_t to prevent overflow
        // (uint32_t LBA * uint32_t sectorSize could exceed 32 bits)
        info.startOffset = static_cast<uint64_t>(startLBA) * static_cast<uint64_t>(sectorSize);
        info.sizeBytes   = static_cast<uint64_t>(numSectors) * static_cast<uint64_t>(sectorSize);

        info.mbrTypeByte = typeByte;
        info.bootable    = (bootIndicator == MBR_BOOT_INDICATOR);
        info.description = GetTypeDescription(typeByte);

        results.push_back(std::move(info));
    }

    return results;
}

std::string MBRParser::GetTypeDescription(uint8_t typeByte) {
    // Common MBR partition type codes
    // Reference: https://en.wikipedia.org/wiki/Partition_type
    switch (typeByte) {
        case 0x01: return "FAT12 (0x01)";
        case 0x04: return "FAT16 <32MB (0x04)";
        case 0x05: return "Extended (CHS) (0x05)";
        case 0x06: return "FAT16 (0x06)";
        case 0x07: return "NTFS/exFAT/HPFS (0x07)";
        case 0x0B: return "FAT32 (CHS) (0x0B)";
        case 0x0C: return "FAT32 (LBA) (0x0C)";
        case 0x0E: return "FAT16 (LBA) (0x0E)";
        case 0x0F: return "Extended (LBA) (0x0F)";
        case 0x11: return "Hidden FAT12 (0x11)";
        case 0x14: return "Hidden FAT16 <32MB (0x14)";
        case 0x16: return "Hidden FAT16 (0x16)";
        case 0x17: return "Hidden NTFS/HPFS (0x17)";
        case 0x1B: return "Hidden FAT32 (CHS) (0x1B)";
        case 0x1C: return "Hidden FAT32 (LBA) (0x1C)";
        case 0x1E: return "Hidden FAT16 (LBA) (0x1E)";
        case 0x27: return "Windows Recovery (0x27)";
        case 0x42: return "Dynamic Disk (0x42)";
        case 0x82: return "Linux Swap (0x82)";
        case 0x83: return "Linux (0x83)";
        case 0x85: return "Linux Extended (0x85)";
        case 0x8E: return "Linux LVM (0x8E)";
        case 0xA5: return "FreeBSD (0xA5)";
        case 0xA6: return "OpenBSD (0xA6)";
        case 0xAF: return "macOS HFS+ (0xAF)";
        case 0xEE: return "GPT Protective (0xEE)";
        case 0xEF: return "EFI System (0xEF)";
        case 0xFD: return "Linux RAID (0xFD)";
        default: {
            std::ostringstream oss;
            oss << "Unknown (0x"
                << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
                << static_cast<unsigned>(typeByte) << ")";
            return oss.str();
        }
    }
}

} // namespace Partitions
} // namespace Recovery
