#include "NTFSFileSystem.h"
#include "DataRunParser.h"

#include <cstring>
#include <algorithm>

namespace Recovery {
namespace Filesystems {
namespace NTFS {

NTFSFileSystem::NTFSFileSystem(Core::ByteReader& reader,
                               const Core::StorageRegion& partition)
    : m_reader(reader)
    , m_partition(partition)
    , m_mounted(false)
    , m_bytesPerSector(0)
    , m_sectorsPerCluster(0)
    , m_bytesPerCluster(0)
    , m_bytesPerMftRecord(0)
    , m_mftLCN(0)
    , m_mftStartOffset(0)
{
}

// ---- Mount: parse boot sector and walk MFT ----

bool NTFSFileSystem::Mount() {
    if (m_mounted) return true;

    // 1. Read and validate the NTFS boot sector
    NTFSBootSector boot;
    if (!m_reader.ReadStruct(m_partition.startOffset, boot))
        return false;

    // Validate OEM ID
    if (std::memcmp(boot.oemId, "NTFS    ", 8) != 0)
        return false;

    // Extract BPB parameters
    m_bytesPerSector    = boot.bytesPerSector;
    m_sectorsPerCluster = boot.sectorsPerCluster;

    // Sanity checks
    if (m_bytesPerSector == 0 || m_sectorsPerCluster == 0)
        return false;

    m_bytesPerCluster = m_bytesPerSector * m_sectorsPerCluster;

    // Decode MFT record size
    m_bytesPerMftRecord = DecodeMftRecordSize(boot.mftRecordSize, m_bytesPerCluster);
    if (m_bytesPerMftRecord == 0 || m_bytesPerMftRecord > 65536)
        return false;

    // Compute MFT absolute byte offset
    m_mftLCN = boot.mftLCN;
    m_mftStartOffset = m_partition.startOffset + m_mftLCN * m_bytesPerCluster;

    // 2. Walk MFT entries
    //    Strategy: Read $MFT (record 0) to get the MFT's own data runs,
    //    then walk all MFT records sequentially through those data runs.
    //    Fallback: if record 0 has no data runs, walk contiguously.

    MFTParser parser(m_reader, m_partition.startOffset,
                     m_bytesPerMftRecord, m_bytesPerCluster, m_bytesPerSector);

    // First, parse record 0 ($MFT) to get MFT data runs
    MFTFileInfo mftSelf;
    if (!parser.ParseEntry(m_mftStartOffset, 0, mftSelf))
        return false;

    // Determine MFT data ranges
    std::vector<Core::DataRange> mftDataRanges;

    if (!mftSelf.dataRanges.empty()) {
        mftDataRanges = mftSelf.dataRanges;
    } else if (mftSelf.hasResidentData) {
        // Extremely unlikely for $MFT but handle it
        return false;
    } else {
        // Fallback: assume MFT is contiguous starting at mftStartOffset
        // Use the partition size as an upper bound
        mftDataRanges.emplace_back(m_mftStartOffset,
                                   m_partition.size - (m_mftLCN * m_bytesPerCluster));
    }

    // Add record 0 to our entries
    m_mftEntries.push_back(std::move(mftSelf));
    m_mftIndexMap[0] = 0;

    // Walk all MFT records through the data runs
    uint64_t globalMftIndex = 1;  // Start from record 1 (record 0 already parsed)

    for (const auto& range : mftDataRanges) {
        if (range.offset == 0 && range.length == 0) continue;  // Skip empty/sparse

        uint64_t rangeStart = range.offset;
        uint64_t rangeEnd   = range.offset + range.length;

        // Calculate start position within this range
        uint64_t startInRange;
        if (globalMftIndex * m_bytesPerMftRecord < range.length &&
            rangeStart == m_mftStartOffset) {
            // First range — skip records we've already parsed
            startInRange = rangeStart + globalMftIndex * m_bytesPerMftRecord;
        } else if (rangeStart == m_mftStartOffset) {
            startInRange = rangeStart + globalMftIndex * m_bytesPerMftRecord;
        } else {
            startInRange = rangeStart;
        }

        for (uint64_t pos = startInRange; pos + m_bytesPerMftRecord <= rangeEnd; pos += m_bytesPerMftRecord) {
            MFTFileInfo info;
            if (parser.ParseEntry(pos, globalMftIndex, info)) {
                m_mftIndexMap[globalMftIndex] = m_mftEntries.size();
                m_mftEntries.push_back(std::move(info));
            }
            // Even if parsing fails (zeroed record), advance the index
            globalMftIndex++;

            // Safety cap: don't try to parse millions of records in a test
            if (globalMftIndex > 100000) break;
        }

        if (globalMftIndex > 100000) break;
    }

    // 3. Build FileRecord objects with paths
    BuildFileRecords();

    m_mounted = true;
    return true;
}

Core::FileSystemType NTFSFileSystem::GetType() const {
    return Core::FileSystemType::NTFS;
}

// ---- Path reconstruction ----

std::string NTFSFileSystem::BuildPath(uint64_t mftIndex) {
    // Walk parent references to build the full path
    std::vector<std::string> components;

    uint64_t current = mftIndex;
    int maxDepth = 256;  // Prevent infinite loops

    while (maxDepth-- > 0) {
        auto it = m_mftIndexMap.find(current);
        if (it == m_mftIndexMap.end()) break;

        const auto& entry = m_mftEntries[it->second];

        // Root directory references itself as parent
        if (current == MFT_RECORD_ROOT_DIR) break;

        // System MFT records (0-4) without names — stop
        if (entry.filename.empty()) break;

        components.push_back(entry.filename);
        current = entry.parentMftIndex;

        // If parent is root, stop
        if (current == MFT_RECORD_ROOT_DIR) break;
    }

    // Reverse to get top-down order
    std::reverse(components.begin(), components.end());

    // Join with backslash (NTFS convention)
    std::string path;
    for (size_t i = 0; i < components.size(); ++i) {
        if (i > 0) path += '\\';
        path += components[i];
    }

    return path;
}

// ---- Build FileRecords ----

void NTFSFileSystem::BuildFileRecords() {
    m_fileRecords.clear();
    m_fileIdMap.clear();

    uint64_t nextId = 1;

    for (const auto& mft : m_mftEntries) {
        // Skip system metadata records (0-23) that aren't user-visible
        // except root directory (5) which is useful for structure
        if (mft.mftIndex < MFT_RECORD_FIRST_USER && mft.mftIndex != MFT_RECORD_ROOT_DIR)
            continue;

        // Skip entries with no filename
        if (mft.filename.empty()) continue;

        Core::FileRecord record;
        record.id                = nextId++;
        record.filename          = mft.filename;
        record.path              = BuildPath(mft.mftIndex);
        record.size              = mft.fileSize;
        record.createdTime       = mft.createdTime;
        record.modifiedTime      = mft.modifiedTime;
        record.accessedTime      = mft.accessedTime;
        record.allocated         = mft.inUse;
        record.deleted           = !mft.inUse;
        record.orphaned          = false;  // TODO: detect orphans in future phases
        record.filesystem        = Core::FileSystemType::NTFS;
        record.filesystemRecordId = mft.mftIndex;
        record.dataRanges        = mft.dataRanges;

        // Extract extension
        auto dotPos = mft.filename.rfind('.');
        if (dotPos != std::string::npos && dotPos < mft.filename.size() - 1) {
            record.extension = mft.filename.substr(dotPos + 1);
        }

        m_fileIdMap[record.id] = m_fileRecords.size();
        m_fileRecords.push_back(std::move(record));
    }

    // Mark orphans: files whose parent directory was not found
    for (auto& rec : m_fileRecords) {
        if (rec.filesystemRecordId == MFT_RECORD_ROOT_DIR) continue;

        auto mftIt = m_mftIndexMap.find(rec.filesystemRecordId);
        if (mftIt != m_mftIndexMap.end()) {
            const auto& mftEntry = m_mftEntries[mftIt->second];
            auto parentIt = m_mftIndexMap.find(mftEntry.parentMftIndex);
            if (parentIt == m_mftIndexMap.end() &&
                mftEntry.parentMftIndex != MFT_RECORD_ROOT_DIR) {
                rec.orphaned = true;
            }
        }
    }
}

// ---- IRecoveryFileSystem: EnumerateFiles ----

bool NTFSFileSystem::EnumerateFiles(std::vector<Core::FileRecord>& results) {
    if (!m_mounted) return false;
    results.insert(results.end(), m_fileRecords.begin(), m_fileRecords.end());
    return true;
}

// ---- IRecoveryFileSystem: GetFileMetadata ----

bool NTFSFileSystem::GetFileMetadata(uint64_t fileId, Core::FileRecord& metadata) {
    if (!m_mounted) return false;

    auto it = m_fileIdMap.find(fileId);
    if (it == m_fileIdMap.end()) return false;

    metadata = m_fileRecords[it->second];
    return true;
}

// ---- IRecoveryFileSystem: GetDataRanges ----

bool NTFSFileSystem::GetDataRanges(uint64_t fileId, std::vector<Core::DataRange>& ranges) {
    if (!m_mounted) return false;

    auto it = m_fileIdMap.find(fileId);
    if (it == m_fileIdMap.end()) return false;

    const auto& rec = m_fileRecords[it->second];

    // For resident data, there are no disk ranges — data is inline
    // Return the ranges from the FileRecord (which may be empty for resident data)
    ranges = rec.dataRanges;
    return true;
}

// ---- Data range reading ----

bool NTFSFileSystem::ReadDataRanges(const std::vector<Core::DataRange>& ranges,
                                    uint64_t expectedSize,
                                    std::vector<uint8_t>& output)
{
    output.clear();
    output.reserve(static_cast<size_t>(expectedSize));

    for (const auto& range : ranges) {
        if (range.offset == 0 && range.length > 0) {
            // Sparse run — fill with zeros
            size_t needed = static_cast<size_t>(
                std::min(range.length, expectedSize - output.size()));
            output.insert(output.end(), needed, 0);
        } else {
            // Read from disk
            uint64_t toRead = range.length;
            if (output.size() + toRead > expectedSize)
                toRead = expectedSize - output.size();

            if (toRead == 0) break;

            std::vector<uint8_t> buf;
            // Read in chunks to avoid huge allocations
            uint64_t chunkSize = 1024 * 1024;  // 1 MB chunks
            uint64_t rangeOffset = range.offset;
            uint64_t remaining = toRead;

            while (remaining > 0) {
                uint32_t readSize = static_cast<uint32_t>(
                    std::min(remaining, chunkSize));
                buf.resize(readSize);
                if (!m_reader.ReadBytes(rangeOffset, readSize, buf.data()))
                    return false;
                output.insert(output.end(), buf.begin(), buf.end());
                rangeOffset += readSize;
                remaining -= readSize;
            }
        }

        if (output.size() >= expectedSize) break;
    }

    // Truncate to expected size if we read too much
    if (output.size() > expectedSize)
        output.resize(static_cast<size_t>(expectedSize));

    return true;
}

// ---- IRecoveryFileSystem: ReadFile ----

bool NTFSFileSystem::ReadFile(uint64_t fileId, std::vector<uint8_t>& output) {
    if (!m_mounted) return false;

    auto it = m_fileIdMap.find(fileId);
    if (it == m_fileIdMap.end()) return false;

    const auto& rec = m_fileRecords[it->second];

    // Find the corresponding MFT entry
    auto mftIt = m_mftIndexMap.find(rec.filesystemRecordId);
    if (mftIt == m_mftIndexMap.end()) return false;

    const auto& mftEntry = m_mftEntries[mftIt->second];

    if (mftEntry.hasResidentData) {
        // Resident: data is inline in the MFT record
        output = mftEntry.residentData;
        return true;
    }

    if (mftEntry.dataRanges.empty()) {
        // No data (possibly a directory or empty file)
        output.clear();
        return true;
    }

    // Non-resident: read from data ranges
    return ReadDataRanges(mftEntry.dataRanges, mftEntry.fileSize, output);
}

} // namespace NTFS
} // namespace Filesystems
} // namespace Recovery
