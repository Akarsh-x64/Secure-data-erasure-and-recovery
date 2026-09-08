#include "exFAT.h"
#include <iostream>
#include <cstring>
#include <sstream>

namespace Erasure {
namespace FileSystems {

ExFatDriver::ExFatDriver(Core::IHardwareController* hardware) 
    : m_hardware(hardware), m_bytesPerSector(0), m_sectorsPerCluster(0) {
    std::memset(&m_vbr, 0, sizeof(m_vbr));
}

uint64_t ExFatDriver::ClusterToSector(uint32_t cluster) const {
    if (cluster < 2) return 0; // Clusters 0 and 1 are reserved
    // Calculate the physical sector offset based on the cluster heap start
    return m_vbr.clusterHeapOffsetSectors + ((cluster - 2) * m_sectorsPerCluster);
}

bool ExFatDriver::Mount() {
    if (!m_hardware) return false;

    Core::DeviceGeometry geo = m_hardware->GetGeometry();
    if (geo.bytesPerSector == 0) return false;

    // Read Sector 0 (The Volume Boot Record)
    std::vector<uint8_t> sectorBuffer(geo.bytesPerSector);
    if (!m_hardware->ReadSectors(0, 1, sectorBuffer.data())) {
        return false;
    }

    // Cast the raw bytes into our strictly packed struct
    std::memcpy(&m_vbr, sectorBuffer.data(), sizeof(ExFatBootSector));

    // Verify it's actually exFAT
    if (std::strncmp(m_vbr.fileSystemName, "EXFAT   ", 8) != 0) {
        return false; // Not an exFAT drive!
    }

    // Validate shift parameters per Microsoft exFAT spec §3.1.5
    if (m_vbr.bytesPerSectorShift < 9 || m_vbr.bytesPerSectorShift > 12 ||
        m_vbr.sectorsPerClusterShift > 25) {
        return false; // Sector size must be 512B..4096B, cluster size max 32MB
    }

    // Decode the shift values into actual sizes
    m_bytesPerSector = 1 << m_vbr.bytesPerSectorShift;
    m_sectorsPerCluster = 1 << m_vbr.sectorsPerClusterShift;

    // Double check that the geometry matches the VBR
    if (m_bytesPerSector != geo.bytesPerSector) {
        return false; 
    }

    // --- NEW: Scan Root Directory for the Allocation Bitmap ---
    m_bitmapFirstCluster = 0;
    m_bitmapDataLength = 0;
    
    uint64_t rootDirSector = ClusterToSector(m_vbr.rootDirectoryFirstCluster);
    std::vector<uint8_t> rootDirBuffer(m_bytesPerSector * m_sectorsPerCluster);
    
    // We'll just read the first cluster of the root dir for simplicity in this implementation
    // (In reality, root dir could span multiple clusters if it's huge, but usually the bitmap entry is first)
    if (m_hardware->ReadSectors(rootDirSector, m_sectorsPerCluster, rootDirBuffer.data())) {
        for (size_t i = 0; i < rootDirBuffer.size(); i += 32) {
            ExFatDirectoryEntry* entry = reinterpret_cast<ExFatDirectoryEntry*>(&rootDirBuffer[i]);
            if (entry->entryType == 0x81) { // Allocation Bitmap!
                ExFatBitmapDirectoryEntry* bitmapEntry = reinterpret_cast<ExFatBitmapDirectoryEntry*>(entry);
                m_bitmapFirstCluster = bitmapEntry->firstCluster;
                m_bitmapDataLength = bitmapEntry->dataLength;
                break;
            }
        }
    }

    // Verify Sector 11 Main Boot Checksum if sectors 0..11 are accessible
    std::vector<uint8_t> bootRegion(12 * m_bytesPerSector);
    if (m_hardware->ReadSectors(0, 12, bootRegion.data())) {
        if (VerifyBootChecksum(bootRegion.data(), m_bytesPerSector)) {
            std::cout << "[ExFatDriver] Sector 11 Main Boot Checksum verified successfully.\n";
        }
    }

    return true; // Successfully mounted and mapped the drive!
}

uint32_t ExFatDriver::ReadFatEntry(uint32_t cluster) const {
    uint64_t fatSector = m_vbr.fatOffsetSectors + ((cluster * 4) / m_bytesPerSector);
    uint32_t fatOffset = (cluster * 4) % m_bytesPerSector;
    
    std::vector<uint8_t> sector(m_bytesPerSector);
    if (!m_hardware->ReadSectors(fatSector, 1, sector.data())) return 0;
    
    uint32_t nextCluster;
    std::memcpy(&nextCluster, &sector[fatOffset], sizeof(uint32_t));
    return nextCluster;
}

bool ExFatDriver::WriteFatEntry(uint32_t cluster, uint32_t value) {
    uint64_t fatSector = m_vbr.fatOffsetSectors + ((cluster * 4) / m_bytesPerSector);
    uint32_t fatOffset = (cluster * 4) % m_bytesPerSector;
    
    std::vector<uint8_t> sector(m_bytesPerSector);
    if (!m_hardware->ReadSectors(fatSector, 1, sector.data())) return false;
    
    std::memcpy(&sector[fatOffset], &value, sizeof(uint32_t));
    return m_hardware->WriteSectors(fatSector, 1, sector.data());
}

bool ExFatDriver::ClearBitmapBit(uint32_t cluster) {
    if (cluster < 2 || m_bitmapFirstCluster == 0 || m_bytesPerSector == 0) return false;
    
    uint64_t bitmapSector = ClusterToSector(m_bitmapFirstCluster) + ((cluster - 2) / (m_bytesPerSector * 8));
    uint32_t bitOffset = (cluster - 2) % (m_bytesPerSector * 8);
    uint32_t byteOffset = bitOffset / 8;
    uint8_t bitMask = ~(1 << (bitOffset % 8));
    
    std::vector<uint8_t> sector(m_bytesPerSector);
    if (!m_hardware->ReadSectors(bitmapSector, 1, sector.data())) return false;
    
    sector[byteOffset] &= bitMask;
    std::cout << "[Bitmap] Cleared bit for Cluster " << cluster << " (Sector " << bitmapSector << ", Byte " << byteOffset << ")\n";
    return m_hardware->WriteSectors(bitmapSector, 1, sector.data());
}

std::vector<std::wstring> ExFatDriver::TokenizePath(const std::wstring& path) const {
    std::vector<std::wstring> tokens;
    std::wstring token;
    
    std::wstring normalizedPath = path;
    for (auto& c : normalizedPath) {
        if (c == L'/') c = L'\\';
    }

    std::wstringstream wss(normalizedPath);
    while (std::getline(wss, token, L'\\')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::vector<uint32_t> ExFatDriver::GetClusterChain(uint32_t startCluster, uint64_t dataLength, bool noFatChain) const {
    std::vector<uint32_t> clusters;
    if (startCluster < 2 || m_bytesPerSector == 0 || m_sectorsPerCluster == 0) return clusters;

    uint32_t currentCluster = startCluster;
    uint32_t clusterBytes = m_bytesPerSector * m_sectorsPerCluster;

    if (dataLength == 0) {
        // Unknown length (e.g. Root Directory). Must follow FAT chain until EOF.
        while (currentCluster >= 2 && currentCluster < 0xFFFFFFF8) {
            clusters.push_back(currentCluster);
            currentCluster = ReadFatEntry(currentCluster);
        }
    } else {
        uint32_t clusterCount = static_cast<uint32_t>((dataLength + clusterBytes - 1) / clusterBytes);
        for (uint32_t i = 0; i < clusterCount; ++i) {
            clusters.push_back(currentCluster);
            if (!noFatChain) {
                uint32_t nextCluster = ReadFatEntry(currentCluster);
                if (nextCluster >= 0xFFFFFFF8) break;
                currentCluster = nextCluster;
            } else {
                currentCluster++;
            }
        }
    }
    return clusters;
}

ExFatDriver::SearchResult ExFatDriver::FindEntryInDirectory(const std::vector<uint32_t>& dirClusters, const std::wstring& targetName, std::vector<uint8_t>& outDirBuffer) const {
    SearchResult result = { false, false, 0, 0, false, 0 };
    if (dirClusters.empty()) return result;

    size_t clusterSizeBytes = m_bytesPerSector * m_sectorsPerCluster;
    outDirBuffer.resize(dirClusters.size() * clusterSizeBytes);

    for (size_t i = 0; i < dirClusters.size(); ++i) {
        uint64_t sector = ClusterToSector(dirClusters[i]);
        if (!m_hardware->ReadSectors(sector, m_sectorsPerCluster, outDirBuffer.data() + (i * clusterSizeBytes))) {
            return result;
        }
    }

    std::wstring currentFileName = L"";
    size_t tempEntryIndex = 0;
    bool isDir = false;
    uint32_t targetFirstCluster = 0;
    uint64_t targetDataLength = 0;
    bool targetNoFatChain = false;

    for (size_t i = 0; i < outDirBuffer.size(); i += 32) {
        ExFatDirectoryEntry* genericEntry = reinterpret_cast<ExFatDirectoryEntry*>(&outDirBuffer[i]);
        
        if (genericEntry->entryType == 0x85) { 
            currentFileName = L"";
            tempEntryIndex = i;
            ExFatFileDirectoryEntry* fileEntry = reinterpret_cast<ExFatFileDirectoryEntry*>(genericEntry);
            isDir = (fileEntry->fileAttributes & 0x10) != 0; 
        } 
        else if (genericEntry->entryType == 0xC0) { 
            ExFatStreamExtensionDirectoryEntry* stream = reinterpret_cast<ExFatStreamExtensionDirectoryEntry*>(genericEntry);
            targetFirstCluster = stream->firstCluster;
            targetDataLength = stream->dataLength;
            targetNoFatChain = (stream->generalSecondaryFlags & 0x02) != 0;
        }
        else if (genericEntry->entryType == 0xC1) { 
            ExFatFileNameDirectoryEntry* nameEntry = reinterpret_cast<ExFatFileNameDirectoryEntry*>(genericEntry);
            for (int c = 0; c < 15; c++) {
                if (nameEntry->fileName[c] != 0x0000) currentFileName += nameEntry->fileName[c];
            }
            
            if (currentFileName == targetName) {
                result.found = true;
                result.isDirectory = isDir;
                result.firstCluster = targetFirstCluster;
                result.dataLength = targetDataLength;
                result.noFatChain = targetNoFatChain;
                result.entryIndex = tempEntryIndex;
                break;
            }
        }
        else if (genericEntry->entryType == 0x00) {
            break; 
        }
    }
    return result;
}

bool ExFatDriver::EraseFile(const std::string& relativePath) {
    if (m_bytesPerSector == 0) return false;

    std::wstring wRelativePath(relativePath.begin(), relativePath.end());
    std::vector<std::wstring> pathTokens = TokenizePath(wRelativePath);
    
    if (pathTokens.empty()) {
        std::cout << "[ERROR] Invalid path provided.\n";
        return false;
    }

    std::cout << "\n--- Initiating Recursive EraseFile for: " << relativePath << " ---\n";

    std::vector<uint32_t> currentDirClusters = GetClusterChain(m_vbr.rootDirectoryFirstCluster, 0, false);
    std::vector<uint8_t> currentDirBuffer;
    SearchResult searchRes;
    
    for (size_t i = 0; i < pathTokens.size(); ++i) {
        const std::wstring& targetName = pathTokens[i];
        bool isLastToken = (i == pathTokens.size() - 1);
        
        std::cout << "[Parser] Searching for '" << std::string(targetName.begin(), targetName.end()) << "'...\n";
        
        searchRes = FindEntryInDirectory(currentDirClusters, targetName, currentDirBuffer);
        
        if (!searchRes.found) {
            std::cout << "[Parser] ERROR: '" << std::string(targetName.begin(), targetName.end()) << "' not found!\n";
            return false;
        }

        if (!isLastToken) {
            if (!searchRes.isDirectory) {
                std::cout << "[Parser] ERROR: '" << std::string(targetName.begin(), targetName.end()) << "' is a file, not a folder!\n";
                return false;
            }
            currentDirClusters = GetClusterChain(searchRes.firstCluster, searchRes.dataLength, searchRes.noFatChain);
            std::cout << "  -> Entered directory. New Start Cluster: " << searchRes.firstCluster << "\n";
        }
    }

    if (searchRes.firstCluster >= 2 && searchRes.dataLength > 0) {
        std::vector<uint32_t> targetClusters = GetClusterChain(searchRes.firstCluster, searchRes.dataLength, searchRes.noFatChain);
        
        std::cout << "[Erasure] Target occupies " << targetClusters.size() << " clusters. Beginning Data wipe...\n";

        for (uint32_t cluster : targetClusters) {
            uint64_t sector = ClusterToSector(cluster);
            std::cout << "  -> Wiping Data Cluster " << cluster << " (Sector " << sector << ")\n";
            
            m_hardware->SecureEraseSectors(sector, m_sectorsPerCluster);
            ClearBitmapBit(cluster);

            if (!searchRes.noFatChain) {
                std::cout << "  -> Freeing FAT Entry for Cluster " << cluster << "\n";
                WriteFatEntry(cluster, 0x00000000); 
            }
        }
    }

    // Mark directory metadata entry set as deleted per Microsoft exFAT spec §6.2.1.1
    // Clearing bit 7 of EntryType (InUse = 0) marks each entry as deleted (e.g. 0x85 -> 0x05, 0xC0 -> 0x40, 0xC1 -> 0x41)
    // while preserving directory traversal so subsequent entries in the directory are not truncated.
    size_t wipeLength = 32; 
    for (size_t offset = searchRes.entryIndex + 32; offset < currentDirBuffer.size(); offset += 32) {
        uint8_t type = currentDirBuffer[offset];
        if (type == 0xC0 || type == 0xC1) {
            wipeLength += 32;
        } else {
            break;
        }
    }

    std::cout << "[Erasure] Marking Directory Metadata Deleted (" << wipeLength << " bytes)...\n";
    for (size_t offset = searchRes.entryIndex; offset < searchRes.entryIndex + wipeLength; offset += 32) {
        currentDirBuffer[offset] &= 0x7F; // Clear InUse bit (0x85 -> 0x05, 0xC0 -> 0x40, 0xC1 -> 0x41)
        std::memset(&currentDirBuffer[offset + 1], 0, 31); // Zero out all filenames, timestamps, cluster pointers, and sizes
    }
    
    // Write the dirty directory buffer back to disk
    for (size_t i = 0; i < currentDirClusters.size(); ++i) {
        uint64_t sector = ClusterToSector(currentDirClusters[i]);
        size_t offset = i * (m_bytesPerSector * m_sectorsPerCluster);
        m_hardware->WriteSectors(sector, m_sectorsPerCluster, &currentDirBuffer[offset]);
    }
    
    std::cout << "--- Recursive EraseFile Securely Completed! ---\n";
    return true;
}

bool ExFatDriver::WipeVolume() {
    if (m_bytesPerSector == 0) return false;
    
    std::cout << "\n=== INITIATING SURGICAL VOLUME WIPE ===\n";
    std::cout << "[Quarantine] Mapping critical filesystem structures...\n";

    std::vector<uint32_t> quarantinedClusters;
    
    // 1. Root Directory
    std::vector<uint32_t> rootDirClusters = GetClusterChain(m_vbr.rootDirectoryFirstCluster, 0, false);
    quarantinedClusters.insert(quarantinedClusters.end(), rootDirClusters.begin(), rootDirClusters.end());

    size_t clusterSizeBytes = m_bytesPerSector * m_sectorsPerCluster;
    std::vector<uint8_t> rootDirBuffer(rootDirClusters.size() * clusterSizeBytes);

    for (size_t i = 0; i < rootDirClusters.size(); ++i) {
        uint64_t sector = ClusterToSector(rootDirClusters[i]);
        m_hardware->ReadSectors(sector, m_sectorsPerCluster, rootDirBuffer.data() + (i * clusterSizeBytes));
    }

    uint32_t upcaseFirstCluster = 0;
    uint64_t upcaseDataLength = 0;

    for (size_t i = 0; i < rootDirBuffer.size(); i += 32) {
        uint8_t entryType = rootDirBuffer[i];
        if (entryType == 0x82) { // Upcase Table
            std::memcpy(&upcaseFirstCluster, &rootDirBuffer[i + 20], sizeof(uint32_t));
            std::memcpy(&upcaseDataLength, &rootDirBuffer[i + 24], sizeof(uint64_t));
        }
    }

    // 2. Allocation Bitmap
    std::vector<uint32_t> bmap = GetClusterChain(m_bitmapFirstCluster, m_bitmapDataLength, false);
    quarantinedClusters.insert(quarantinedClusters.end(), bmap.begin(), bmap.end());

    // 3. Upcase Table
    std::vector<uint32_t> upcase;
    if (upcaseFirstCluster >= 2) {
        upcase = GetClusterChain(upcaseFirstCluster, upcaseDataLength, false);
        quarantinedClusters.insert(quarantinedClusters.end(), upcase.begin(), upcase.end());
    }

    auto isQuarantined = [&](uint32_t c) {
        for (uint32_t q : quarantinedClusters) {
            if (q == c) return true;
        }
        return false;
    };

    std::cout << "[Erasure] Carpet Bombing " << m_vbr.clusterCount << " data clusters...\n";
    uint32_t wipeCount = 0;
    
    // Secure Erase all non-quarantined clusters
    for (uint32_t cluster = 2; cluster <= m_vbr.clusterCount + 1; ++cluster) {
        if (isQuarantined(cluster)) continue;

        uint64_t sector = ClusterToSector(cluster);
        m_hardware->SecureEraseSectors(sector, m_sectorsPerCluster);
        wipeCount++;

        if (wipeCount % 1000 == 0) {
            std::cout << "  -> Wiped " << wipeCount << " clusters...\r";
            std::cout.flush();
        }
    }
    std::cout << "\n[Erasure] Successfully wiped " << wipeCount << " user data clusters!\n";

    std::cout << "[System] Rebuilding FAT and Allocation Bitmap...\n";
    
    // Zero entire FAT
    std::vector<uint8_t> zeroFat(m_bytesPerSector, 0);
    for (uint32_t i = 0; i < m_vbr.fatLengthSectors; ++i) {
        m_hardware->WriteSectors(m_vbr.fatOffsetSectors + i, 1, zeroFat.data());
    }

    // Re-link FAT
    WriteFatEntry(0, 0xFFFFFFF8);
    WriteFatEntry(1, 0xFFFFFFFF);

    auto rebuildFat = [&](const std::vector<uint32_t>& chain) {
        if (chain.empty()) return;
        for (size_t i = 0; i < chain.size() - 1; ++i) {
            WriteFatEntry(chain[i], chain[i+1]);
        }
        WriteFatEntry(chain.back(), 0xFFFFFFFF);
    };

    rebuildFat(rootDirClusters);
    rebuildFat(bmap);
    rebuildFat(upcase);

    // Rebuild Bitmap entirely in RAM, then flush
    std::vector<uint8_t> bitmapData(bmap.size() * clusterSizeBytes, 0); 
    auto fastSetBit = [&](uint32_t cluster) {
        if (cluster < 2) return;
        uint32_t bitOffset = cluster - 2;
        bitmapData[bitOffset / 8] |= (1 << (bitOffset % 8));
    };

    for (uint32_t c : quarantinedClusters) {
        fastSetBit(c);
    }

    for (size_t i = 0; i < bmap.size(); ++i) {
        m_hardware->WriteSectors(ClusterToSector(bmap[i]), m_sectorsPerCluster, bitmapData.data() + (i * clusterSizeBytes));
    }

    std::cout << "[System] Scrubbing Root Directory Metadata...\n";
    for (size_t i = 0; i < rootDirBuffer.size(); i += 32) {
        uint8_t type = rootDirBuffer[i];
        if (type != 0x81 && type != 0x82 && type != 0x83) { // Preserve Bitmap, Upcase, Vol Label
            std::memset(&rootDirBuffer[i], 0, 32);
        }
    }

    for (size_t i = 0; i < rootDirClusters.size(); ++i) {
        m_hardware->WriteSectors(ClusterToSector(rootDirClusters[i]), m_sectorsPerCluster, rootDirBuffer.data() + (i * clusterSizeBytes));
    }

    std::cout << "=== SURGICAL WIPE SECURELY COMPLETED! ===\n";
    return true;
}

void ExFatDriver::PrintVBRInfo() const {
    if (m_bytesPerSector == 0) {
        std::cout << "Drive is not mounted or not an exFAT drive!\n";
        return;
    }

    std::cout << "\n=== exFAT Volume Boot Record (VBR) ===\n";
    std::cout << "File System Name: " << std::string(m_vbr.fileSystemName, 8) << "\n";
    std::cout << "Volume Length (Sectors): " << m_vbr.volumeLengthSectors << "\n";
    std::cout << "FAT Offset (Sectors): " << m_vbr.fatOffsetSectors << "\n";
    std::cout << "FAT Length (Sectors): " << m_vbr.fatLengthSectors << "\n";
    std::cout << "Cluster Heap Offset: " << m_vbr.clusterHeapOffsetSectors << "\n";
    std::cout << "Cluster Count: " << m_vbr.clusterCount << "\n";
    std::cout << "Root Dir First Cluster: " << m_vbr.rootDirectoryFirstCluster << "\n";
    std::cout << "Bytes Per Sector: " << m_bytesPerSector << "\n";
    std::cout << "Sectors Per Cluster: " << m_sectorsPerCluster << "\n";
    std::cout << "Total Bytes in a Cluster: " << (m_bytesPerSector * m_sectorsPerCluster) << "\n";
    std::cout << "======================================\n";
}

// =============================================================================
// Checksum & Hash Algorithms (Microsoft exFAT Specification Compliant)
// =============================================================================

uint16_t ExFatDriver::ComputeNameHash(const std::u16string& name) {
    uint16_t hash = 0;
    for (char16_t ch : name) {
        char16_t up = (ch >= u'a' && ch <= u'z') ? static_cast<char16_t>(ch - 32) : ch;
        uint8_t low = static_cast<uint8_t>(up & 0xFF);
        uint8_t high = static_cast<uint8_t>((up >> 8) & 0xFF);
        hash = static_cast<uint16_t>(((hash << 15) | (hash >> 1)) + low);
        hash = static_cast<uint16_t>(((hash << 15) | (hash >> 1)) + high);
    }
    return hash;
}

uint16_t ExFatDriver::ComputeEntrySetChecksum(const uint8_t* entrySet, size_t entryCount) {
    uint16_t checksum = 0;
    size_t byteCount = entryCount * 32;
    for (size_t i = 0; i < byteCount; ++i) {
        // Skip SetChecksum field at byte offset 2 and 3 of the primary entry
        if (i == 2 || i == 3) continue;
        checksum = static_cast<uint16_t>(((checksum << 15) | (checksum >> 1)) + entrySet[i]);
    }
    return checksum;
}

uint32_t ExFatDriver::ComputeBootChecksum(const uint8_t* bootSectors, size_t byteCount) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < byteCount; ++i) {
        // VolumeFlags (offset 106, 107) and PercentInUse (offset 112) of Sector 0 are skipped per spec §3.1.9
        if (i == 106 || i == 107 || i == 112) continue;
        checksum = ((checksum << 31) | (checksum >> 1)) + bootSectors[i];
    }
    return checksum;
}

bool ExFatDriver::VerifyBootChecksum(const uint8_t* bootRegion, size_t sectorSize) {
    if (!bootRegion || sectorSize < 512) return false;
    uint32_t calculated = ComputeBootChecksum(bootRegion, 11 * sectorSize);
    const uint32_t* sector11 = reinterpret_cast<const uint32_t*>(bootRegion + (11 * sectorSize));
    for (size_t i = 0; i < sectorSize / sizeof(uint32_t); ++i) {
        if (sector11[i] != calculated) return false;
    }
    return true;
}

bool ExFatDriver::VerifyEntrySetChecksum(const uint8_t* entrySet, size_t entryCount) {
    if (!entrySet || entryCount < 2) return false;
    const auto* primary = reinterpret_cast<const ExFatFileDirectoryEntry*>(entrySet);
    uint16_t expected = primary->setChecksum;
    uint16_t calculated = ComputeEntrySetChecksum(entrySet, entryCount);
    return expected == calculated;
}

} // namespace FileSystems
} // namespace Erasure

