#include "FAT32.h"
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

Fat32Driver::Fat32Driver(Core::IHardwareController* hardware)
    : m_hardware(hardware)
    , m_bytesPerSector(0)
    , m_sectorsPerCluster(0)
    , m_bytesPerCluster(0)
    , m_firstDataSector(0)
    , m_totalClusters(0)
    , m_fatStartSector(0)
    , m_fatSizeSectors(0)
    , m_numFATs(0)
    , m_rootCluster(2)
    , m_fsInfoSector(1)
    , m_isMounted(false)
{
    std::memset(&m_bpb, 0, sizeof(m_bpb));
    std::memset(&m_fsInfo, 0, sizeof(m_fsInfo));
}

// =============================================================================
// Geometry & Addressing Helpers
// =============================================================================

uint64_t Fat32Driver::ClusterToSector(uint32_t cluster) const {
    if (cluster < 2) return 0;
    return m_firstDataSector + static_cast<uint64_t>(cluster - 2) * m_sectorsPerCluster;
}

bool Fat32Driver::ReadSectors(uint64_t startSector, uint32_t count, void* buffer) const {
    if (!m_hardware) return false;
    return m_hardware->ReadSectors(startSector, count, buffer);
}

bool Fat32Driver::WriteSectors(uint64_t startSector, uint32_t count, const void* buffer) {
    if (!m_hardware) return false;
    return m_hardware->WriteSectors(startSector, count, buffer);
}

// =============================================================================
// Filesystem Mount & Geometry Extraction
// =============================================================================

bool Fat32Driver::Mount() {
    if (!m_hardware) {
        std::cerr << "[Fat32Driver] Error: Hardware controller is null.\n";
        return false;
    }

    Core::DeviceGeometry geo = m_hardware->GetGeometry();
    if (geo.bytesPerSector == 0) {
        std::cerr << "[Fat32Driver] Error: Invalid sector size (0 bytes).\n";
        return false;
    }

    // Read Sector 0 (The Volume Boot Record)
    std::vector<uint8_t> vbrBuffer(geo.bytesPerSector);
    if (!ReadSectors(0, 1, vbrBuffer.data())) {
        std::cerr << "[Fat32Driver] Error: Failed to read VBR Sector 0.\n";
        return false;
    }

    std::memcpy(&m_bpb, vbrBuffer.data(), sizeof(FAT32::Fat32BootSector));

    // 1. Verify boot signature: 0xAA55
    if (m_bpb.signature != FAT32::FAT32_BOOT_SIGNATURE) {
        std::cerr << "[Fat32Driver] Error: Invalid boot signature 0x"
                  << std::hex << m_bpb.signature << std::dec << " (Expected 0xAA55)\n";
        return false;
    }

    // 2. Validate FAT32 specific BPB fields
    // BytsPerSec must be power of 2, 512..4096
    uint16_t bps = m_bpb.bytesPerSector;
    if (bps < 512 || bps > 4096 || (bps & (bps - 1)) != 0) {
        std::cerr << "[Fat32Driver] Error: Unsupported sector size: " << bps << " bytes.\n";
        return false;
    }

    // SecPerClus must be power of 2, 1..128
    uint8_t spc = m_bpb.sectorsPerCluster;
    if (spc == 0 || spc > 128 || (spc & (spc - 1)) != 0) {
        std::cerr << "[Fat32Driver] Error: Unsupported sectors per cluster: " << (int)spc << "\n";
        return false;
    }

    // FAT32 mandates rootEntryCount == 0, fatSize16 == 0, and fatSize32 > 0
    if (m_bpb.rootEntryCount != 0 || m_bpb.fatSize16 != 0 || m_bpb.fatSize32 == 0) {
        std::cerr << "[Fat32Driver] Error: Volume is not a valid FAT32 filesystem (BPB structure mismatch).\n";
        return false;
    }

    if (m_bpb.reservedSectorCount == 0 || m_bpb.numFATs == 0) {
        std::cerr << "[Fat32Driver] Error: Invalid reservedSectorCount or numFATs.\n";
        return false;
    }

    // 3. Cache geometric parameters
    m_bytesPerSector    = bps;
    m_sectorsPerCluster = spc;
    m_bytesPerCluster   = m_bytesPerSector * m_sectorsPerCluster;
    m_fatStartSector    = m_bpb.reservedSectorCount;
    m_fatSizeSectors    = m_bpb.fatSize32;
    m_numFATs           = m_bpb.numFATs;
    m_firstDataSector   = m_fatStartSector + (m_numFATs * m_fatSizeSectors);
    m_rootCluster       = m_bpb.rootCluster ? m_bpb.rootCluster : 2;
    m_fsInfoSector      = m_bpb.fsInfoSector ? m_bpb.fsInfoSector : 1;

    uint32_t totalSectors = m_bpb.totalSectors32 ? m_bpb.totalSectors32 : m_bpb.totalSectors16;
    if (totalSectors < m_firstDataSector) {
        std::cerr << "[Fat32Driver] Error: totalSectors is smaller than firstDataSector.\n";
        return false;
    }

    uint32_t dataSectors = totalSectors - m_firstDataSector;
    m_totalClusters = dataSectors / m_sectorsPerCluster;

    // 4. Read and validate FSInfo sector
    ReadFSInfo();

    m_isMounted = true;
    return true;
}

// =============================================================================
// FSInfo Management
// =============================================================================

bool Fat32Driver::ReadFSInfo() {
    std::vector<uint8_t> buffer(m_bytesPerSector);
    if (!ReadSectors(m_fsInfoSector, 1, buffer.data())) {
        return false;
    }

    std::memcpy(&m_fsInfo, buffer.data(), sizeof(FAT32::Fat32FSInfo));

    if (m_fsInfo.leadSig != FAT32::FAT32_FSINFO_LEAD_SIG ||
        m_fsInfo.strucSig != FAT32::FAT32_FSINFO_STRUC_SIG ||
        m_fsInfo.trailSig != FAT32::FAT32_FSINFO_TRAIL_SIG) {
        // Non-fatal warning: FSInfo signatures invalid or uninitialized
        return false;
    }
    return true;
}

bool Fat32Driver::WriteFSInfo() {
    std::vector<uint8_t> buffer(m_bytesPerSector, 0);
    std::memcpy(buffer.data(), &m_fsInfo, sizeof(FAT32::Fat32FSInfo));

    // Write primary FSInfo
    bool ok = WriteSectors(m_fsInfoSector, 1, buffer.data());

    // Write backup FSInfo (typically sector 7 if backupBootSector is 6)
    if (m_bpb.backupBootSector > 0) {
        uint16_t backupFSInfoSector = m_bpb.backupBootSector + (m_fsInfoSector - 0);
        WriteSectors(backupFSInfoSector, 1, buffer.data());
    }
    return ok;
}

bool Fat32Driver::UpdateFSInfo(int32_t freeClusterDelta, uint32_t nextFreeHint) {
    if (m_fsInfo.freeCount != 0xFFFFFFFF) {
        int64_t updated = static_cast<int64_t>(m_fsInfo.freeCount) + freeClusterDelta;
        if (updated < 0) updated = 0;
        if (updated > static_cast<int64_t>(m_totalClusters)) updated = m_totalClusters;
        m_fsInfo.freeCount = static_cast<uint32_t>(updated);
    }
    if (nextFreeHint >= 2 && nextFreeHint < m_totalClusters + 2) {
        m_fsInfo.nextFree = nextFreeHint;
    }
    return WriteFSInfo();
}

// =============================================================================
// FAT Table Management
// =============================================================================

uint32_t Fat32Driver::ReadFatEntry(uint32_t cluster) const {
    if (cluster < 2 || cluster >= m_totalClusters + 2) return FAT32::FAT32_CLUSTER_EOC_MAX;

    uint64_t fatOffsetBytes = static_cast<uint64_t>(cluster) * 4;
    uint64_t fatSector = m_fatStartSector + (fatOffsetBytes / m_bytesPerSector);
    uint32_t offsetInSector = static_cast<uint32_t>(fatOffsetBytes % m_bytesPerSector);

    std::vector<uint8_t> sector(m_bytesPerSector);
    if (!ReadSectors(fatSector, 1, sector.data())) {
        return FAT32::FAT32_CLUSTER_EOC_MAX;
    }

    uint32_t rawEntry = *reinterpret_cast<const uint32_t*>(sector.data() + offsetInSector);
    return rawEntry & FAT32::FAT32_CLUSTER_MASK;
}

bool Fat32Driver::WriteFatEntry(uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster >= m_totalClusters + 2) return false;

    // Mask value to 28 bits
    uint32_t val28 = value & FAT32::FAT32_CLUSTER_MASK;

    // Determine active FATs to update
    // BPB_ExtFlags: Bit 7 = 1 means mirroring disabled, bits 0-3 = active FAT
    bool mirroring = (m_bpb.extFlags & 0x0080) == 0;
    uint8_t activeFat = mirroring ? 0 : static_cast<uint8_t>(m_bpb.extFlags & 0x000F);
    uint8_t fatsToUpdate = mirroring ? m_numFATs : 1;

    bool allSuccess = true;
    for (uint8_t f = 0; f < fatsToUpdate; ++f) {
        uint8_t fatIndex = mirroring ? f : activeFat;
        uint64_t fatStart = m_fatStartSector + (static_cast<uint64_t>(fatIndex) * m_fatSizeSectors);
        uint64_t fatOffsetBytes = static_cast<uint64_t>(cluster) * 4;
        uint64_t fatSector = fatStart + (fatOffsetBytes / m_bytesPerSector);
        uint32_t offsetInSector = static_cast<uint32_t>(fatOffsetBytes % m_bytesPerSector);

        std::vector<uint8_t> sector(m_bytesPerSector);
        if (!ReadSectors(fatSector, 1, sector.data())) {
            allSuccess = false;
            continue;
        }

        uint32_t* entryPtr = reinterpret_cast<uint32_t*>(sector.data() + offsetInSector);
        // Preserve high 4 reserved bits
        *entryPtr = (*entryPtr & 0xF0000000) | val28;

        if (!WriteSectors(fatSector, 1, sector.data())) {
            allSuccess = false;
        }
    }
    return allSuccess;
}

std::vector<uint32_t> Fat32Driver::GetClusterChain(uint32_t startCluster) const {
    std::vector<uint32_t> chain;
    if (startCluster < 2 || startCluster >= m_totalClusters + 2) return chain;

    uint32_t current = startCluster;
    uint32_t maxIterations = m_totalClusters + 2;

    while (current >= 2 && current < FAT32::FAT32_CLUSTER_EOC_MIN && maxIterations-- > 0) {
        chain.push_back(current);
        uint32_t next = ReadFatEntry(current);
        if (next == current || next == FAT32::FAT32_CLUSTER_FREE || next == FAT32::FAT32_CLUSTER_BAD) {
            break; // Loop detected or bad entry
        }
        current = next;
    }
    return chain;
}

bool Fat32Driver::FreeClusterChain(const std::vector<uint32_t>& chain) {
    if (chain.empty()) return true;

    for (uint32_t c : chain) {
        WriteFatEntry(c, FAT32::FAT32_CLUSTER_FREE);
    }
    UpdateFSInfo(static_cast<int32_t>(chain.size()), chain[0]);
    return true;
}

// =============================================================================
// Directory Traversal & SFN/LFN Helpers
// =============================================================================

std::vector<std::string> Fat32Driver::TokenizePath(const std::string& path) const {
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

uint8_t Fat32Driver::ComputeSfnChecksum(const uint8_t* pFcbName) {
    uint8_t sum = 0;
    for (int i = 11; i != 0; --i) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *pFcbName++;
    }
    return sum;
}

std::string Fat32Driver::SfnToString(const uint8_t* name11) {
    std::string name;
    for (int i = 0; i < 8; ++i) {
        if (name11[i] == ' ') break;
        name.push_back(static_cast<char>(name11[i]));
    }

    std::string ext;
    for (int i = 8; i < 11; ++i) {
        if (name11[i] == ' ') break;
        ext.push_back(static_cast<char>(name11[i]));
    }

    if (!ext.empty()) {
        return name + "." + ext;
    }
    return name;
}

std::string Fat32Driver::Utf16ToUtf8(const char16_t* utf16Str, size_t length) {
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        char16_t c = utf16Str[i];
        if (c == 0x0000 || c == 0xFFFF) break;
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

bool Fat32Driver::EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

Fat32Driver::DirSearchResult Fat32Driver::FindEntryInDirectory(uint32_t dirCluster, const std::string& targetName) const {
    DirSearchResult result;
    if (dirCluster < 2) return result;

    std::vector<uint32_t> dirClusters = GetClusterChain(dirCluster);
    if (dirClusters.empty()) return result;

    std::vector<uint8_t> clusterBuffer(m_bytesPerCluster);

    // State for accumulating chained LFN records
    struct LfnPiece {
        uint8_t seq;
        std::u16string chars;
    };
    std::vector<LfnPiece> lfnPieces;
    uint8_t currentLfnChecksum = 0;
    size_t currentLfnCount = 0;

    for (uint32_t c : dirClusters) {
        uint64_t sector = ClusterToSector(c);
        if (!ReadSectors(sector, m_sectorsPerCluster, clusterBuffer.data())) {
            continue;
        }

        for (size_t offset = 0; offset + 32 <= m_bytesPerCluster; offset += 32) {
            const uint8_t* raw = clusterBuffer.data() + offset;

            // Free all subsequent entries
            if (raw[0] == FAT32::FAT32_DIR_ENTRY_FREE_ALL) {
                return result; // End of directory
            }

            // Deleted entry
            if (raw[0] == FAT32::FAT32_DIR_ENTRY_DELETED) {
                lfnPieces.clear();
                currentLfnCount = 0;
                continue;
            }

            // Check if LFN entry
            uint8_t attr = raw[11];
            if (attr == FAT32::FAT32_ATTR_LONG_NAME) {
                const auto* lfn = reinterpret_cast<const FAT32::Fat32LfnEntry*>(raw);
                uint8_t seq = lfn->order & FAT32::FAT32_LFN_ORDER_MASK;

                if (lfn->order & FAT32::FAT32_LFN_LAST_ENTRY) {
                    lfnPieces.clear();
                    currentLfnChecksum = lfn->checksum;
                    currentLfnCount = 0;
                }

                std::u16string pieceChars;
                for (int ch = 0; ch < 5; ++ch) pieceChars.push_back(lfn->name1[ch]);
                for (int ch = 0; ch < 6; ++ch) pieceChars.push_back(lfn->name2[ch]);
                for (int ch = 0; ch < 2; ++ch) pieceChars.push_back(lfn->name3[ch]);

                lfnPieces.push_back({ seq, pieceChars });
                currentLfnCount++;
                continue;
            }

            // Short File Name (SFN) entry
            const auto* sfn = reinterpret_cast<const FAT32::Fat32DirEntry*>(raw);

            // Skip Volume ID entries
            if (sfn->attr & FAT32::FAT32_ATTR_VOLUME_ID) {
                lfnPieces.clear();
                currentLfnCount = 0;
                continue;
            }

            // Reconstruct name (LFN if available and matching checksum, else SFN)
            std::string entryName;
            uint8_t sfnChecksum = ComputeSfnChecksum(sfn->name);

            if (!lfnPieces.empty() && currentLfnChecksum == sfnChecksum) {
                // Sort pieces ascending by sequence order
                std::sort(lfnPieces.begin(), lfnPieces.end(), [](const LfnPiece& a, const LfnPiece& b) {
                    return a.seq < b.seq;
                });
                std::u16string fullU16;
                for (const auto& p : lfnPieces) {
                    fullU16 += p.chars;
                }
                entryName = Utf16ToUtf8(fullU16.data(), fullU16.length());
            } else {
                entryName = SfnToString(sfn->name);
            }

            if (EqualsIgnoreCase(entryName, targetName)) {
                result.found = true;
                result.isDirectory = (sfn->attr & FAT32::FAT32_ATTR_DIRECTORY) != 0;
                result.firstCluster = (static_cast<uint32_t>(sfn->fstClusHI) << 16) | sfn->fstClusLO;
                result.fileSize = sfn->fileSize;
                result.dirCluster = c;
                result.entryOffsetInCluster = offset;
                result.lfnCount = currentLfnCount;
                result.fileName = entryName;
                return result;
            }

            // Reset LFN state after processing SFN
            lfnPieces.clear();
            currentLfnCount = 0;
        }
    }
    return result;
}

bool Fat32Driver::SanitizeDirectoryEntry(const DirSearchResult& result) {
    if (!result.found || result.dirCluster < 2) return false;

    uint64_t sector = ClusterToSector(result.dirCluster);
    std::vector<uint8_t> clusterBuffer(m_bytesPerCluster);

    if (!ReadSectors(sector, m_sectorsPerCluster, clusterBuffer.data())) {
        return false;
    }

    // 1. Sanitize preceding LFN entries if they exist within the same cluster
    size_t lfnEntriesToScrub = result.lfnCount;
    size_t curOffset = result.entryOffsetInCluster;

    while (lfnEntriesToScrub > 0 && curOffset >= 32) {
        curOffset -= 32;
        uint8_t* lfnRaw = clusterBuffer.data() + curOffset;
        if (lfnRaw[11] == FAT32::FAT32_ATTR_LONG_NAME) {
            // Mark deleted
            lfnRaw[0] = FAT32::FAT32_DIR_ENTRY_DELETED;
            // Overwrite all remaining 31 bytes with zero
            std::memset(lfnRaw + 1, 0, 31);
        }
        lfnEntriesToScrub--;
    }

    // 2. Sanitize primary SFN entry
    uint8_t* sfnRaw = clusterBuffer.data() + result.entryOffsetInCluster;
    sfnRaw[0] = FAT32::FAT32_DIR_ENTRY_DELETED;
    // Obliterate name characters, timestamps, file size, and cluster references
    std::memset(sfnRaw + 1, 0, 31);

    return WriteSectors(sector, m_sectorsPerCluster, clusterBuffer.data());
}

bool Fat32Driver::ListDirectoryContents(uint32_t dirCluster,
                                       std::vector<std::pair<std::string, uint32_t>>& outEntries,
                                       std::vector<bool>& outIsDir) const {
    outEntries.clear();
    outIsDir.clear();
    if (dirCluster < 2) return false;

    std::vector<uint32_t> dirClusters = GetClusterChain(dirCluster);
    std::vector<uint8_t> clusterBuffer(m_bytesPerCluster);

    struct LfnPiece {
        uint8_t seq;
        std::u16string chars;
    };
    std::vector<LfnPiece> lfnPieces;
    uint8_t currentLfnChecksum = 0;

    for (uint32_t c : dirClusters) {
        uint64_t sector = ClusterToSector(c);
        if (!ReadSectors(sector, m_sectorsPerCluster, clusterBuffer.data())) {
            continue;
        }

        for (size_t offset = 0; offset + 32 <= m_bytesPerCluster; offset += 32) {
            const uint8_t* raw = clusterBuffer.data() + offset;

            if (raw[0] == FAT32::FAT32_DIR_ENTRY_FREE_ALL) {
                return true; // End of entries
            }
            if (raw[0] == FAT32::FAT32_DIR_ENTRY_DELETED) {
                lfnPieces.clear();
                continue;
            }

            uint8_t attr = raw[11];
            if (attr == FAT32::FAT32_ATTR_LONG_NAME) {
                const auto* lfn = reinterpret_cast<const FAT32::Fat32LfnEntry*>(raw);
                uint8_t seq = lfn->order & FAT32::FAT32_LFN_ORDER_MASK;
                if (lfn->order & FAT32::FAT32_LFN_LAST_ENTRY) {
                    lfnPieces.clear();
                    currentLfnChecksum = lfn->checksum;
                }
                std::u16string pieceChars;
                for (int ch = 0; ch < 5; ++ch) pieceChars.push_back(lfn->name1[ch]);
                for (int ch = 0; ch < 6; ++ch) pieceChars.push_back(lfn->name2[ch]);
                for (int ch = 0; ch < 2; ++ch) pieceChars.push_back(lfn->name3[ch]);
                lfnPieces.push_back({ seq, pieceChars });
                continue;
            }

            const auto* sfn = reinterpret_cast<const FAT32::Fat32DirEntry*>(raw);
            if (sfn->attr & FAT32::FAT32_ATTR_VOLUME_ID) {
                lfnPieces.clear();
                continue;
            }

            std::string entryName;
            uint8_t sfnChecksum = ComputeSfnChecksum(sfn->name);
            if (!lfnPieces.empty() && currentLfnChecksum == sfnChecksum) {
                std::sort(lfnPieces.begin(), lfnPieces.end(), [](const LfnPiece& a, const LfnPiece& b) {
                    return a.seq < b.seq;
                });
                std::u16string fullU16;
                for (const auto& p : lfnPieces) fullU16 += p.chars;
                entryName = Utf16ToUtf8(fullU16.data(), fullU16.length());
            } else {
                entryName = SfnToString(sfn->name);
            }

            // Skip current "." and parent ".." references
            if (entryName != "." && entryName != "..") {
                uint32_t cluster = (static_cast<uint32_t>(sfn->fstClusHI) << 16) | sfn->fstClusLO;
                bool isDir = (sfn->attr & FAT32::FAT32_ATTR_DIRECTORY) != 0;
                outEntries.push_back({ entryName, cluster });
                outIsDir.push_back(isDir);
            }

            lfnPieces.clear();
        }
    }
    return true;
}

// =============================================================================
// Public Deletion & Formatting Operations
// =============================================================================

bool Fat32Driver::EraseFile(const std::string& relativePath) {
    if (!m_isMounted) return false;

    std::vector<std::string> tokens = TokenizePath(relativePath);
    if (tokens.empty()) return false;

    uint32_t currentDirCluster = m_rootCluster;
    DirSearchResult searchRes;

    for (size_t i = 0; i < tokens.size(); ++i) {
        searchRes = FindEntryInDirectory(currentDirCluster, tokens[i]);
        if (!searchRes.found) {
            std::cerr << "[Fat32Driver] File not found: " << tokens[i] << "\n";
            return false;
        }

        if (i + 1 < tokens.size()) {
            if (!searchRes.isDirectory) {
                std::cerr << "[Fat32Driver] Path component is not a directory: " << tokens[i] << "\n";
                return false;
            }
            currentDirCluster = searchRes.firstCluster;
        }
    }

    // If target is a directory, delegate to EraseDirectory
    if (searchRes.isDirectory) {
        return EraseDirectory(relativePath);
    }

    // 1. Resolve and wipe physical data clusters with 3-Pass DoD 5220.22-M
    if (searchRes.firstCluster >= 2) {
        std::vector<uint32_t> clusterChain = GetClusterChain(searchRes.firstCluster);
        for (uint32_t c : clusterChain) {
            uint64_t sector = ClusterToSector(c);
            // Pass 1: 0x00, Pass 2: 0xFF, Pass 3: PRNG Gibberish
            m_hardware->SecureEraseSectors(sector, m_sectorsPerCluster);
        }

        // 2. Clear FAT allocation entries
        FreeClusterChain(clusterChain);
    }

    // 3. Obliterate directory metadata (SFN + LFN entries stamped 0xE5 and zeroed)
    return SanitizeDirectoryEntry(searchRes);
}

bool Fat32Driver::EraseDirectoryRecursive(uint32_t dirCluster) {
    if (dirCluster < 2) return false;

    std::vector<std::pair<std::string, uint32_t>> entries;
    std::vector<bool> isDirList;

    if (!ListDirectoryContents(dirCluster, entries, isDirList)) {
        return false;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        uint32_t childCluster = entries[i].second;
        bool isDir = isDirList[i];

        if (isDir) {
            EraseDirectoryRecursive(childCluster);
        } else {
            if (childCluster >= 2) {
                std::vector<uint32_t> chain = GetClusterChain(childCluster);
                for (uint32_t c : chain) {
                    m_hardware->SecureEraseSectors(ClusterToSector(c), m_sectorsPerCluster);
                }
                FreeClusterChain(chain);
            }
        }
    }

    // Wipe directory's own cluster chain
    std::vector<uint32_t> dirChain = GetClusterChain(dirCluster);
    for (uint32_t c : dirChain) {
        m_hardware->SecureEraseSectors(ClusterToSector(c), m_sectorsPerCluster);
    }
    FreeClusterChain(dirChain);

    return true;
}

bool Fat32Driver::EraseDirectory(const std::string& relativePath) {
    if (!m_isMounted) return false;

    std::vector<std::string> tokens = TokenizePath(relativePath);
    if (tokens.empty()) return false;

    uint32_t currentDirCluster = m_rootCluster;
    DirSearchResult searchRes;

    for (size_t i = 0; i < tokens.size(); ++i) {
        searchRes = FindEntryInDirectory(currentDirCluster, tokens[i]);
        if (!searchRes.found) {
            std::cerr << "[Fat32Driver] Directory not found: " << tokens[i] << "\n";
            return false;
        }
        if (i + 1 < tokens.size()) {
            if (!searchRes.isDirectory) return false;
            currentDirCluster = searchRes.firstCluster;
        }
    }

    if (!searchRes.isDirectory) return false;

    // Recursively eradicate contents and directory cluster chain
    if (!EraseDirectoryRecursive(searchRes.firstCluster)) {
        return false;
    }

    // Scrub parent directory entry
    return SanitizeDirectoryEntry(searchRes);
}

bool Fat32Driver::WipeVolume() {
    if (!m_isMounted) return false;

    // 1. Carpet-bomb all user data clusters with 3-Pass DoD sanitization
    // Skip Cluster 2 (Root directory) initially to preserve structure during scan
    for (uint32_t c = 3; c < m_totalClusters + 2; ++c) {
        uint32_t entry = ReadFatEntry(c);
        if (entry != FAT32::FAT32_CLUSTER_FREE && entry != FAT32::FAT32_CLUSTER_BAD) {
            uint64_t sector = ClusterToSector(c);
            m_hardware->SecureEraseSectors(sector, m_sectorsPerCluster);
            WriteFatEntry(c, FAT32::FAT32_CLUSTER_FREE);
        }
    }

    // 2. Clean root directory entries (Cluster 2)
    std::vector<uint32_t> rootChain = GetClusterChain(m_rootCluster);
    std::vector<uint8_t> cleanCluster(m_bytesPerCluster, 0);

    for (size_t i = 0; i < rootChain.size(); ++i) {
        uint32_t c = rootChain[i];
        uint64_t sector = ClusterToSector(c);
        if (i == 0) {
            // Keep first cluster of root dir, zero out entries
            WriteSectors(sector, m_sectorsPerCluster, cleanCluster.data());
        } else {
            // Free and wipe extra clusters allocated to root dir
            m_hardware->SecureEraseSectors(sector, m_sectorsPerCluster);
            WriteFatEntry(c, FAT32::FAT32_CLUSTER_FREE);
        }
    }

    // Set Root Dir FAT entry to EOC
    WriteFatEntry(m_rootCluster, FAT32::FAT32_CLUSTER_EOC_MAX);

    // 3. Reset FSInfo
    UpdateFSInfo(m_totalClusters - 1, m_rootCluster + 1);

    return true;
}

bool Fat32Driver::FormatDrive(bool fullDriveSanitize) {
    if (!m_hardware) return false;

    Core::DeviceGeometry geo = m_hardware->GetGeometry();
    if (geo.bytesPerSector == 0 || geo.totalSectors == 0) return false;

    uint32_t bps = geo.bytesPerSector;
    uint64_t totalSec = geo.totalSectors;

    if (fullDriveSanitize) {
        m_hardware->SecureEraseSectors(0, totalSec);
    }

    // 1. Calculate FAT32 geometry parameters
    uint8_t spc = 8; // 8 sectors per cluster (4KB clusters for 512B sectors)
    if (totalSec < 65536) spc = 1;
    else if (totalSec < 1048576) spc = 8;
    else if (totalSec < 67108864) spc = 64; // 32KB clusters

    uint16_t rsvdSec = 32; // 32 reserved sectors
    uint8_t numFATs = 2;

    // Estimate sectors per FAT: totalSec / (spc * (bps / 4))
    uint64_t entriesPerFatSector = bps / 4;
    uint32_t fatSize = static_cast<uint32_t>((totalSec / spc + entriesPerFatSector - 1) / entriesPerFatSector);
    if (fatSize == 0) fatSize = 32;

    // 2. Build and write Fat32BootSector (Sector 0 and Backup at Sector 6)
    FAT32::Fat32BootSector bpb;
    std::memset(&bpb, 0, sizeof(bpb));

    bpb.jmpBoot[0] = 0xEB;
    bpb.jmpBoot[1] = 0x58;
    bpb.jmpBoot[2] = 0x90;
    std::memcpy(bpb.oemName, "MSWIN4.1", 8);
    bpb.bytesPerSector       = static_cast<uint16_t>(bps);
    bpb.sectorsPerCluster    = spc;
    bpb.reservedSectorCount  = rsvdSec;
    bpb.numFATs              = numFATs;
    bpb.media                = 0xF8;
    bpb.hiddenSectors        = 0;
    bpb.totalSectors32       = static_cast<uint32_t>(totalSec);
    bpb.fatSize32            = fatSize;
    bpb.extFlags             = 0x0000;
    bpb.fsVersion            = 0x0000;
    bpb.rootCluster          = 2;
    bpb.fsInfoSector         = 1;
    bpb.backupBootSector     = 6;
    bpb.driveNumber          = 0x80;
    bpb.bootSignature        = 0x29;
    bpb.volumeID             = 0x12345678;
    std::memcpy(bpb.volumeLabel, "NO NAME    ", 11);
    std::memcpy(bpb.fileSystemType, "FAT32   ", 8);
    bpb.signature            = FAT32::FAT32_BOOT_SIGNATURE;

    std::vector<uint8_t> secBuffer(bps, 0);
    std::memcpy(secBuffer.data(), &bpb, sizeof(bpb));

    WriteSectors(0, 1, secBuffer.data());
    WriteSectors(bpb.backupBootSector, 1, secBuffer.data());

    // 3. Build and write Fat32FSInfo (Sector 1 and Backup at Sector 7)
    FAT32::Fat32FSInfo fsi;
    std::memset(&fsi, 0, sizeof(fsi));

    fsi.leadSig   = FAT32::FAT32_FSINFO_LEAD_SIG;
    fsi.strucSig  = FAT32::FAT32_FSINFO_STRUC_SIG;
    uint32_t dataSec = static_cast<uint32_t>(totalSec) - (rsvdSec + (numFATs * fatSize));
    fsi.freeCount = (dataSec / spc) - 1;
    fsi.nextFree  = 3;
    fsi.trailSig  = FAT32::FAT32_FSINFO_TRAIL_SIG;

    std::memset(secBuffer.data(), 0, bps);
    std::memcpy(secBuffer.data(), &fsi, sizeof(fsi));

    WriteSectors(1, 1, secBuffer.data());
    WriteSectors(bpb.backupBootSector + 1, 1, secBuffer.data());

    // 4. Initialize FAT1 and FAT2
    // Entry 0: 0x0FFFFF00 | media (0x0FFFF8)
    // Entry 1: 0x0FFFFFFF (EOC)
    // Entry 2: 0x0FFFFFFF (Root dir EOC)
    for (uint8_t f = 0; f < numFATs; ++f) {
        uint64_t fatStart = rsvdSec + (static_cast<uint64_t>(f) * fatSize);
        std::vector<uint8_t> fatSec(bps, 0);

        uint32_t* entries = reinterpret_cast<uint32_t*>(fatSec.data());
        entries[0] = 0x0FFFFF00 | bpb.media;
        entries[1] = FAT32::FAT32_CLUSTER_EOC_MAX;
        entries[2] = FAT32::FAT32_CLUSTER_EOC_MAX;

        WriteSectors(fatStart, 1, fatSec.data());

        // Zero remaining FAT sectors
        std::memset(fatSec.data(), 0, bps);
        for (uint32_t s = 1; s < fatSize; ++s) {
            WriteSectors(fatStart + s, 1, fatSec.data());
        }
    }

    // 5. Initialize Root Directory cluster (Cluster 2) with zeros
    uint64_t rootSector = (rsvdSec + (numFATs * fatSize));
    std::vector<uint8_t> clusterBuf(bps * spc, 0);
    WriteSectors(rootSector, spc, clusterBuf.data());

    return Mount();
}

void Fat32Driver::PrintBootInfo() const {
    std::cout << "\n================ FAT32 VOLUME BOOT PARAMETERS ================\n"
              << "OEM Name:                 " << std::string(m_bpb.oemName, 8) << "\n"
              << "Bytes per Sector:         " << m_bytesPerSector << "\n"
              << "Sectors per Cluster:      " << m_sectorsPerCluster << " (" << m_bytesPerCluster << " bytes)\n"
              << "Reserved Sectors:         " << m_bpb.reservedSectorCount << "\n"
              << "Number of FATs:           " << (int)m_numFATs << "\n"
              << "FAT Size (Sectors):       " << m_fatSizeSectors << "\n"
              << "First Data Sector (LBA):  " << m_firstDataSector << "\n"
              << "Total Clusters:           " << m_totalClusters << "\n"
              << "Root Directory Cluster:   " << m_rootCluster << "\n"
              << "FSInfo Sector:            " << m_fsInfoSector << "\n"
              << "FSInfo Free Count:        " << m_fsInfo.freeCount << "\n"
              << "==============================================================\n";
}

} // namespace FileSystems
} // namespace Erasure
