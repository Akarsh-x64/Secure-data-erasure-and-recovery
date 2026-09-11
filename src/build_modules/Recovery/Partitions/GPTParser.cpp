#include "GPTParser.h"

#include <cstring>
#include <algorithm>

namespace Recovery {
namespace Partitions {

// GPT constants
static constexpr uint32_t GPT_HEADER_MIN_SIZE   = 92;      // Minimum GPT header size
static constexpr uint32_t GPT_ENTRY_MIN_SIZE    = 128;     // Standard GPT entry size
static constexpr uint32_t GPT_NAME_BYTES        = 72;      // UTF-16LE name field in entry

// "EFI PART" signature (8 bytes)
static constexpr uint8_t GPT_SIGNATURE[8] = {
    0x45, 0x46, 0x49, 0x20,  // "EFI "
    0x50, 0x41, 0x52, 0x54   // "PART"
};

// Well-known GPT partition type GUIDs (stored in mixed-endian format as on disk)
// Format: first 3 components are little-endian, last 2 are big-endian
//
// Unused/Empty:             00000000-0000-0000-0000-000000000000
// EFI System:               C12A7328-F81F-11D2-BA4B-00A0C93EC93B
// Microsoft Basic Data:     EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
// Microsoft Reserved:       E3C9E316-0B5C-4DB8-817D-F92DF00215AE
// Linux Filesystem:         0FC63DAF-8483-4772-8E79-3D69D8477DE4
// Linux Swap:               0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
// Linux LVM:                E6D6D379-F507-44C2-A23C-238F2A3DF928
// Apple HFS+:               48465300-0000-11AA-AA11-00306543ECAC
// Apple APFS:               7C3457EF-0000-11AA-AA11-00306543ECAC

// Mixed-endian GUID as stored on disk for comparison
// (the first 3 fields are little-endian, last 2 are big-endian)
struct KnownGUID {
    uint8_t     bytes[16];
    const char* description;
};

// GUIDs as they appear in raw bytes on disk (mixed-endian)
static const KnownGUID KNOWN_GUIDS[] = {
    // EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    {{ 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
       0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B }, "EFI System Partition"},

    // Microsoft Basic Data: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
    {{ 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
       0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 }, "Microsoft Basic Data"},

    // Microsoft Reserved: E3C9E316-0B5C-4DB8-817D-F92DF00215AE
    {{ 0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
       0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE }, "Microsoft Reserved"},

    // Windows Recovery: DE94BBA4-06D1-4D40-A16A-BFD50179D6AC
    {{ 0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D,
       0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC }, "Windows Recovery Environment"},

    // Linux Filesystem: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
    {{ 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
       0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 }, "Linux Filesystem"},

    // Linux Swap: 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
    {{ 0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43,
       0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F }, "Linux Swap"},

    // Linux LVM: E6D6D379-F507-44C2-A23C-238F2A3DF928
    {{ 0x79, 0xD3, 0xD6, 0xE6, 0x07, 0xF5, 0xC2, 0x44,
       0xA2, 0x3C, 0x23, 0x8F, 0x2A, 0x3D, 0xF9, 0x28 }, "Linux LVM"},

    // Apple HFS+: 48465300-0000-11AA-AA11-00306543ECAC
    {{ 0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xAA, 0x11,
       0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC }, "Apple HFS+"},

    // Apple APFS: 7C3457EF-0000-11AA-AA11-00306543ECAC
    {{ 0xEF, 0x57, 0x34, 0x7C, 0x00, 0x00, 0xAA, 0x11,
       0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC }, "Apple APFS"},
};

static constexpr size_t KNOWN_GUID_COUNT = sizeof(KNOWN_GUIDS) / sizeof(KNOWN_GUIDS[0]);

// Reads a little-endian uint32 from a byte buffer
static uint32_t ReadLE32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
         | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16)
         | (static_cast<uint32_t>(data[3]) << 24);
}

// Reads a little-endian uint64 from a byte buffer
static uint64_t ReadLE64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0])
         | (static_cast<uint64_t>(data[1]) << 8)
         | (static_cast<uint64_t>(data[2]) << 16)
         | (static_cast<uint64_t>(data[3]) << 24)
         | (static_cast<uint64_t>(data[4]) << 32)
         | (static_cast<uint64_t>(data[5]) << 40)
         | (static_cast<uint64_t>(data[6]) << 48)
         | (static_cast<uint64_t>(data[7]) << 56);
}

bool GPTParser::IsZeroGUID(const uint8_t guid[16]) {
    for (int i = 0; i < 16; ++i) {
        if (guid[i] != 0) return false;
    }
    return true;
}

std::string GPTParser::UTF16LEToUTF8(const uint8_t* data, uint32_t byteCount) {
    std::string result;
    result.reserve(byteCount / 2);

    for (uint32_t i = 0; i + 1 < byteCount; i += 2) {
        // Read UTF-16LE code unit
        uint16_t codeUnit = static_cast<uint16_t>(data[i])
                          | (static_cast<uint16_t>(data[i + 1]) << 8);

        if (codeUnit == 0) {
            break; // Null terminator
        }

        // BMP range conversion to UTF-8
        if (codeUnit < 0x80) {
            result.push_back(static_cast<char>(codeUnit));
        } else if (codeUnit < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (codeUnit >> 6)));
            result.push_back(static_cast<char>(0x80 | (codeUnit & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (codeUnit >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codeUnit >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codeUnit & 0x3F)));
        }
    }

    return result;
}

std::string GPTParser::GetTypeDescription(const uint8_t guid[16]) {
    for (size_t i = 0; i < KNOWN_GUID_COUNT; ++i) {
        if (std::memcmp(guid, KNOWN_GUIDS[i].bytes, 16) == 0) {
            return KNOWN_GUIDS[i].description;
        }
    }
    return "Unknown GPT Type";
}

bool GPTParser::CanParse(Core::ByteReader& reader) {
    // GPT header is at LBA 1. For a standard 512-byte sector disk,
    // that's byte offset 512. But we need to handle other sector sizes too.
    // However, by convention GPT header is always at byte offset equal to
    // one sector size. For detection, we check at offset 512 first (the
    // minimum possible), since we may not know the sector size yet.

    uint64_t storageSize = reader.GetSize();

    // Need at least space for protective MBR + GPT header
    if (storageSize < 512 + GPT_HEADER_MIN_SIZE) {
        return false;
    }

    // Check for GPT signature at LBA 1 (byte offset 512 for 512-byte sectors)
    uint8_t sig[8];
    if (!reader.ReadBytes(512, 8, sig)) {
        return false;
    }

    return (std::memcmp(sig, GPT_SIGNATURE, 8) == 0);
}

std::vector<Core::PartitionInfo>
GPTParser::Parse(Core::ByteReader& reader, uint32_t sectorSize) {
    std::vector<Core::PartitionInfo> results;

    if (sectorSize == 0) {
        return results;
    }

    uint64_t storageSize = reader.GetSize();

    // GPT header starts at LBA 1
    uint64_t headerOffset = static_cast<uint64_t>(sectorSize);  // LBA 1

    // Ensure we can read the header
    if (storageSize < headerOffset + GPT_HEADER_MIN_SIZE) {
        return results;
    }

    // Read the GPT header (92 bytes minimum)
    uint8_t header[GPT_HEADER_MIN_SIZE];
    if (!reader.ReadBytes(headerOffset, GPT_HEADER_MIN_SIZE, header)) {
        return results;
    }

    // Validate signature
    if (std::memcmp(header, GPT_SIGNATURE, 8) != 0) {
        return results;
    }

    // Parse header fields
    // uint32_t revision      = ReadLE32(header + 8);      // Not used in V1
    // uint32_t headerSize    = ReadLE32(header + 12);      // Not used in V1
    // uint32_t headerCRC32   = ReadLE32(header + 16);      // Not validated in V1
    // uint64_t currentLBA    = ReadLE64(header + 24);      // Not used in V1
    // uint64_t backupLBA     = ReadLE64(header + 32);      // Not used in V1
    // uint64_t firstUsable   = ReadLE64(header + 40);      // Not used in V1
    // uint64_t lastUsable    = ReadLE64(header + 48);      // Not used in V1
    // Disk GUID at header + 56 (16 bytes)                   // Not used in V1

    uint64_t entryArrayLBA   = ReadLE64(header + 72);  // Partition entry array starting LBA
    uint32_t numEntries      = ReadLE32(header + 80);  // Number of partition entries
    uint32_t entrySize       = ReadLE32(header + 84);  // Size of each partition entry

    // Sanity checks
    if (entrySize < GPT_ENTRY_MIN_SIZE) {
        return results;  // Entry too small to contain required fields
    }

    if (numEntries == 0) {
        return results;
    }

    // Cap at a reasonable maximum to prevent absurd allocations
    // (GPT spec typically uses 128 entries, but we allow up to 1024)
    if (numEntries > 1024) {
        numEntries = 1024;
    }

    // Calculate the byte offset of the partition entry array
    uint64_t entryArrayOffset = entryArrayLBA * static_cast<uint64_t>(sectorSize);

    // Read partition entries one at a time
    uint32_t validIndex = 0;

    for (uint32_t i = 0; i < numEntries; ++i) {
        uint64_t entryOffset = entryArrayOffset + (static_cast<uint64_t>(i) * entrySize);

        // Bounds check: ensure we can read the full entry
        if (entryOffset + entrySize > storageSize) {
            break;  // Truncated — stop parsing, don't crash
        }

        // Read the entry
        std::vector<uint8_t> entryData(entrySize);
        if (!reader.ReadBytes(entryOffset, entrySize, entryData.data())) {
            break;
        }

        // Check if this entry is empty (all-zero type GUID)
        const uint8_t* typeGUID = entryData.data();
        if (IsZeroGUID(typeGUID)) {
            continue;  // Skip empty entries
        }

        // Parse the entry fields
        const uint8_t* uniqueGUID = entryData.data() + 16;
        uint64_t firstLBA   = ReadLE64(entryData.data() + 32);
        uint64_t lastLBA    = ReadLE64(entryData.data() + 40);
        uint64_t attributes = ReadLE64(entryData.data() + 48);

        // Sanity: firstLBA must be <= lastLBA
        if (firstLBA > lastLBA) {
            continue;  // Corrupt entry, skip
        }

        // Calculate sector count
        uint64_t sectorCount = lastLBA - firstLBA + 1;

        Core::PartitionInfo info;
        info.index       = validIndex++;
        info.scheme      = Core::PartitionScheme::GPT;
        info.startLBA    = firstLBA;
        info.sectorCount = sectorCount;
        info.startOffset = firstLBA * static_cast<uint64_t>(sectorSize);
        info.sizeBytes   = sectorCount * static_cast<uint64_t>(sectorSize);

        // GPT-specific fields
        std::memcpy(info.typeGUID, typeGUID, 16);
        std::memcpy(info.uniqueGUID, uniqueGUID, 16);
        info.gptAttributes = attributes;

        // Extract partition name (UTF-16LE at offset 56, up to 72 bytes)
        uint32_t nameBytes = std::min(static_cast<uint32_t>(GPT_NAME_BYTES),
                                      entrySize - 56);
        info.gptName = UTF16LEToUTF8(entryData.data() + 56, nameBytes);

        // MBR fields not applicable
        info.mbrTypeByte = 0;
        info.bootable    = false;

        // Human-readable description from type GUID
        info.description = GetTypeDescription(typeGUID);

        results.push_back(std::move(info));
    }

    return results;
}

} // namespace Partitions
} // namespace Recovery
