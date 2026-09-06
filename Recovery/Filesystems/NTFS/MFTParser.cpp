#include "MFTParser.h"
#include "DataRunParser.h"

#include <cstring>
#include <algorithm>

namespace Recovery {
namespace Filesystems {
namespace NTFS {

MFTParser::MFTParser(Core::ByteReader& reader,
                     uint64_t partitionOffset,
                     uint32_t bytesPerMftRecord,
                     uint32_t bytesPerCluster,
                     uint32_t bytesPerSector)
    : m_reader(reader)
    , m_partitionOffset(partitionOffset)
    , m_bytesPerMftRecord(bytesPerMftRecord)
    , m_bytesPerCluster(bytesPerCluster)
    , m_bytesPerSector(bytesPerSector)
{
}

// ---- UTF-16LE to UTF-8 conversion ----

std::string MFTParser::Utf16ToUtf8(const uint8_t* utf16Data, uint32_t charCount) {
    std::string result;
    result.reserve(charCount);

    for (uint32_t i = 0; i < charCount; ++i) {
        // Read UTF-16LE code unit
        uint16_t codeUnit = static_cast<uint16_t>(utf16Data[i * 2])
                          | (static_cast<uint16_t>(utf16Data[i * 2 + 1]) << 8);

        if (codeUnit < 0x80) {
            result += static_cast<char>(codeUnit);
        } else if (codeUnit < 0x800) {
            result += static_cast<char>(0xC0 | (codeUnit >> 6));
            result += static_cast<char>(0x80 | (codeUnit & 0x3F));
        } else {
            // BMP character (surrogate pairs not handled — matches Phase 2 GPT limitation)
            result += static_cast<char>(0xE0 | (codeUnit >> 12));
            result += static_cast<char>(0x80 | ((codeUnit >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codeUnit & 0x3F));
        }
    }

    return result;
}

// ---- Fixup array ----

bool MFTParser::ApplyFixups(uint8_t* record, uint32_t recordSize) {
    auto* header = reinterpret_cast<MFTEntryHeader*>(record);

    uint16_t fixupOffset = header->fixupArrayOffset;
    uint16_t fixupCount  = header->fixupArrayCount;

    // fixupCount includes the signature word, so actual replacements = fixupCount - 1
    if (fixupCount < 2) return true;  // No fixups needed
    if (fixupOffset + fixupCount * 2 > recordSize) return false;

    // The signature value (all sector-end words should match this)
    uint16_t signature = *reinterpret_cast<uint16_t*>(record + fixupOffset);

    // For each sector in the record, validate and replace the last 2 bytes
    uint32_t sectorsInRecord = recordSize / m_bytesPerSector;
    uint32_t expectedFixups = sectorsInRecord;

    if (fixupCount - 1 < expectedFixups)
        expectedFixups = fixupCount - 1;

    for (uint32_t i = 0; i < expectedFixups; ++i) {
        uint32_t sectorEndOffset = (i + 1) * m_bytesPerSector - 2;

        if (sectorEndOffset + 2 > recordSize)
            break;

        // Validate: the last 2 bytes of this sector should match the signature
        uint16_t* sectorEnd = reinterpret_cast<uint16_t*>(record + sectorEndOffset);
        if (*sectorEnd != signature)
            return false;  // Fixup mismatch — record may be corrupt

        // Replace with the original value from the fixup array
        uint16_t originalValue = *reinterpret_cast<uint16_t*>(
            record + fixupOffset + (i + 1) * 2);
        *sectorEnd = originalValue;
    }

    return true;
}

// ---- Attribute walking ----

bool MFTParser::ParseAttributes(const uint8_t* record, uint32_t recordSize,
                                uint32_t firstAttrOffset, MFTFileInfo& info)
{
    uint32_t offset = firstAttrOffset;
    bool hasWin32Name = false;

    while (offset + sizeof(AttributeHeader) <= recordSize) {
        auto* attrHeader = reinterpret_cast<const AttributeHeader*>(record + offset);

        // End of attribute list
        if (attrHeader->type == ATTR_TYPE_END || attrHeader->type == 0)
            break;

        // Sanity: attribute length must be positive and aligned
        if (attrHeader->length == 0 || attrHeader->length > recordSize - offset)
            break;

        // Skip named streams (alternate data streams) for now — only process unnamed $DATA
        bool isUnnamed = (attrHeader->nameLength == 0);

        switch (attrHeader->type) {
            case ATTR_TYPE_FILE_NAME: {
                if (attrHeader->nonResident != 0) break;  // $FILE_NAME is always resident

                auto* resHdr = reinterpret_cast<const ResidentAttrHeader*>(record + offset);
                uint32_t valueStart = offset + resHdr->valueOffset;
                uint32_t valueLen   = resHdr->valueLength;

                if (valueStart + valueLen > recordSize) break;
                if (valueLen < sizeof(FileNameAttribute)) break;

                auto* fnAttr = reinterpret_cast<const FileNameAttribute*>(record + valueStart);

                // Check that filename data is within bounds
                uint32_t nameDataLen = fnAttr->filenameLengthChars * 2;
                if (valueStart + sizeof(FileNameAttribute) + nameDataLen > recordSize) break;

                uint8_t ns = fnAttr->filenameNamespace;

                // Prefer Win32 or Win32+DOS name over DOS-only
                // If we already have a Win32 name, don't overwrite with DOS
                if (ns == FILENAME_NAMESPACE_DOS && hasWin32Name)
                    break;

                const uint8_t* nameData = record + valueStart + sizeof(FileNameAttribute);
                info.filename = Utf16ToUtf8(nameData, fnAttr->filenameLengthChars);

                info.parentMftIndex = ParentRefToMftIndex(fnAttr->parentDirReference);
                info.createdTime    = NtfsTimeToUnix(fnAttr->creationTime);
                info.modifiedTime   = NtfsTimeToUnix(fnAttr->modificationTime);
                info.accessedTime   = NtfsTimeToUnix(fnAttr->readTime);

                if (ns == FILENAME_NAMESPACE_WIN32 || ns == FILENAME_NAMESPACE_WIN32_AND_DOS)
                    hasWin32Name = true;

                break;
            }

            case ATTR_TYPE_DATA: {
                // Only process the unnamed (default) $DATA stream
                if (!isUnnamed) break;

                if (attrHeader->nonResident == 0) {
                    // Resident data — inline within the MFT record
                    auto* resHdr = reinterpret_cast<const ResidentAttrHeader*>(record + offset);
                    uint32_t valueStart = offset + resHdr->valueOffset;
                    uint32_t valueLen   = resHdr->valueLength;

                    if (valueStart + valueLen > recordSize) break;

                    info.hasResidentData = true;
                    info.fileSize = valueLen;
                    info.residentData.assign(
                        record + valueStart,
                        record + valueStart + valueLen);
                } else {
                    // Non-resident data — decode data runs
                    auto* nrHdr = reinterpret_cast<const NonResidentAttrHeader*>(record + offset);

                    info.hasResidentData = false;
                    info.fileSize = nrHdr->realSize;

                    uint32_t runStart = offset + nrHdr->dataRunOffset;
                    uint32_t runMaxLen = attrHeader->length - nrHdr->dataRunOffset;

                    if (runStart >= recordSize) break;
                    if (runStart + runMaxLen > recordSize)
                        runMaxLen = recordSize - runStart;

                    auto runs = DataRunParser::Parse(
                        record + runStart, runMaxLen, m_bytesPerCluster);

                    // Data runs give partition-relative cluster offsets.
                    // Convert to absolute byte offsets by adding partitionOffset.
                    for (auto& run : runs) {
                        if (run.offset != 0) {  // Skip sparse runs
                            run.offset += m_partitionOffset;
                        }
                    }

                    info.dataRanges = std::move(runs);
                }
                break;
            }

            default:
                break;
        }

        offset += attrHeader->length;
    }

    return true;
}

// ---- Main entry parser ----

bool MFTParser::ParseEntry(uint64_t mftByteOffset, uint64_t mftIndex, MFTFileInfo& info) {
    // Read the raw MFT record
    std::vector<uint8_t> recordBuf(m_bytesPerMftRecord);
    if (!m_reader.ReadBytes(mftByteOffset, m_bytesPerMftRecord, recordBuf.data()))
        return false;

    auto* header = reinterpret_cast<MFTEntryHeader*>(recordBuf.data());

    // Validate signature
    if (std::memcmp(header->signature, "FILE", 4) != 0)
        return false;

    // Apply fixup array
    if (!ApplyFixups(recordBuf.data(), m_bytesPerMftRecord))
        return false;

    // Populate basic info from header
    info.mftIndex    = mftIndex;
    info.inUse       = (header->flags & MFT_RECORD_IN_USE) != 0;
    info.isDirectory = (header->flags & MFT_RECORD_IS_DIR) != 0;

    // Skip extension records (baseRecordRef != 0) — they extend a base record
    // For V1 we only process base records
    if (header->baseRecordRef != 0)
        return false;

    // Parse attributes
    uint32_t firstAttr = header->firstAttributeOffset;
    if (firstAttr >= m_bytesPerMftRecord)
        return false;

    return ParseAttributes(recordBuf.data(), m_bytesPerMftRecord, firstAttr, info);
}

} // namespace NTFS
} // namespace Filesystems
} // namespace Recovery
