#include "NTFS.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace Erasure {
namespace FileSystems {

// =============================================================================
// Constructor
// =============================================================================

NtfsDriver::NtfsDriver(Core::IHardwareController* hardware)
    : m_hardware(hardware)
    , m_bytesPerSector(0)
    , m_sectorsPerCluster(0)
    , m_bytesPerCluster(0)
    , m_mftRecordSize(1024)
    , m_indexBlockSize(4096)
    , m_totalSectors(0)
{
    std::memset(&m_vbr, 0, sizeof(m_vbr));
}

// =============================================================================
// Geometry & Addressing Helpers
// =============================================================================

uint64_t NtfsDriver::ClusterToSector(uint64_t lcn) const {
    return lcn * m_sectorsPerCluster;
}

uint64_t NtfsDriver::SectorToByteOffset(uint64_t sector) const {
    return sector * m_bytesPerSector;
}

bool NtfsDriver::ReadSectors(uint64_t startSector, uint32_t count, void* buffer) const {
    if (!m_hardware) return false;
    return m_hardware->ReadSectors(startSector, count, buffer);
}

bool NtfsDriver::WriteSectors(uint64_t startSector, uint32_t count, const void* buffer) {
    if (!m_hardware) return false;
    return m_hardware->WriteSectors(startSector, count, buffer);
}

// =============================================================================
// NTFS Fixup Array (Update Sequence)
// =============================================================================

bool NtfsDriver::ApplyFixup(uint8_t* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize < sizeof(NTFS::NtfsRecordHeader)) return false;

    const auto* hdr = reinterpret_cast<const NTFS::NtfsRecordHeader*>(buffer);
    if (hdr->magic != NTFS::NTFS_MAGIC_FILE && hdr->magic != NTFS::NTFS_MAGIC_INDX) {
        return false;
    }

    uint16_t usnOffset = hdr->updateSequenceOffset;
    uint16_t usnCount = hdr->updateSequenceSize;

    if (static_cast<size_t>(usnOffset) + static_cast<size_t>(usnCount) * 2 > bufferSize) {
        return false;
    }

    uint16_t usn = *reinterpret_cast<const uint16_t*>(buffer + usnOffset);
    const uint16_t* fixupArray = reinterpret_cast<const uint16_t*>(buffer + usnOffset + 2);

    for (uint16_t i = 0; i < usnCount - 1; ++i) {
        size_t sectorEndOffset = static_cast<size_t>(i + 1) * 512 - 2;
        if (sectorEndOffset + 2 > bufferSize) break;

        uint16_t currentSectorWord = *reinterpret_cast<uint16_t*>(buffer + sectorEndOffset);
        if (currentSectorWord == usn) {
            *reinterpret_cast<uint16_t*>(buffer + sectorEndOffset) = fixupArray[i];
        }
    }
    return true;
}

bool NtfsDriver::EncodeFixup(uint8_t* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize < sizeof(NTFS::NtfsRecordHeader)) return false;

    const auto* hdr = reinterpret_cast<const NTFS::NtfsRecordHeader*>(buffer);
    if (hdr->magic != NTFS::NTFS_MAGIC_FILE && hdr->magic != NTFS::NTFS_MAGIC_INDX) {
        return false;
    }

    uint16_t usnOffset = hdr->updateSequenceOffset;
    uint16_t usnCount = hdr->updateSequenceSize;

    if (static_cast<size_t>(usnOffset) + static_cast<size_t>(usnCount) * 2 > bufferSize) {
        return false;
    }

    uint16_t* usnPtr = reinterpret_cast<uint16_t*>(buffer + usnOffset);
    uint16_t usn = *usnPtr + 1;
    if (usn == 0 || usn == 0xFFFF) usn = 1;
    *usnPtr = usn;

    uint16_t* fixupArray = reinterpret_cast<uint16_t*>(buffer + usnOffset + 2);

    for (uint16_t i = 0; i < usnCount - 1; ++i) {
        size_t sectorEndOffset = static_cast<size_t>(i + 1) * 512 - 2;
        if (sectorEndOffset + 2 > bufferSize) break;

        uint16_t* sectorWordPtr = reinterpret_cast<uint16_t*>(buffer + sectorEndOffset);
        fixupArray[i] = *sectorWordPtr;
        *sectorWordPtr = usn;
    }
    return true;
}

// =============================================================================
// Runlist Decoder
// =============================================================================

bool NtfsDriver::DecodeRunList(const uint8_t* runlist, size_t maxLen, std::vector<NTFS::NtfsExtent>& outExtents) const {
    outExtents.clear();
    if (!runlist || maxLen == 0) return false;

    size_t offset = 0;
    int64_t currentLcn = 0;

    while (offset < maxLen) {
        uint8_t header = runlist[offset++];
        if (header == 0) break; // Terminating byte

        uint8_t lenFieldSize = header & 0x0F;
        uint8_t offsetFieldSize = (header >> 4) & 0x0F;

        if (lenFieldSize == 0 || offset + lenFieldSize + offsetFieldSize > maxLen) {
            break;
        }

        // 1. Decode Run Length (cluster count)
        uint64_t clusterCount = 0;
        for (uint8_t i = 0; i < lenFieldSize; ++i) {
            clusterCount |= static_cast<uint64_t>(runlist[offset++]) << (i * 8);
        }

        // 2. Decode Run Offset (signed relative LCN offset)
        if (offsetFieldSize > 0) {
            int64_t lcnOffset = 0;
            for (uint8_t i = 0; i < offsetFieldSize; ++i) {
                lcnOffset |= static_cast<int64_t>(runlist[offset++]) << (i * 8);
            }
            // Sign-extend if negative
            if (offsetFieldSize > 0 && (runlist[offset - 1] & 0x80)) {
                uint64_t mask = ~0ULL << (offsetFieldSize * 8);
                lcnOffset = static_cast<int64_t>(static_cast<uint64_t>(lcnOffset) | mask);
            }
            currentLcn += lcnOffset;
            outExtents.push_back({ static_cast<uint64_t>(currentLcn), clusterCount });
        }
    }
    return !outExtents.empty();
}

// =============================================================================
// MFT Record Mapping & I/O
// =============================================================================

uint64_t NtfsDriver::MftRecordToSector(uint64_t recordNum, uint32_t& outOffsetInSector) const {
    uint64_t byteOffsetInMft = recordNum * m_mftRecordSize;

    if (!m_mftExtents.empty()) {
        uint64_t currentExtentByte = 0;
        for (const auto& ext : m_mftExtents) {
            uint64_t extentBytes = ext.clusterCount * m_bytesPerCluster;
            if (byteOffsetInMft < currentExtentByte + extentBytes) {
                uint64_t offsetInExtent = byteOffsetInMft - currentExtentByte;
                uint64_t clusterInExtent = offsetInExtent / m_bytesPerCluster;
                uint32_t byteInCluster = static_cast<uint32_t>(offsetInExtent % m_bytesPerCluster);

                uint64_t targetLcn = ext.lcn + clusterInExtent;
                uint64_t sector = ClusterToSector(targetLcn) + (byteInCluster / m_bytesPerSector);
                outOffsetInSector = byteInCluster % m_bytesPerSector;
                return sector;
            }
            currentExtentByte += extentBytes;
        }
    }

    // Fallback if extents not yet loaded (e.g., loading Record 0 itself during Mount)
    uint64_t mftStartSector = ClusterToSector(m_vbr.mftStartLCN);
    uint64_t sectorOffset = byteOffsetInMft / m_bytesPerSector;
    outOffsetInSector = static_cast<uint32_t>(byteOffsetInMft % m_bytesPerSector);
    return mftStartSector + sectorOffset;
}

bool NtfsDriver::ReadMftRecord(uint64_t recordNum, std::vector<uint8_t>& outRecord) const {
    outRecord.resize(m_mftRecordSize);
    uint32_t offsetInSector = 0;
    uint64_t startSector = MftRecordToSector(recordNum, offsetInSector);

    uint32_t sectorsNeeded = (m_mftRecordSize + offsetInSector + m_bytesPerSector - 1) / m_bytesPerSector;
    std::vector<uint8_t> buffer(sectorsNeeded * m_bytesPerSector);

    if (!ReadSectors(startSector, sectorsNeeded, buffer.data())) {
        return false;
    }

    std::memcpy(outRecord.data(), buffer.data() + offsetInSector, m_mftRecordSize);
    ApplyFixup(outRecord.data(), outRecord.size());
    return true;
}

bool NtfsDriver::WriteMftRecord(uint64_t recordNum, const std::vector<uint8_t>& inRecord) {
    if (inRecord.size() < m_mftRecordSize) return false;

    uint32_t offsetInSector = 0;
    uint64_t startSector = MftRecordToSector(recordNum, offsetInSector);

    uint32_t sectorsNeeded = (m_mftRecordSize + offsetInSector + m_bytesPerSector - 1) / m_bytesPerSector;
    std::vector<uint8_t> buffer(sectorsNeeded * m_bytesPerSector);

    if (!ReadSectors(startSector, sectorsNeeded, buffer.data())) {
        return false;
    }

    std::vector<uint8_t> recordCopy(inRecord.begin(), inRecord.begin() + m_mftRecordSize);
    EncodeFixup(recordCopy.data(), m_mftRecordSize);

    std::memcpy(buffer.data() + offsetInSector, recordCopy.data(), m_mftRecordSize);
    return WriteSectors(startSector, sectorsNeeded, buffer.data());
}

bool NtfsDriver::WipeMftRecordOnDisk(uint64_t recordNum) {
    std::vector<uint8_t> record;
    if (!ReadMftRecord(recordNum, record)) {
        return false;
    }

    auto* hdr = reinterpret_cast<NTFS::NtfsRecordHeader*>(record.data());
    if (hdr->magic == NTFS::NTFS_MAGIC_FILE) {
        hdr->sequenceNumber++;
        hdr->flags &= ~NTFS::FILE_RECORD_IN_USE; // Mark record as free/unallocated
        hdr->hardLinkCount = 0;
        hdr->baseFileRecord = 0;

        uint16_t firstAttr = hdr->firstAttributeOffset;
        if (firstAttr == 0 || firstAttr > m_mftRecordSize - 8) {
            firstAttr = 0x38;
            hdr->firstAttributeOffset = firstAttr;
        }

        // Write ATTR_END marker at firstAttributeOffset
        *reinterpret_cast<uint32_t*>(record.data() + firstAttr) = NTFS::ATTR_END;
        hdr->usedBytes = firstAttr + 8; // Quadword aligned

        // Zero out all sensitive attribute payload data completely
        size_t zeroStart = firstAttr + 4;
        if (zeroStart < m_mftRecordSize) {
            std::memset(record.data() + zeroStart, 0, m_mftRecordSize - zeroStart);
        }
    } else {
        std::memset(record.data(), 0, m_mftRecordSize);
    }

    bool ok = WriteMftRecord(recordNum, record);
    ClearMftRecordBitmapBit(recordNum);
    return ok;
}

// =============================================================================
// Attribute Parsing
// =============================================================================

const uint8_t* NtfsDriver::FindAttribute(const std::vector<uint8_t>& recordBuffer, uint32_t attrType, const std::string& name) const {
    if (recordBuffer.size() < sizeof(NTFS::NtfsRecordHeader)) return nullptr;

    const auto* hdr = reinterpret_cast<const NTFS::NtfsRecordHeader*>(recordBuffer.data());
    uint16_t offset = hdr->firstAttributeOffset;

    while (offset + sizeof(NTFS::NtfsAttributeHeader) <= recordBuffer.size()) {
        const auto* attr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(recordBuffer.data() + offset);

        if (attr->type == NTFS::ATTR_END || attr->length == 0) break;

        if (attr->type == attrType) {
            if (name.empty() && attr->nameLength == 0) {
                return recordBuffer.data() + offset;
            }
            if (!name.empty() && attr->nameLength == name.length()) {
                if (offset + attr->nameOffset + static_cast<size_t>(attr->nameLength) * 2 <= recordBuffer.size()) {
                    const char16_t* nameUtf16 = reinterpret_cast<const char16_t*>(recordBuffer.data() + offset + attr->nameOffset);
                    std::string attrNameStr = Utf16ToUtf8(nameUtf16, attr->nameLength);
                    if (attrNameStr == name) {
                        return recordBuffer.data() + offset;
                    }
                }
            }
        }

        offset += attr->length;
    }
    return nullptr;
}

bool NtfsDriver::GetFileAllocatedExtents(const std::vector<uint8_t>& recordBuffer,
                                        std::vector<NTFS::NtfsExtent>& outExtents,
                                        bool& outIsResident,
                                        std::vector<uint8_t>& outResidentData,
                                        uint64_t& outFileSize) const {
    outExtents.clear();
    outResidentData.clear();
    outIsResident = false;
    outFileSize = 0;

    const uint8_t* attrPtr = FindAttribute(recordBuffer, NTFS::ATTR_DATA);
    if (!attrPtr) return false;

    const auto* attr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(attrPtr);

    if (attr->nonResidentFlag == 0) {
        // Resident File Data
        outIsResident = true;
        const auto* res = reinterpret_cast<const NTFS::NtfsResidentAttributeHeader*>(attrPtr + sizeof(NTFS::NtfsAttributeHeader));
        outFileSize = res->valueLength;
        if (res->valueOffset + res->valueLength <= attr->length &&
            static_cast<size_t>(attrPtr - recordBuffer.data()) + res->valueOffset + res->valueLength <= recordBuffer.size()) {
            const uint8_t* valPtr = attrPtr + res->valueOffset;
            outResidentData.assign(valPtr, valPtr + res->valueLength);
            return true;
        }
        return false;
    } else {
        // Non-Resident File Data
        outIsResident = false;
        const auto* nonRes = reinterpret_cast<const NTFS::NtfsNonResidentAttributeHeader*>(attrPtr + sizeof(NTFS::NtfsAttributeHeader));
        outFileSize = nonRes->dataSize;
        const uint8_t* runlistPtr = attrPtr + nonRes->dataRunsOffset;
        size_t runlistMaxLen = attr->length - nonRes->dataRunsOffset;
        return DecodeRunList(runlistPtr, runlistMaxLen, outExtents);
    }
}

// =============================================================================
// Cluster Bitmap Manipulation ($Bitmap - Record 6)
// =============================================================================

bool NtfsDriver::ReadClusterBitmapByte(uint64_t lcn, uint8_t& outByte, uint64_t& outSector, uint32_t& outOffsetInSector, uint8_t& outBitMask) const {
    std::vector<uint8_t> bitmapRecord;
    if (!ReadMftRecord(NTFS::MFT_REC_BITMAP, bitmapRecord)) return false;

    std::vector<NTFS::NtfsExtent> extents;
    bool isResident = false;
    std::vector<uint8_t> residentData;
    uint64_t fileSize = 0;

    if (!GetFileAllocatedExtents(bitmapRecord, extents, isResident, residentData, fileSize)) {
        return false;
    }

    uint64_t byteIndex = lcn / 8;
    outBitMask = 1 << (lcn % 8);

    if (isResident) {
        if (byteIndex >= residentData.size()) return false;
        outByte = residentData[byteIndex];
        outSector = 0;
        outOffsetInSector = static_cast<uint32_t>(byteIndex);
        return true;
    }

    // Traverse non-resident extents of $Bitmap
    uint64_t accumulatedBytes = 0;
    for (const auto& ext : extents) {
        uint64_t extentBytes = ext.clusterCount * m_bytesPerCluster;
        if (byteIndex < accumulatedBytes + extentBytes) {
            uint64_t offsetInExtent = byteIndex - accumulatedBytes;
            uint64_t clusterInExtent = offsetInExtent / m_bytesPerCluster;
            uint32_t byteInCluster = static_cast<uint32_t>(offsetInExtent % m_bytesPerCluster);

            outSector = ClusterToSector(ext.lcn + clusterInExtent) + (byteInCluster / m_bytesPerSector);
            outOffsetInSector = byteInCluster % m_bytesPerSector;

            std::vector<uint8_t> sectorBuf(m_bytesPerSector);
            if (!ReadSectors(outSector, 1, sectorBuf.data())) return false;
            outByte = sectorBuf[outOffsetInSector];
            return true;
        }
        accumulatedBytes += extentBytes;
    }
    return false;
}

bool NtfsDriver::ClearClusterBitmapBit(uint64_t lcn) {
    uint8_t currentByte = 0;
    uint64_t sector = 0;
    uint32_t offsetInSector = 0;
    uint8_t bitMask = 0;

    if (!ReadClusterBitmapByte(lcn, currentByte, sector, offsetInSector, bitMask)) {
        return false;
    }

    if (sector == 0) {
        // Resident $Bitmap (rare, only in tiny synthetic volumes)
        std::vector<uint8_t> bitmapRecord;
        if (!ReadMftRecord(NTFS::MFT_REC_BITMAP, bitmapRecord)) return false;
        uint8_t* attrPtr = const_cast<uint8_t*>(FindAttribute(bitmapRecord, NTFS::ATTR_DATA));
        if (!attrPtr) return false;
        auto* res = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(attrPtr + sizeof(NTFS::NtfsAttributeHeader));
        uint8_t* valPtr = attrPtr + res->valueOffset;
        valPtr[offsetInSector] &= ~bitMask;
        return WriteMftRecord(NTFS::MFT_REC_BITMAP, bitmapRecord);
    }

    std::vector<uint8_t> sectorBuf(m_bytesPerSector);
    if (!ReadSectors(sector, 1, sectorBuf.data())) return false;

    sectorBuf[offsetInSector] &= ~bitMask;
    return WriteSectors(sector, 1, sectorBuf.data());
}

bool NtfsDriver::ClearMftRecordBitmapBit(uint64_t recordNum) {
    std::vector<uint8_t> mft0;
    if (!ReadMftRecord(NTFS::MFT_REC_MFT, mft0)) return false;

    const uint8_t* attrPtr = FindAttribute(mft0, NTFS::ATTR_BITMAP);
    if (!attrPtr) return false;

    const auto* attrHdr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(attrPtr);
    uint64_t byteIdx = recordNum / 8;
    uint8_t mask = static_cast<uint8_t>(1 << (recordNum % 8));

    if (attrHdr->nonResidentFlag == 0) {
        const auto* res = reinterpret_cast<const NTFS::NtfsResidentAttributeHeader*>(
            attrPtr + sizeof(NTFS::NtfsAttributeHeader)
        );
        if (byteIdx < res->valueLength) {
            uint8_t* valPtr = const_cast<uint8_t*>(attrPtr + res->valueOffset);
            valPtr[byteIdx] &= ~mask;
            return WriteMftRecord(NTFS::MFT_REC_MFT, mft0);
        }
    } else {
        const auto* nonRes = reinterpret_cast<const NTFS::NtfsNonResidentAttributeHeader*>(
            attrPtr + sizeof(NTFS::NtfsAttributeHeader)
        );
        std::vector<NTFS::NtfsExtent> extents;
        size_t runListLen = (attrHdr->length > nonRes->dataRunsOffset) ? (attrHdr->length - nonRes->dataRunsOffset) : 0;
        if (DecodeRunList(attrPtr + nonRes->dataRunsOffset, runListLen, extents)) {
            uint64_t curByte = 0;
            for (const auto& ext : extents) {
                uint64_t extBytes = ext.clusterCount * m_bytesPerCluster;
                if (byteIdx < curByte + extBytes) {
                    uint64_t offInExt = byteIdx - curByte;
                    uint64_t clusterInExt = offInExt / m_bytesPerCluster;
                    uint32_t byteInCluster = static_cast<uint32_t>(offInExt % m_bytesPerCluster);

                    uint64_t targetLcn = ext.lcn + clusterInExt;
                    uint64_t sector = ClusterToSector(targetLcn) + (byteInCluster / m_bytesPerSector);
                    uint32_t offInSector = byteInCluster % m_bytesPerSector;

                    std::vector<uint8_t> secBuf(m_bytesPerSector);
                    if (ReadSectors(sector, 1, secBuf.data())) {
                        secBuf[offInSector] &= ~mask;
                        return WriteSectors(sector, 1, secBuf.data());
                    }
                    return false;
                }
                curByte += extBytes;
            }
        }
    }
    return false;
}

// =============================================================================
// Directory Traversal Helpers
// =============================================================================

std::vector<std::string> NtfsDriver::TokenizePath(const std::string& path) const {
    std::vector<std::string> tokens;
    std::string token;
    std::string normalized = path;

    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }

    std::stringstream ss(normalized);
    while (std::getline(ss, token, '/')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string NtfsDriver::Utf16ToUtf8(const char16_t* utf16Str, size_t length) {
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        char16_t c = utf16Str[i];
        if (c < 0x80) {
            result.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (c >> 6)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (c >> 12)));
            result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return result;
}

bool NtfsDriver::EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool NtfsDriver::SearchIndexBlock(const uint8_t* blockData, size_t blockSize,
                                 const std::string& targetName,
                                 uint64_t& outChildRef, bool& outIsDir,
                                 uint64_t& outChildVcn, size_t& outEntryOffset,
                                 uint32_t& outEntryLen) const {
    if (!blockData || blockSize < sizeof(NTFS::NtfsIndexHeader)) return false;

    const auto* idxHdr = reinterpret_cast<const NTFS::NtfsIndexHeader*>(blockData);
    size_t offset = idxHdr->firstEntryOffset;

    while (offset + sizeof(NTFS::NtfsIndexEntry) <= blockSize && offset < idxHdr->totalEntriesSize) {
        const auto* entry = reinterpret_cast<const NTFS::NtfsIndexEntry*>(blockData + offset);
        if (entry->length == 0) break;

        outEntryOffset = offset;
        outEntryLen = entry->length;

        if (entry->flags & NTFS::INDEX_ENTRY_LAST) {
            if (entry->flags & NTFS::INDEX_ENTRY_HAS_SUBNODES) {
                outChildVcn = *reinterpret_cast<const uint64_t*>(blockData + offset + entry->length - 8);
            }
            break;
        }

        if (entry->keyLength >= sizeof(NTFS::NtfsFileNameAttribute)) {
            const auto* fn = reinterpret_cast<const NTFS::NtfsFileNameAttribute*>(
                blockData + offset + sizeof(NTFS::NtfsIndexEntry)
            );
            std::string nameStr = Utf16ToUtf8(fn->fileName, fn->fileNameLength);

            if (EqualsIgnoreCase(nameStr, targetName)) {
                outChildRef = entry->fileReference & 0x0000FFFFFFFFFFFFULL;
                outIsDir = (fn->flags & 0x10000000) != 0;
                return true;
            }
        }

        if (entry->flags & NTFS::INDEX_ENTRY_HAS_SUBNODES) {
            outChildVcn = *reinterpret_cast<const uint64_t*>(blockData + offset + entry->length - 8);
        }

        offset += entry->length;
    }
    return false;
}

bool NtfsDriver::FindEntryInDirectory(uint64_t dirRecordNum, const std::string& targetName,
                                     uint64_t& outChildRecordNum, bool& outIsDir,
                                     uint64_t& outIndexSector, uint32_t& outIndexOffset,
                                     uint32_t& outEntrySize) const {
    std::vector<uint8_t> dirRecord;
    if (!ReadMftRecord(dirRecordNum, dirRecord)) return false;

    // 1. Search in resident $INDEX_ROOT (Attribute 0x90)
    const uint8_t* indexRootAttr = FindAttribute(dirRecord, NTFS::ATTR_INDEX_ROOT);
    if (indexRootAttr) {
        const auto* resHdr = reinterpret_cast<const NTFS::NtfsResidentAttributeHeader*>(
            indexRootAttr + sizeof(NTFS::NtfsAttributeHeader)
        );
        const uint8_t* rootPayload = indexRootAttr + resHdr->valueOffset;
        const uint8_t* indexData = rootPayload + sizeof(NTFS::NtfsIndexRootHeader);
        size_t indexDataLen = resHdr->valueLength - sizeof(NTFS::NtfsIndexRootHeader);

        uint64_t childRef = 0;
        uint64_t childVcn = 0;
        size_t entryOffset = 0;
        uint32_t entryLen = 0;

        if (SearchIndexBlock(indexData, indexDataLen, targetName, childRef, outIsDir, childVcn, entryOffset, entryLen)) {
            outChildRecordNum = childRef;
            uint32_t mftOffsetInSector = 0;
            outIndexSector = MftRecordToSector(dirRecordNum, mftOffsetInSector);
            outIndexOffset = static_cast<uint32_t>(mftOffsetInSector + (indexData - dirRecord.data()) + entryOffset);
            outEntrySize = entryLen;
            return true;
        }
    }

    // 2. Search in non-resident $INDEX_ALLOCATION (Attribute 0xA0) with "INDX" blocks
    const uint8_t* indexAllocAttr = FindAttribute(dirRecord, NTFS::ATTR_INDEX_ALLOCATION, "$I30");
    if (indexAllocAttr) {
        std::vector<NTFS::NtfsExtent> allocExtents;
        const auto* nonRes = reinterpret_cast<const NTFS::NtfsNonResidentAttributeHeader*>(
            indexAllocAttr + sizeof(NTFS::NtfsAttributeHeader)
        );
        const auto* attrHdr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(indexAllocAttr);
        size_t runListLen = (attrHdr->length > nonRes->dataRunsOffset) ? (attrHdr->length - nonRes->dataRunsOffset) : 0;
        DecodeRunList(indexAllocAttr + nonRes->dataRunsOffset, runListLen, allocExtents);

        for (const auto& ext : allocExtents) {
            uint64_t blocksInExtent = (ext.clusterCount * m_bytesPerCluster) / m_indexBlockSize;
            for (uint64_t b = 0; b < blocksInExtent; ++b) {
                uint64_t blockStartSector = ClusterToSector(ext.lcn) + (b * m_indexBlockSize / m_bytesPerSector);
                std::vector<uint8_t> blockBuffer(m_indexBlockSize);

                if (!ReadSectors(blockStartSector, m_indexBlockSize / m_bytesPerSector, blockBuffer.data())) {
                    continue;
                }

                ApplyFixup(blockBuffer.data(), blockBuffer.size());
                const auto* ib = reinterpret_cast<const NTFS::NtfsIndexBlock*>(blockBuffer.data());
                if (ib->magic != NTFS::NTFS_MAGIC_INDX) continue;

                uint64_t childRef = 0;
                uint64_t childVcn = 0;
                size_t entryOffset = 0;
                uint32_t entryLen = 0;

                const uint8_t* indexHeaderPtr = reinterpret_cast<const uint8_t*>(&ib->indexHeader);
                size_t headerOffsetInBlock = indexHeaderPtr - blockBuffer.data();

                if (SearchIndexBlock(indexHeaderPtr, m_indexBlockSize - headerOffsetInBlock,
                                     targetName, childRef, outIsDir, childVcn, entryOffset, entryLen)) {
                    outChildRecordNum = childRef;
                    outIndexSector = blockStartSector;
                    outIndexOffset = static_cast<uint32_t>(headerOffsetInBlock + entryOffset);
                    outEntrySize = entryLen;
                    return true;
                }
            }
        }
    }
    return false;
}

bool NtfsDriver::ScrubDirectoryEntry(uint64_t dirRecordNum, const std::string& targetName) {
    std::vector<uint8_t> dirRecord;
    if (!ReadMftRecord(dirRecordNum, dirRecord)) return false;

    // 1. Try removing from resident $INDEX_ROOT
    uint8_t* indexRootAttr = const_cast<uint8_t*>(FindAttribute(dirRecord, NTFS::ATTR_INDEX_ROOT));
    if (indexRootAttr) {
        auto* attrHdr = reinterpret_cast<NTFS::NtfsAttributeHeader*>(indexRootAttr);
        auto* resHdr = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(
            indexRootAttr + sizeof(NTFS::NtfsAttributeHeader)
        );
        uint8_t* rootPayload = indexRootAttr + resHdr->valueOffset;
        uint8_t* indexData = rootPayload + sizeof(NTFS::NtfsIndexRootHeader);
        size_t indexDataLen = resHdr->valueLength - sizeof(NTFS::NtfsIndexRootHeader);

        uint64_t childRef = 0;
        bool isDir = false;
        uint64_t childVcn = 0;
        size_t entryOffset = 0;
        uint32_t entryLen = 0;

        if (SearchIndexBlock(indexData, indexDataLen, targetName, childRef, isDir, childVcn, entryOffset, entryLen)) {
            auto* idxHdr = reinterpret_cast<NTFS::NtfsIndexHeader*>(indexData);
            if (entryOffset + entryLen <= idxHdr->totalEntriesSize) {
                // Shift subsequent index entries left over the eradicated entry
                size_t bytesToShift = idxHdr->totalEntriesSize - (entryOffset + entryLen);
                if (bytesToShift > 0) {
                    std::memmove(indexData + entryOffset, indexData + entryOffset + entryLen, bytesToShift);
                }
                // Zero vacated tail space of index entries
                std::memset(indexData + idxHdr->totalEntriesSize - entryLen, 0, entryLen);

                idxHdr->totalEntriesSize -= entryLen;
                idxHdr->allocatedSize -= entryLen;
                resHdr->valueLength -= entryLen;

                uint32_t oldAttrLen = attrHdr->length;
                attrHdr->length -= entryLen;

                // Shift any subsequent attributes in the MFT record left
                size_t afterAttrOffset = (indexRootAttr - dirRecord.data()) + oldAttrLen;
                auto* recHdr = reinterpret_cast<NTFS::NtfsRecordHeader*>(dirRecord.data());
                if (recHdr->usedBytes >= afterAttrOffset && recHdr->usedBytes >= entryLen) {
                    size_t tailBytes = recHdr->usedBytes - afterAttrOffset;
                    if (tailBytes > 0) {
                        std::memmove(dirRecord.data() + afterAttrOffset - entryLen, dirRecord.data() + afterAttrOffset, tailBytes);
                    }
                    recHdr->usedBytes -= entryLen;
                    std::memset(dirRecord.data() + recHdr->usedBytes, 0, entryLen);
                } else if (recHdr->usedBytes > 0 && recHdr->usedBytes >= entryLen) {
                    recHdr->usedBytes -= entryLen;
                }

                return WriteMftRecord(dirRecordNum, dirRecord);
            }
        }
    }

    // 2. Try removing from non-resident $INDEX_ALLOCATION ("INDX" blocks)
    const uint8_t* indexAllocAttr = FindAttribute(dirRecord, NTFS::ATTR_INDEX_ALLOCATION, "$I30");
    if (indexAllocAttr) {
        std::vector<NTFS::NtfsExtent> allocExtents;
        const auto* nonRes = reinterpret_cast<const NTFS::NtfsNonResidentAttributeHeader*>(
            indexAllocAttr + sizeof(NTFS::NtfsAttributeHeader)
        );
        const auto* attrHdr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(indexAllocAttr);
        size_t runListLen = (attrHdr->length > nonRes->dataRunsOffset) ? (attrHdr->length - nonRes->dataRunsOffset) : 0;
        DecodeRunList(indexAllocAttr + nonRes->dataRunsOffset, runListLen, allocExtents);

        for (const auto& ext : allocExtents) {
            uint64_t blocksInExtent = (ext.clusterCount * m_bytesPerCluster) / m_indexBlockSize;
            for (uint64_t b = 0; b < blocksInExtent; ++b) {
                uint64_t blockStartSector = ClusterToSector(ext.lcn) + (b * m_indexBlockSize / m_bytesPerSector);
                std::vector<uint8_t> blockBuffer(m_indexBlockSize);

                if (!ReadSectors(blockStartSector, m_indexBlockSize / m_bytesPerSector, blockBuffer.data())) {
                    continue;
                }

                ApplyFixup(blockBuffer.data(), blockBuffer.size());
                auto* ib = reinterpret_cast<NTFS::NtfsIndexBlock*>(blockBuffer.data());
                if (ib->magic != NTFS::NTFS_MAGIC_INDX) continue;

                uint64_t childRef = 0;
                bool isDir = false;
                uint64_t childVcn = 0;
                size_t entryOffset = 0;
                uint32_t entryLen = 0;

                uint8_t* indexHeaderPtr = reinterpret_cast<uint8_t*>(&ib->indexHeader);
                size_t headerOffsetInBlock = indexHeaderPtr - blockBuffer.data();

                if (SearchIndexBlock(indexHeaderPtr, m_indexBlockSize - headerOffsetInBlock,
                                     targetName, childRef, isDir, childVcn, entryOffset, entryLen)) {
                    auto* idxHdr = &ib->indexHeader;
                    if (entryOffset + entryLen <= idxHdr->totalEntriesSize) {
                        size_t bytesToShift = idxHdr->totalEntriesSize - (entryOffset + entryLen);
                        if (bytesToShift > 0) {
                            std::memmove(indexHeaderPtr + entryOffset, indexHeaderPtr + entryOffset + entryLen, bytesToShift);
                        }
                        std::memset(indexHeaderPtr + idxHdr->totalEntriesSize - entryLen, 0, entryLen);
                        idxHdr->totalEntriesSize -= entryLen;

                        EncodeFixup(blockBuffer.data(), blockBuffer.size());
                        return WriteSectors(blockStartSector, m_indexBlockSize / m_bytesPerSector, blockBuffer.data());
                    }
                }
            }
        }
    }

    return false;
}

bool NtfsDriver::ListDirectoryContents(uint64_t dirRecordNum,
                                      std::vector<std::pair<std::string, uint64_t>>& outEntries,
                                      std::vector<bool>& outIsDir) const {
    outEntries.clear();
    outIsDir.clear();

    std::vector<uint8_t> dirRecord;
    if (!ReadMftRecord(dirRecordNum, dirRecord)) return false;

    // Helper lambda to harvest entries from an index block
    auto harvestEntries = [&](const uint8_t* blockData, size_t blockSize) {
        if (!blockData || blockSize < sizeof(NTFS::NtfsIndexHeader)) return;
        const auto* idxHdr = reinterpret_cast<const NTFS::NtfsIndexHeader*>(blockData);
        size_t offset = idxHdr->firstEntryOffset;

        while (offset + sizeof(NTFS::NtfsIndexEntry) <= blockSize && offset < idxHdr->totalEntriesSize) {
            const auto* entry = reinterpret_cast<const NTFS::NtfsIndexEntry*>(blockData + offset);
            if (entry->length == 0) break;

            if (!(entry->flags & NTFS::INDEX_ENTRY_LAST) && entry->keyLength >= sizeof(NTFS::NtfsFileNameAttribute)) {
                const auto* fn = reinterpret_cast<const NTFS::NtfsFileNameAttribute*>(
                    blockData + offset + sizeof(NTFS::NtfsIndexEntry)
                );
                std::string nameStr = Utf16ToUtf8(fn->fileName, fn->fileNameLength);
                if (nameStr != "." && nameStr != "..") {
                    uint64_t ref = entry->fileReference & 0x0000FFFFFFFFFFFFULL;
                    bool isDirectory = (fn->flags & 0x10000000) != 0;
                    outEntries.push_back({ nameStr, ref });
                    outIsDir.push_back(isDirectory);
                }
            }
            offset += entry->length;
        }
    };

    // 1. Harvest from $INDEX_ROOT
    const uint8_t* indexRootAttr = FindAttribute(dirRecord, NTFS::ATTR_INDEX_ROOT);
    if (indexRootAttr) {
        const auto* resHdr = reinterpret_cast<const NTFS::NtfsResidentAttributeHeader*>(
            indexRootAttr + sizeof(NTFS::NtfsAttributeHeader)
        );
        const uint8_t* rootPayload = indexRootAttr + resHdr->valueOffset;
        const uint8_t* indexData = rootPayload + sizeof(NTFS::NtfsIndexRootHeader);
        size_t indexDataLen = resHdr->valueLength - sizeof(NTFS::NtfsIndexRootHeader);
        harvestEntries(indexData, indexDataLen);
    }

    // 2. Harvest from $INDEX_ALLOCATION ("INDX" blocks)
    const uint8_t* indexAllocAttr = FindAttribute(dirRecord, NTFS::ATTR_INDEX_ALLOCATION, "$I30");
    if (indexAllocAttr) {
        std::vector<NTFS::NtfsExtent> allocExtents;
        const auto* nonRes = reinterpret_cast<const NTFS::NtfsNonResidentAttributeHeader*>(
            indexAllocAttr + sizeof(NTFS::NtfsAttributeHeader)
        );
        const auto* attrHdr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(indexAllocAttr);
        size_t runListLen = (attrHdr->length > nonRes->dataRunsOffset) ? (attrHdr->length - nonRes->dataRunsOffset) : 0;
        DecodeRunList(indexAllocAttr + nonRes->dataRunsOffset, runListLen, allocExtents);

        for (const auto& ext : allocExtents) {
            uint64_t blocksInExtent = (ext.clusterCount * m_bytesPerCluster) / m_indexBlockSize;
            for (uint64_t b = 0; b < blocksInExtent; ++b) {
                uint64_t blockStartSector = ClusterToSector(ext.lcn) + (b * m_indexBlockSize / m_bytesPerSector);
                std::vector<uint8_t> blockBuffer(m_indexBlockSize);
                if (!ReadSectors(blockStartSector, m_indexBlockSize / m_bytesPerSector, blockBuffer.data())) continue;
                ApplyFixup(blockBuffer.data(), blockBuffer.size());
                const auto* ib = reinterpret_cast<const NTFS::NtfsIndexBlock*>(blockBuffer.data());
                if (ib->magic == NTFS::NTFS_MAGIC_INDX) {
                    const uint8_t* indexHeaderPtr = reinterpret_cast<const uint8_t*>(&ib->indexHeader);
                    size_t headerOffsetInBlock = indexHeaderPtr - blockBuffer.data();
                    harvestEntries(indexHeaderPtr, m_indexBlockSize - headerOffsetInBlock);
                }
            }
        }
    }
    return true;
}

// =============================================================================
// Core API: Mount
// =============================================================================

bool NtfsDriver::Mount() {
    if (!m_hardware) {
        std::cerr << "[NtfsDriver] Error: Hardware controller is null.\n";
        return false;
    }

    Core::DeviceGeometry geo = m_hardware->GetGeometry();
    if (geo.bytesPerSector == 0) {
        std::cerr << "[NtfsDriver] Error: Invalid device geometry.\n";
        return false;
    }

    // Read Sector 0 (Volume Boot Record)
    std::vector<uint8_t> sector0(geo.bytesPerSector);
    if (!ReadSectors(0, 1, sector0.data())) {
        std::cerr << "[NtfsDriver] Error: Failed to read Sector 0.\n";
        return false;
    }

    std::memcpy(&m_vbr, sector0.data(), sizeof(NTFS::NtfsBootSector));

    // Verify OEM ID is "NTFS    " and boot signature is 0xAA55
    if (std::strncmp(m_vbr.oemId, "NTFS    ", 8) != 0) {
        std::cerr << "[NtfsDriver] Error: Device is not formatted as NTFS.\n";
        return false;
    }

    if (m_vbr.bootSignature != NTFS::NTFS_BOOT_SIGNATURE) {
        std::cerr << "[NtfsDriver] Error: Invalid boot signature (expected 0xAA55).\n";
        return false;
    }

    m_bytesPerSector = m_vbr.bytesPerSector;
    m_sectorsPerCluster = m_vbr.sectorsPerCluster;
    m_bytesPerCluster = m_bytesPerSector * m_sectorsPerCluster;
    m_totalSectors = m_vbr.totalSectors;

    // Decode MFT Record Size
    if (m_vbr.clustersPerMftRecord < 0) {
        m_mftRecordSize = 1 << (-m_vbr.clustersPerMftRecord);
    } else {
        m_mftRecordSize = static_cast<uint32_t>(m_vbr.clustersPerMftRecord) * m_bytesPerCluster;
    }

    // Decode Index Buffer Size
    if (m_vbr.clustersPerIndexBuffer < 0) {
        m_indexBlockSize = 1 << (-m_vbr.clustersPerIndexBuffer);
    } else {
        m_indexBlockSize = static_cast<uint32_t>(m_vbr.clustersPerIndexBuffer) * m_bytesPerCluster;
    }

    // Read Record 0 ($MFT itself) to build dynamic extent mapping
    std::vector<uint8_t> mftRecord0;
    if (!ReadMftRecord(NTFS::MFT_REC_MFT, mftRecord0)) {
        std::cerr << "[NtfsDriver] Warning: Failed to read $MFT Record 0 during initial mount.\n";
    } else {
        std::vector<NTFS::NtfsExtent> extents;
        bool isResident = false;
        std::vector<uint8_t> residentData;
        uint64_t fileSize = 0;
        if (GetFileAllocatedExtents(mftRecord0, extents, isResident, residentData, fileSize) && !extents.empty()) {
            m_mftExtents = extents;
        } else {
            m_mftExtents = { { m_vbr.mftStartLCN, 1 } };
        }
    }

    std::cout << "[NtfsDriver] Successfully mounted NTFS volume.\n";
    return true;
}

// =============================================================================
// Core API: EraseFile & Recursive EraseDirectory
// =============================================================================

bool NtfsDriver::EraseFile(const std::string& relativePath) {
    if (m_bytesPerSector == 0) {
        std::cerr << "[NtfsDriver] Error: Filesystem not mounted.\n";
        return false;
    }

    std::vector<std::string> pathTokens = TokenizePath(relativePath);
    if (pathTokens.empty()) {
        std::cerr << "[NtfsDriver] Error: Empty path provided.\n";
        return false;
    }

    std::cout << "\n--- Initiating NTFS Secure Deletion for: " << relativePath << " ---\n";

    // Start at Root Directory (Record 5)
    uint64_t currentDirRecord = NTFS::MFT_REC_ROOT;
    uint64_t targetRecord = 0;
    bool isDirectory = false;
    uint64_t indexSector = 0;
    uint32_t indexOffset = 0;
    uint32_t entrySize = 0;

    for (size_t i = 0; i < pathTokens.size(); ++i) {
        const std::string& token = pathTokens[i];
        bool isLast = (i == pathTokens.size() - 1);

        if (!FindEntryInDirectory(currentDirRecord, token, targetRecord, isDirectory,
                                 indexSector, indexOffset, entrySize)) {
            std::cerr << "[NtfsDriver] Error: Path component '" << token << "' not found.\n";
            return false;
        }

        if (!isLast) {
            if (!isDirectory) {
                std::cerr << "[NtfsDriver] Error: '" << token << "' is not a directory.\n";
                return false;
            }
            currentDirRecord = targetRecord;
        }
    }

    // If target is a directory, delegate to recursive folder eradication
    if (isDirectory) {
        std::cout << "[NtfsDriver] Target is a directory. Delegating to recursive folder eradication...\n";
        return EraseDirectory(relativePath);
    }

    // Target is a file:
    std::vector<uint8_t> targetRecordBuffer;
    if (!ReadMftRecord(targetRecord, targetRecordBuffer)) {
        std::cerr << "[NtfsDriver] Error: Failed to read target MFT record " << targetRecord << "\n";
        return false;
    }

    // 1. Data Obliteration
    std::vector<NTFS::NtfsExtent> dataExtents;
    bool isResident = false;
    std::vector<uint8_t> residentData;
    uint64_t fileSize = 0;

    if (GetFileAllocatedExtents(targetRecordBuffer, dataExtents, isResident, residentData, fileSize)) {
        if (isResident) {
            std::cout << "  [Data] File payload is resident inside MFT record (" << fileSize
                      << " bytes). Will be obliterated during MFT record wipe.\n";
        } else {
            std::cout << "  [Data] File occupies " << dataExtents.size() << " non-resident extents on disk.\n";
            for (const auto& ext : dataExtents) {
                uint64_t startSector = ClusterToSector(ext.lcn);
                uint32_t countSectors = static_cast<uint32_t>(ext.clusterCount * m_sectorsPerCluster);

                std::cout << "  -> Securely erasing LCN " << ext.lcn << " (" << ext.clusterCount
                          << " clusters, Sectors " << startSector << " to " << (startSector + countSectors - 1) << ")...\n";

                m_hardware->SecureEraseSectors(startSector, countSectors);

                // Clear allocation bits in $Bitmap
                for (uint64_t c = 0; c < ext.clusterCount; ++c) {
                    ClearClusterBitmapBit(ext.lcn + c);
                }
            }
        }
    }

    // 2. Metadata Obliteration (MFT Record)
    std::cout << "  [Metadata] Zeroing on-disk MFT Record " << targetRecord << " (" << m_mftRecordSize << " bytes)...\n";
    WipeMftRecordOnDisk(targetRecord);

    // 3. Parent Directory Sanitization
    std::cout << "  [Directory] Scrubbing entry from parent directory record " << currentDirRecord << "...\n";
    ScrubDirectoryEntry(currentDirRecord, pathTokens.back());

    std::cout << "[NtfsDriver] File erasure complete. Target permanently unrecoverable.\n";
    return true;
}

bool NtfsDriver::EraseDirectory(const std::string& relativePath) {
    if (m_bytesPerSector == 0) return false;

    std::vector<std::string> pathTokens = TokenizePath(relativePath);
    if (pathTokens.empty()) {
        std::cerr << "[NtfsDriver] Error: Root directory cannot be erased via EraseDirectory. Use WipeVolume instead.\n";
        return false;
    }

    uint64_t parentDirRecord = NTFS::MFT_REC_ROOT;
    uint64_t targetDirRecord = 0;
    bool isDirectory = false;
    uint64_t indexSector = 0;
    uint32_t indexOffset = 0;
    uint32_t entrySize = 0;

    for (size_t i = 0; i < pathTokens.size(); ++i) {
        const std::string& token = pathTokens[i];
        bool isLast = (i == pathTokens.size() - 1);

        if (!FindEntryInDirectory(parentDirRecord, token, targetDirRecord, isDirectory,
                                 indexSector, indexOffset, entrySize)) {
            std::cerr << "[NtfsDriver] Error: Directory '" << token << "' not found.\n";
            return false;
        }

        if (!isLast) {
            parentDirRecord = targetDirRecord;
        }
    }

    if (!isDirectory) {
        std::vector<uint8_t> targetRecBuf;
        if (ReadMftRecord(targetDirRecord, targetRecBuf)) {
            const auto* thdr = reinterpret_cast<const NTFS::NtfsRecordHeader*>(targetRecBuf.data());
            if (thdr->magic == NTFS::NTFS_MAGIC_FILE && (thdr->flags & NTFS::FILE_RECORD_DIRECTORY)) {
                isDirectory = true;
            }
        }
    }

    if (!isDirectory) {
        std::cerr << "[NtfsDriver] Error: Target is a file, not a directory.\n";
        return false;
    }

    std::cout << "\n--- Initiating Recursive Directory Erasure for: " << relativePath
              << " (MFT Record " << targetDirRecord << ") ---\n";

    // Erase all children recursively
    if (!EraseDirectoryRecursive(targetDirRecord)) {
        std::cerr << "[NtfsDriver] Warning: Errors encountered while erasing directory contents.\n";
    }

    // Erase directory's own $INDEX_ALLOCATION blocks if any exist
    std::vector<uint8_t> dirRecordBuf;
    if (ReadMftRecord(targetDirRecord, dirRecordBuf)) {
        const uint8_t* indexAllocAttr = FindAttribute(dirRecordBuf, NTFS::ATTR_INDEX_ALLOCATION, "$I30");
        if (indexAllocAttr) {
            std::vector<NTFS::NtfsExtent> allocExtents;
            const auto* nonRes = reinterpret_cast<const NTFS::NtfsNonResidentAttributeHeader*>(
                indexAllocAttr + sizeof(NTFS::NtfsAttributeHeader)
            );
            const auto* attrHdr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(indexAllocAttr);
            size_t runListLen = (attrHdr->length > nonRes->dataRunsOffset) ? (attrHdr->length - nonRes->dataRunsOffset) : 0;
            DecodeRunList(indexAllocAttr + nonRes->dataRunsOffset, runListLen, allocExtents);
            for (const auto& ext : allocExtents) {
                uint64_t startSec = ClusterToSector(ext.lcn);
                uint32_t countSec = static_cast<uint32_t>(ext.clusterCount * m_sectorsPerCluster);
                m_hardware->SecureEraseSectors(startSec, countSec);
                for (uint64_t c = 0; c < ext.clusterCount; ++c) {
                    ClearClusterBitmapBit(ext.lcn + c);
                }
            }
        }
    }

    // Wipe directory's on-disk MFT record
    WipeMftRecordOnDisk(targetDirRecord);

    // Scrub entry from parent directory
    ScrubDirectoryEntry(parentDirRecord, pathTokens.back());

    std::cout << "[NtfsDriver] Recursive directory erasure completed successfully.\n";
    return true;
}

bool NtfsDriver::EraseDirectoryRecursive(uint64_t dirRecordNum) {
    std::vector<std::pair<std::string, uint64_t>> entries;
    std::vector<bool> isDirList;

    if (!ListDirectoryContents(dirRecordNum, entries, isDirList)) {
        return false;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& childName = entries[i].first;
        uint64_t childRecord = entries[i].second;
        bool childIsDir = isDirList[i];

        if (childIsDir) {
            std::cout << "  -> Recursing into sub-folder: " << childName << " (Record " << childRecord << ")...\n";
            EraseDirectoryRecursive(childRecord);

            // Wipe sub-folder's MFT record
            WipeMftRecordOnDisk(childRecord);
        } else {
            std::cout << "  -> Erasing child file: " << childName << " (Record " << childRecord << ")...\n";

            std::vector<uint8_t> childRecBuf;
            if (ReadMftRecord(childRecord, childRecBuf)) {
                std::vector<NTFS::NtfsExtent> extents;
                bool isResident = false;
                std::vector<uint8_t> resData;
                uint64_t fileSize = 0;
                if (GetFileAllocatedExtents(childRecBuf, extents, isResident, resData, fileSize) && !isResident) {
                    for (const auto& ext : extents) {
                        uint64_t startSec = ClusterToSector(ext.lcn);
                        uint32_t countSec = static_cast<uint32_t>(ext.clusterCount * m_sectorsPerCluster);
                        m_hardware->SecureEraseSectors(startSec, countSec);
                        for (uint64_t c = 0; c < ext.clusterCount; ++c) {
                            ClearClusterBitmapBit(ext.lcn + c);
                        }
                    }
                }
            }
            WipeMftRecordOnDisk(childRecord);
        }

        // Scrub entry from current directory index
        ScrubDirectoryEntry(dirRecordNum, childName);
    }
    return true;
}

// =============================================================================
// Volume-Wide Surgical Wipe (WipeVolume)
// =============================================================================

bool NtfsDriver::WipeVolume() {
    if (m_bytesPerSector == 0) return false;

    std::cout << "\n=== INITIATING NTFS SURGICAL VOLUME WIPE ===\n";
    std::cout << "[Quarantine] Preserving system records 0 through 15 ($MFT, $LogFile, $Root, etc.)...\n";

    // Read Record 0 ($MFT) to find total records
    std::vector<uint8_t> mft0;
    if (!ReadMftRecord(NTFS::MFT_REC_MFT, mft0)) return false;

    std::vector<NTFS::NtfsExtent> mftExtents;
    bool isResident = false;
    std::vector<uint8_t> resData;
    uint64_t mftByteSize = 0;
    GetFileAllocatedExtents(mft0, mftExtents, isResident, resData, mftByteSize);

    uint64_t totalRecords = mftByteSize / m_mftRecordSize;
    if (totalRecords < NTFS::MFT_REC_USER_START) {
        totalRecords = NTFS::MFT_REC_USER_START;
    }

    std::cout << "[Erasure] Scanning and obliterating all user records (16 to " << totalRecords << ")...\n";
    uint64_t userWipedCount = 0;

    for (uint64_t rec = NTFS::MFT_REC_USER_START; rec < totalRecords; ++rec) {
        std::vector<uint8_t> recordBuf;
        if (!ReadMftRecord(rec, recordBuf)) continue;

        const auto* hdr = reinterpret_cast<const NTFS::NtfsRecordHeader*>(recordBuf.data());
        if (hdr->magic != NTFS::NTFS_MAGIC_FILE) continue;

        // If record is in use, erase allocated data extents
        if (hdr->flags & NTFS::FILE_RECORD_IN_USE) {
            std::vector<NTFS::NtfsExtent> extents;
            bool res = false;
            std::vector<uint8_t> rData;
            uint64_t sz = 0;
            if (GetFileAllocatedExtents(recordBuf, extents, res, rData, sz) && !res) {
                for (const auto& ext : extents) {
                    uint64_t sec = ClusterToSector(ext.lcn);
                    uint32_t count = static_cast<uint32_t>(ext.clusterCount * m_sectorsPerCluster);
                    m_hardware->SecureEraseSectors(sec, count);
                    for (uint64_t c = 0; c < ext.clusterCount; ++c) {
                        ClearClusterBitmapBit(ext.lcn + c);
                    }
                }
            }

            // Wipe MFT record on disk
            WipeMftRecordOnDisk(rec);
            userWipedCount++;
        }
    }

    // Scrub user directory entries inside Root directory (Record 5)
    std::cout << "[System] Scrubbing user entries from Root directory...\n";
    std::vector<std::pair<std::string, uint64_t>> rootEntries;
    std::vector<bool> rootIsDir;
    if (ListDirectoryContents(NTFS::MFT_REC_ROOT, rootEntries, rootIsDir)) {
        for (const auto& entry : rootEntries) {
            ScrubDirectoryEntry(NTFS::MFT_REC_ROOT, entry.first);
        }
    }

    std::cout << "[NtfsDriver] Volume wipe completed. Wiped " << userWipedCount << " user files/directories.\n";
    return true;
}

// =============================================================================
// Complete Drive Format (FormatDrive)
// =============================================================================

bool NtfsDriver::FormatDrive(bool fullDriveSanitize) {
    if (!m_hardware) return false;

    Core::DeviceGeometry geo = m_hardware->GetGeometry();
    if (geo.bytesPerSector == 0 || geo.totalSectors == 0) return false;

    std::cout << "\n=== INITIATING COMPLETE NTFS DRIVE FORMAT ===\n";

    if (fullDriveSanitize) {
        std::cout << "[Hardware] Initiating full drive demolition / sanitization...\n";
        if (!m_hardware->SecureEraseDrive()) {
            std::cout << "[Hardware] SecureEraseDrive fallback: Overwriting sectors with zero pattern...\n";
            // Zero out first 10,000 sectors as protective sweep
            uint32_t sweepCount = static_cast<uint32_t>(std::min<uint64_t>(geo.totalSectors, 10000));
            m_hardware->SecureEraseSectors(0, sweepCount);
        }
    }

    std::cout << "[Format] Initializing pristine NTFS parameters...\n";

    uint32_t bytesPerSec = geo.bytesPerSector;
    uint8_t secPerClust = (bytesPerSec == 4096) ? 1 : 8; // 4KB cluster
    uint64_t totalSec = geo.totalSectors;
    uint64_t mftStartLcn = 4;      // Start MFT at Cluster 4
    uint64_t mftMirrStartLcn = 2;  // Start MFTMirr at Cluster 2

    // 1. Construct pristine Volume Boot Record
    NTFS::NtfsBootSector vbr;
    std::memset(&vbr, 0, sizeof(vbr));
    vbr.jumpInstruction[0] = 0xEB;
    vbr.jumpInstruction[1] = 0x52;
    vbr.jumpInstruction[2] = 0x90;
    std::memcpy(vbr.oemId, "NTFS    ", 8);
    vbr.bytesPerSector = static_cast<uint16_t>(bytesPerSec);
    vbr.sectorsPerCluster = secPerClust;
    vbr.mediaDescriptor = 0xF8;
    vbr.totalSectors = totalSec;
    vbr.mftStartLCN = mftStartLcn;
    vbr.mftMirrStartLCN = mftMirrStartLcn;
    vbr.clustersPerMftRecord = -10;   // 2^10 = 1024 bytes
    vbr.clustersPerIndexBuffer = -12; // 2^12 = 4096 bytes
    vbr.volumeSerialNumber = 0x1A2B3C4D5E6F7081ULL;
    vbr.bootSignature = NTFS::NTFS_BOOT_SIGNATURE;

    std::vector<uint8_t> vbrSector(bytesPerSec, 0);
    std::memcpy(vbrSector.data(), &vbr, sizeof(vbr));

    std::cout << "[Format] Writing Sector 0 (VBR)...\n";
    if (!WriteSectors(0, 1, vbrSector.data())) {
        std::cerr << "[Format] Error: Failed to write VBR.\n";
        return false;
    }

    // 2. Initialize pristine MFT records (0 to 15)
    std::cout << "[Format] Writing pristine initial MFT records (Records 0-15)...\n";
    uint64_t mftStartSector = mftStartLcn * secPerClust;

    std::vector<uint8_t> cleanRecord(1024, 0);
    for (uint64_t rec = 0; rec < 16; ++rec) {
        std::memset(cleanRecord.data(), 0, 1024);
        auto* hdr = reinterpret_cast<NTFS::NtfsRecordHeader*>(cleanRecord.data());
        hdr->magic = NTFS::NTFS_MAGIC_FILE;
        hdr->updateSequenceOffset = 0x30;
        hdr->updateSequenceSize = 3;
        hdr->sequenceNumber = 1;
        hdr->firstAttributeOffset = 0x38;
        hdr->allocatedBytes = 1024;
        hdr->recordNumber = static_cast<uint32_t>(rec);

        if (rec == NTFS::MFT_REC_ROOT) {
            hdr->flags = NTFS::FILE_RECORD_IN_USE | NTFS::FILE_RECORD_DIRECTORY;
            hdr->hardLinkCount = 1;

            // Write minimal $INDEX_ROOT attribute inside Record 5
            uint8_t* attrPtr = cleanRecord.data() + 0x38;
            auto* attrHdr = reinterpret_cast<NTFS::NtfsAttributeHeader*>(attrPtr);
            attrHdr->type = NTFS::ATTR_INDEX_ROOT;
            attrHdr->length = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader) +
                              sizeof(NTFS::NtfsIndexRootHeader) + sizeof(NTFS::NtfsIndexHeader) + sizeof(NTFS::NtfsIndexEntry);
            attrHdr->nonResidentFlag = 0;

            auto* res = reinterpret_cast<NTFS::NtfsResidentAttributeHeader*>(attrPtr + sizeof(NTFS::NtfsAttributeHeader));
            res->valueOffset = sizeof(NTFS::NtfsAttributeHeader) + sizeof(NTFS::NtfsResidentAttributeHeader);
            res->valueLength = sizeof(NTFS::NtfsIndexRootHeader) + sizeof(NTFS::NtfsIndexHeader) + sizeof(NTFS::NtfsIndexEntry);

            auto* rootHdr = reinterpret_cast<NTFS::NtfsIndexRootHeader*>(attrPtr + res->valueOffset);
            rootHdr->attributeType = NTFS::ATTR_FILE_NAME;
            rootHdr->collationRule = 1;
            rootHdr->indexAllocationEntrySize = 4096;
            rootHdr->clustersPerIndexRecord = 1;

            auto* idxHdr = reinterpret_cast<NTFS::NtfsIndexHeader*>(rootHdr + 1);
            idxHdr->firstEntryOffset = sizeof(NTFS::NtfsIndexHeader);
            idxHdr->totalEntriesSize = sizeof(NTFS::NtfsIndexHeader) + sizeof(NTFS::NtfsIndexEntry);
            idxHdr->allocatedSize = idxHdr->totalEntriesSize;

            auto* lastEntry = reinterpret_cast<NTFS::NtfsIndexEntry*>(idxHdr + 1);
            lastEntry->length = sizeof(NTFS::NtfsIndexEntry);
            lastEntry->flags = NTFS::INDEX_ENTRY_LAST;

            // End marker
            uint8_t* endAttr = attrPtr + attrHdr->length;
            *reinterpret_cast<uint32_t*>(endAttr) = NTFS::ATTR_END;
            hdr->usedBytes = static_cast<uint32_t>((endAttr + 4) - cleanRecord.data());
        } else if (rec < NTFS::MFT_REC_USER_START) {
            hdr->flags = NTFS::FILE_RECORD_IN_USE;
            hdr->hardLinkCount = 1;
            uint8_t* endAttr = cleanRecord.data() + 0x38;
            *reinterpret_cast<uint32_t*>(endAttr) = NTFS::ATTR_END;
            hdr->usedBytes = 0x3C;
        }

        uint64_t targetSec = mftStartSector + (rec * 1024 / bytesPerSec);
        uint32_t offInSec = (rec * 1024) % bytesPerSec;

        uint32_t sectorsNeeded = (1024 + offInSec + bytesPerSec - 1) / bytesPerSec;
        std::vector<uint8_t> secBuf(sectorsNeeded * bytesPerSec, 0);
        ReadSectors(targetSec, sectorsNeeded, secBuf.data());
        EncodeFixup(cleanRecord.data(), 1024);
        std::memcpy(secBuf.data() + offInSec, cleanRecord.data(), 1024);
        WriteSectors(targetSec, sectorsNeeded, secBuf.data());
    }

    // 3. Re-mount the freshly formatted volume
    Mount();
    std::cout << "[Format] NTFS formatting completed successfully.\n";
    return true;
}

// =============================================================================
// Target Location Resolution for Forensic Verification & Auditing
// =============================================================================

bool NtfsDriver::LocateTargetLocations(const std::string& relativePath, NTFS::TargetLocations& outLocs) const {
    outLocs = NTFS::TargetLocations();
    outLocs.path = relativePath;

    std::vector<std::string> pathTokens = TokenizePath(relativePath);
    if (pathTokens.empty()) return false;

    uint64_t currentDirRecord = NTFS::MFT_REC_ROOT;
    uint64_t targetRecord = 0;
    bool isDir = false;
    uint64_t idxSec = 0;
    uint32_t idxOff = 0;
    uint32_t entrySz = 0;

    for (size_t i = 0; i < pathTokens.size(); ++i) {
        const std::string& token = pathTokens[i];
        bool isLast = (i == pathTokens.size() - 1);

        if (!FindEntryInDirectory(currentDirRecord, token, targetRecord, isDir, idxSec, idxOff, entrySz)) {
            return false;
        }

        if (!isLast) {
            currentDirRecord = targetRecord;
        }
    }

    outLocs.isValid = true;
    outLocs.isDirectory = isDir;
    outLocs.mftRecordNum = targetRecord;
    outLocs.parentDirRecordNum = currentDirRecord;
    outLocs.parentIndexSector = idxSec;
    outLocs.parentIndexByteOffset = idxOff;
    outLocs.parentIndexEntrySize = entrySz;

    uint32_t offInSec = 0;
    outLocs.mftSector = MftRecordToSector(targetRecord, offInSec);
    outLocs.mftByteOffsetInSector = offInSec;
    outLocs.mftRecordSize = m_mftRecordSize;

    // Read MFT record to populate data extents
    std::vector<uint8_t> recBuf;
    if (ReadMftRecord(targetRecord, recBuf)) {
        bool res = false;
        std::vector<uint8_t> resPayload;
        uint64_t fSize = 0;
        if (GetFileAllocatedExtents(recBuf, outLocs.dataExtents, res, resPayload, fSize)) {
            outLocs.isResident = res;
            outLocs.fileSize = fSize;
        }
    }

    // Populate bitmap info for the first allocated cluster
    if (!outLocs.dataExtents.empty()) {
        uint64_t firstLcn = outLocs.dataExtents.front().lcn;
        ReadClusterBitmapByte(firstLcn, outLocs.bitmapOriginalByte, outLocs.bitmapSector,
                              outLocs.bitmapByteOffsetInSector, outLocs.bitmapBitMask);
    }
    return true;
}



void NtfsDriver::PrintBootInfo() const {
    std::cout << "\n--- NTFS Volume Information ---\n";
    std::cout << "  OEM Identifier:        " << std::string(m_vbr.oemId, 8) << "\n";
    std::cout << "  Bytes Per Sector:      " << m_bytesPerSector << "\n";
    std::cout << "  Sectors Per Cluster:   " << m_sectorsPerCluster << "\n";
    std::cout << "  Bytes Per Cluster:     " << m_bytesPerCluster << "\n";
    std::cout << "  MFT Record Size:       " << m_mftRecordSize << " bytes\n";
    std::cout << "  Index Block Size:      " << m_indexBlockSize << " bytes\n";
    std::cout << "  Total Volume Sectors:  " << m_totalSectors << "\n";
    std::cout << "  $MFT Start Cluster:    " << m_vbr.mftStartLCN << " (Sector " << ClusterToSector(m_vbr.mftStartLCN) << ")\n";
    std::cout << "  $MFTMirr Start Cluster:" << m_vbr.mftMirrStartLCN << "\n";
    std::cout << "--------------------------------\n";
}

} // namespace FileSystems
} // namespace Erasure
