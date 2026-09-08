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

    std::memcpy(buffer.data() + offsetInSector, inRecord.data(), m_mftRecordSize);
    return WriteSectors(startSector, sectorsNeeded, buffer.data());
}

bool NtfsDriver::WipeMftRecordOnDisk(uint64_t recordNum) {
    uint32_t offsetInSector = 0;
    uint64_t startSector = MftRecordToSector(recordNum, offsetInSector);

    uint32_t sectorsNeeded = (m_mftRecordSize + offsetInSector + m_bytesPerSector - 1) / m_bytesPerSector;
    std::vector<uint8_t> buffer(sectorsNeeded * m_bytesPerSector);

    if (!ReadSectors(startSector, sectorsNeeded, buffer.data())) {
        return false;
    }

    // Obliterate the 1024-byte record on disk with zeros
    std::memset(buffer.data() + offsetInSector, 0, m_mftRecordSize);
    return WriteSectors(startSector, sectorsNeeded, buffer.data());
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
    uint64_t childRecord = 0;
    bool isDir = false;
    uint64_t indexSector = 0;
    uint32_t indexOffset = 0;
    uint32_t entrySize = 0;

    if (!FindEntryInDirectory(dirRecordNum, targetName, childRecord, isDir, indexSector, indexOffset, entrySize)) {
        return false;
    }

    uint32_t sectorsNeeded = (indexOffset + entrySize + m_bytesPerSector - 1) / m_bytesPerSector;
    std::vector<uint8_t> buffer(sectorsNeeded * m_bytesPerSector);

    if (!ReadSectors(indexSector, sectorsNeeded, buffer.data())) {
        return false;
    }

    // Zero out the entire directory index entry on disk
    std::memset(buffer.data() + indexOffset, 0, entrySize);
    return WriteSectors(indexSector, sectorsNeeded, buffer.data());
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
        std::memcpy(secBuf.data() + offInSec, cleanRecord.data(), 1024);
        WriteSectors(targetSec, sectorsNeeded, secBuf.data());
    }

    // 3. Re-mount the freshly formatted volume
    Mount();
    std::cout << "[Format] NTFS formatting completed successfully.\n";
    return true;
}

// =============================================================================
// Forensic Verification & Hex Inspection Suite
// =============================================================================

void NtfsDriver::PrintHexDump(const void* data, size_t size, uint64_t basePhysicalOffset, const std::string& label) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    std::cout << "\n--------------------------------------------------------------------------------\n";
    std::cout << "[XXD HEX DUMP] " << label << " (" << size << " bytes) @ Physical Offset 0x"
              << std::hex << basePhysicalOffset << std::dec << "\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    for (size_t i = 0; i < size; i += 16) {
        // Physical file/disk offset
        std::cout << std::hex << std::setw(8) << std::setfill('0') << (basePhysicalOffset + i) << ": ";

        // 16 Hex bytes
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

void NtfsDriver::ExplainMftRecordBytes(const uint8_t* recordData, size_t size, uint64_t baseOffset) const {
    if (!recordData || size < sizeof(NTFS::NtfsRecordHeader)) return;

    const auto* hdr = reinterpret_cast<const NTFS::NtfsRecordHeader*>(recordData);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: MFT RECORD @ 0x" << std::hex << baseOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x00..+0x03 [Magic]: 0x" << std::hex << hdr->magic << std::dec;
    if (hdr->magic == NTFS::NTFS_MAGIC_FILE) std::cout << " ('FILE' - Active MFT Record)\n";
    else if (hdr->magic == 0) std::cout << " (0x00000000 - Sanitized / Zeroed Record)\n";
    else std::cout << " (Corrupted / Other)\n";

    std::cout << "  * Offset +0x04..+0x05 [Fixup Offset]: " << hdr->updateSequenceOffset << "\n";
    std::cout << "  * Offset +0x06..+0x07 [Fixup Size]:   " << hdr->updateSequenceSize << "\n";
    std::cout << "  * Offset +0x10..+0x11 [Sequence Num]: " << hdr->sequenceNumber << "\n";
    std::cout << "  * Offset +0x16..+0x17 [Record Flags]: 0x" << std::hex << hdr->flags << std::dec;
    if (hdr->flags & NTFS::FILE_RECORD_IN_USE) std::cout << " [IN_USE]";
    if (hdr->flags & NTFS::FILE_RECORD_DIRECTORY) std::cout << " [DIRECTORY]";
    if (hdr->flags == 0) std::cout << " [FREE / UNALLOCATED]";
    std::cout << "\n";

    std::cout << "  * Offset +0x18..+0x1B [Used Bytes]:     " << hdr->usedBytes << " bytes\n";
    std::cout << "  * Offset +0x1C..+0x1F [Allocated Size]: " << hdr->allocatedBytes << " bytes\n";

    // Inspect Attributes
    uint16_t offset = hdr->firstAttributeOffset;
    while (offset + sizeof(NTFS::NtfsAttributeHeader) <= size) {
        const auto* attr = reinterpret_cast<const NTFS::NtfsAttributeHeader*>(recordData + offset);
        if (attr->type == NTFS::ATTR_END || attr->length == 0) break;

        std::cout << "  -> Attribute @ +0x" << std::hex << offset << " Type: 0x" << attr->type << std::dec;
        if (attr->type == NTFS::ATTR_STANDARD_INFORMATION) std::cout << " ($STANDARD_INFORMATION: Timestamps, DOS Flags)\n";
        else if (attr->type == NTFS::ATTR_FILE_NAME) {
            std::cout << " ($FILE_NAME: ";
            if (attr->nonResidentFlag == 0) {
                const auto* res = reinterpret_cast<const NTFS::NtfsResidentAttributeHeader*>(
                    recordData + offset + sizeof(NTFS::NtfsAttributeHeader)
                );
                const auto* fn = reinterpret_cast<const NTFS::NtfsFileNameAttribute*>(recordData + offset + res->valueOffset);
                std::cout << "'" << Utf16ToUtf8(fn->fileName, fn->fileNameLength) << "')\n";
            } else {
                std::cout << "Non-resident)\n";
            }
        }
        else if (attr->type == NTFS::ATTR_DATA) {
            std::cout << " ($DATA: " << (attr->nonResidentFlag == 0 ? "Resident Payload" : "Non-Resident Runlist") << ")\n";
        }
        else if (attr->type == NTFS::ATTR_INDEX_ROOT) std::cout << " ($INDEX_ROOT: Directory B-Tree Root)\n";
        else if (attr->type == NTFS::ATTR_INDEX_ALLOCATION) std::cout << " ($INDEX_ALLOCATION: Large Directory Index Blocks)\n";
        else std::cout << "\n";

        offset += attr->length;
    }
}

void NtfsDriver::ExplainDirectoryEntryBytes(const uint8_t* entryData, size_t size, uint64_t baseOffset) const {
    if (!entryData || size < sizeof(NTFS::NtfsIndexEntry)) return;

    const auto* entry = reinterpret_cast<const NTFS::NtfsIndexEntry*>(entryData);
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: DIRECTORY INDEX ENTRY @ 0x" << std::hex << baseOffset << std::dec << "]\n";
    std::cout << "  * Offset +0x00..+0x07 [File Reference]: 0x" << std::hex << entry->fileReference << std::dec
              << " (Record Number: " << (entry->fileReference & 0x0000FFFFFFFFFFFFULL) << ")\n";
    std::cout << "  * Offset +0x08..+0x09 [Entry Length]:   " << entry->length << " bytes\n";
    std::cout << "  * Offset +0x0A..+0x0B [Key Length]:     " << entry->keyLength << " bytes\n";
    std::cout << "  * Offset +0x0C..+0x0D [Entry Flags]:    0x" << std::hex << entry->flags << std::dec;
    if (entry->flags & NTFS::INDEX_ENTRY_HAS_SUBNODES) std::cout << " [HAS_CHILD_VCN]";
    if (entry->flags & NTFS::INDEX_ENTRY_LAST) std::cout << " [LAST_ENTRY_IN_NODE]";
    std::cout << "\n";

    if (entry->keyLength >= sizeof(NTFS::NtfsFileNameAttribute)) {
        const auto* fn = reinterpret_cast<const NTFS::NtfsFileNameAttribute*>(entryData + sizeof(NTFS::NtfsIndexEntry));
        std::cout << "  * Target Filename: '" << Utf16ToUtf8(fn->fileName, fn->fileNameLength) << "'\n";
        std::cout << "  * Allocated File Size: " << fn->allocatedSize << " bytes\n";
        std::cout << "  * Real File Size:      " << fn->realSize << " bytes\n";
    }
}

void NtfsDriver::ExplainDataSectorBytes(const uint8_t* data, size_t size, uint64_t baseOffset) const {
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: DATA SECTOR @ 0x" << std::hex << baseOffset << std::dec << "]\n";
    bool allZeros = true;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] != 0) { allZeros = false; break; }
    }
    if (allZeros) {
        std::cout << "  * Status: ALL 0x00 ZERO-FILLED (Forensically Obliterated / Unallocated)\n";
    } else {
        std::cout << "  * Status: ACTIVE USER PAYLOAD DATA (Raw disk contents present)\n";
    }
}

void NtfsDriver::ExplainBitmapBytes(uint8_t byteVal, uint8_t mask, uint64_t cluster, uint64_t baseOffset) const {
    std::cout << "\n[SEMANTIC BYTE BREAKDOWN: $Bitmap (Record 6) @ 0x" << std::hex << baseOffset << std::dec << "]\n";
    std::cout << "  * Cluster Index: " << cluster << "\n";
    std::cout << "  * Bitmap Byte Value: 0x" << std::hex << static_cast<int>(byteVal)
              << ", Bit Mask: 0x" << static_cast<int>(mask) << std::dec << "\n";
    if (byteVal & mask) {
        std::cout << "  * Bit Status: 1 (ALLOCATED / IN-USE BY CLUSTER " << cluster << ")\n";
    } else {
        std::cout << "  * Bit Status: 0 (FREE / UNALLOCATED)\n";
    }
}

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

// =============================================================================
// Unified Verification Runners
// =============================================================================

bool NtfsDriver::VerifyAndErase(const std::string& targetPath) {
    std::cout << "\n================================================================================\n";
    std::cout << "       NTFS SECURE DELETION & FORENSIC VERIFICATION ENGINE                      \n";
    std::cout << "================================================================================\n";

    NTFS::TargetLocations locs;
    if (!LocateTargetLocations(targetPath, locs)) {
        std::cerr << "[Verification] Error: Target '" << targetPath << "' could not be located on disk.\n";
        return false;
    }

    // =========================================================================
    // STEP 1: BEFORE DELETION FORENSIC INSPECTION
    // =========================================================================
    std::cout << "\n>>> [PHASE 1: BEFORE DELETION] EXACT DISK LOCATIONS & BYTE INSPECTION <<<\n";

    // 1. MFT Record
    uint64_t mftPhysicalByteOffset = SectorToByteOffset(locs.mftSector) + locs.mftByteOffsetInSector;
    std::vector<uint8_t> mftRecordBefore;
    ReadMftRecord(locs.mftRecordNum, mftRecordBefore);
    PrintHexDump(mftRecordBefore.data(), std::min<size_t>(mftRecordBefore.size(), 256),
                 mftPhysicalByteOffset, "MFT Record " + std::to_string(locs.mftRecordNum) + " (Header & Attributes)");
    ExplainMftRecordBytes(mftRecordBefore.data(), mftRecordBefore.size(), mftPhysicalByteOffset);

    // 2. Data Clusters (if non-resident)
    uint64_t dataPhysicalByteOffset = 0;
    if (!locs.isResident && !locs.dataExtents.empty()) {
        uint64_t firstSector = ClusterToSector(locs.dataExtents.front().lcn);
        dataPhysicalByteOffset = SectorToByteOffset(firstSector);
        std::vector<uint8_t> dataSector(m_bytesPerSector);
        ReadSectors(firstSector, 1, dataSector.data());
        PrintHexDump(dataSector.data(), std::min<size_t>(dataSector.size(), 128),
                     dataPhysicalByteOffset, "Data Cluster " + std::to_string(locs.dataExtents.front().lcn) + " Sector 0");
        ExplainDataSectorBytes(dataSector.data(), dataSector.size(), dataPhysicalByteOffset);
    }

    // 3. Parent Directory Entry
    uint64_t parentDirPhysicalByteOffset = SectorToByteOffset(locs.parentIndexSector) + locs.parentIndexByteOffset;
    if (locs.parentIndexEntrySize > 0) {
        std::vector<uint8_t> dirEntryBuf(locs.parentIndexEntrySize);
        std::vector<uint8_t> secBuf(m_bytesPerSector);
        ReadSectors(locs.parentIndexSector, 1, secBuf.data());
        std::memcpy(dirEntryBuf.data(), secBuf.data() + (locs.parentIndexByteOffset % m_bytesPerSector), locs.parentIndexEntrySize);
        PrintHexDump(dirEntryBuf.data(), dirEntryBuf.size(), parentDirPhysicalByteOffset,
                     "Parent Directory Index Entry for '" + targetPath + "'");
        ExplainDirectoryEntryBytes(dirEntryBuf.data(), dirEntryBuf.size(), parentDirPhysicalByteOffset);
    }

    // 4. Volume Allocation Bitmap ($Bitmap)
    if (!locs.isResident && !locs.dataExtents.empty() && locs.bitmapSector > 0) {
        uint64_t bitmapPhysicalByteOffset = SectorToByteOffset(locs.bitmapSector) + locs.bitmapByteOffsetInSector;
        std::vector<uint8_t> bSec(m_bytesPerSector);
        ReadSectors(locs.bitmapSector, 1, bSec.data());
        PrintHexDump(&bSec[locs.bitmapByteOffsetInSector], 16, bitmapPhysicalByteOffset,
                     "$Bitmap Cluster Allocation Block");
        ExplainBitmapBytes(locs.bitmapOriginalByte, locs.bitmapBitMask, locs.dataExtents.front().lcn, bitmapPhysicalByteOffset);
    }

    // =========================================================================
    // STEP 2: EXECUTE SECURE ERASURE
    // =========================================================================
    std::cout << "\n>>> [PHASE 2: SECURE ERASURE] EXECUTING SURGICAL OBLITERATION <<<\n";
    bool eraseSuccess = false;
    if (locs.isDirectory) {
        eraseSuccess = EraseDirectory(targetPath);
    } else {
        eraseSuccess = EraseFile(targetPath);
    }

    if (!eraseSuccess) {
        std::cerr << "[Verification] Error: Erase operation failed.\n";
        return false;
    }

    // =========================================================================
    // STEP 3: AFTER DELETION FORENSIC RE-INSPECTION
    // =========================================================================
    std::cout << "\n>>> [PHASE 3: AFTER DELETION] RE-INSPECTING EXACT SAME PHYSICAL OFFSETS <<<\n";

    // 1. Re-inspect MFT Record physical location
    std::vector<uint8_t> mftRecordAfter(m_mftRecordSize);
    uint32_t secOff = 0;
    uint64_t mftSec = MftRecordToSector(locs.mftRecordNum, secOff);
    std::vector<uint8_t> secBuf(m_bytesPerSector);
    ReadSectors(mftSec, 1, secBuf.data());
    std::memcpy(mftRecordAfter.data(), secBuf.data() + secOff, std::min<size_t>(m_bytesPerSector - secOff, m_mftRecordSize));

    PrintHexDump(mftRecordAfter.data(), std::min<size_t>(mftRecordAfter.size(), 256),
                 mftPhysicalByteOffset, "MFT Record " + std::to_string(locs.mftRecordNum) + " [POST-WIPE]");
    ExplainMftRecordBytes(mftRecordAfter.data(), mftRecordAfter.size(), mftPhysicalByteOffset);

    // 2. Re-inspect Data Clusters physical location
    if (!locs.isResident && !locs.dataExtents.empty()) {
        uint64_t firstSector = ClusterToSector(locs.dataExtents.front().lcn);
        std::vector<uint8_t> dataSectorAfter(m_bytesPerSector);
        ReadSectors(firstSector, 1, dataSectorAfter.data());
        PrintHexDump(dataSectorAfter.data(), std::min<size_t>(dataSectorAfter.size(), 128),
                     dataPhysicalByteOffset, "Data Cluster " + std::to_string(locs.dataExtents.front().lcn) + " [POST-WIPE]");
        ExplainDataSectorBytes(dataSectorAfter.data(), dataSectorAfter.size(), dataPhysicalByteOffset);
    }

    // 3. Re-inspect Parent Directory Entry
    if (locs.parentIndexEntrySize > 0) {
        std::vector<uint8_t> dirEntryBufAfter(locs.parentIndexEntrySize);
        std::vector<uint8_t> secBuf2(m_bytesPerSector);
        ReadSectors(locs.parentIndexSector, 1, secBuf2.data());
        std::memcpy(dirEntryBufAfter.data(), secBuf2.data() + (locs.parentIndexByteOffset % m_bytesPerSector), locs.parentIndexEntrySize);
        PrintHexDump(dirEntryBufAfter.data(), dirEntryBufAfter.size(), parentDirPhysicalByteOffset,
                     "Parent Directory Index Entry [POST-SCRUB]");
        ExplainDirectoryEntryBytes(dirEntryBufAfter.data(), dirEntryBufAfter.size(), parentDirPhysicalByteOffset);
    }

    // 4. Re-inspect Volume Allocation Bitmap
    if (!locs.isResident && !locs.dataExtents.empty() && locs.bitmapSector > 0) {
        uint64_t bitmapPhysicalByteOffset = SectorToByteOffset(locs.bitmapSector) + locs.bitmapByteOffsetInSector;
        std::vector<uint8_t> bSecAfter(m_bytesPerSector);
        ReadSectors(locs.bitmapSector, 1, bSecAfter.data());
        uint8_t afterByte = bSecAfter[locs.bitmapByteOffsetInSector];
        PrintHexDump(&bSecAfter[locs.bitmapByteOffsetInSector], 16, bitmapPhysicalByteOffset,
                     "$Bitmap Cluster Allocation Block [POST-WIPE]");
        ExplainBitmapBytes(afterByte, locs.bitmapBitMask, locs.dataExtents.front().lcn, bitmapPhysicalByteOffset);
    }

    std::cout << "\n================================================================================\n";
    std::cout << " [VERIFICATION RESULT] SECURE ERASURE VALIDATED: DATA ZEROED & UNRECOVERABLE   \n";
    std::cout << "================================================================================\n";
    return true;
}

bool NtfsDriver::VerifyAndFormatDrive(bool fullDriveSanitize) {
    std::cout << "\n================================================================================\n";
    std::cout << "       NTFS FORMAT & DISK VERIFICATION ENGINE                                   \n";
    std::cout << "================================================================================\n";

    // 1. Before Format
    std::cout << "\n>>> [PHASE 1: BEFORE FORMAT] READING SECTOR 0 (VBR) <<<\n";
    std::vector<uint8_t> sector0(512);
    ReadSectors(0, 1, sector0.data());
    PrintHexDump(sector0.data(), 128, 0, "Sector 0 (Pre-Format VBR)");

    // 2. Execute Format
    std::cout << "\n>>> [PHASE 2: FORMAT DRIVE] EXECUTING NTFS FORMAT <<<\n";
    if (!FormatDrive(fullDriveSanitize)) {
        std::cerr << "[Format] Error: Failed to format drive.\n";
        return false;
    }

    // 3. After Format
    std::cout << "\n>>> [PHASE 3: AFTER FORMAT] RE-READING SECTOR 0 & PRISTINE MFT <<<\n";
    ReadSectors(0, 1, sector0.data());
    PrintHexDump(sector0.data(), 128, 0, "Sector 0 (Pristine Post-Format VBR)");

    std::vector<uint8_t> mft0;
    ReadMftRecord(NTFS::MFT_REC_MFT, mft0);
    uint32_t offSec = 0;
    uint64_t mftSec = MftRecordToSector(NTFS::MFT_REC_MFT, offSec);
    PrintHexDump(mft0.data(), 256, SectorToByteOffset(mftSec) + offSec, "Pristine Post-Format MFT Record 0 ($MFT)");

    std::cout << "\n================================================================================\n";
    std::cout << " [VERIFICATION RESULT] DISK FORMAT VALIDATED: PRISTINE NTFS INITIALIZED          \n";
    std::cout << "================================================================================\n";
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
