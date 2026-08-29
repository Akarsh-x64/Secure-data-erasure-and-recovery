#include "exFAT.h"
#include <iostream>
#include <cstring>

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
    if (cluster < 2 || m_bitmapFirstCluster == 0) return false;
    
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

bool ExFatDriver::DeleteFile(const std::string& relativePath) {
    if (m_bytesPerSector == 0) return false;

    std::wstring targetName(relativePath.begin(), relativePath.end());
    std::cout << "\n--- Initiating DeleteFile for: " << relativePath << " ---\n";

    uint64_t rootDirSector = ClusterToSector(m_vbr.rootDirectoryFirstCluster);
    std::vector<uint8_t> rootDirBuffer(m_bytesPerSector * m_sectorsPerCluster);
    
    if (!m_hardware->ReadSectors(rootDirSector, m_sectorsPerCluster, rootDirBuffer.data())) {
        return false;
    }

    std::wstring currentFileName = L"";
    uint32_t targetFirstCluster = 0;
    uint64_t targetDataLength = 0;
    bool targetNoFatChain = false;
    size_t fileEntryIndex = 0;
    bool found = false;

    for (size_t i = 0; i < rootDirBuffer.size(); i += 32) {
        ExFatDirectoryEntry* genericEntry = reinterpret_cast<ExFatDirectoryEntry*>(&rootDirBuffer[i]);
        
        if (genericEntry->entryType == 0x85) { 
            currentFileName = L"";
            fileEntryIndex = i; // Mark where the metadata starts
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
                found = true;
                std::cout << "[Parser] Found File! Start Cluster: " << targetFirstCluster 
                          << ", Length: " << targetDataLength << " bytes"
                          << ", NoFATChain: " << (targetNoFatChain ? "True" : "False") << "\n";
                
                // Erase the metadata from our RAM buffer immediately
                std::memset(&rootDirBuffer[fileEntryIndex], 0, (i + 32) - fileEntryIndex);
                break;
            }
        }
        else if (genericEntry->entryType == 0x00) {
            break; // End of directory
        }
    }

    if (!found) {
        std::cout << "[Parser] File not found in root directory.\n";
        return false;
    }

    if (targetFirstCluster >= 2 && targetDataLength > 0) {
        uint32_t currentCluster = targetFirstCluster;
        uint32_t clusterCount = (targetDataLength + (m_bytesPerSector * m_sectorsPerCluster) - 1) / (m_bytesPerSector * m_sectorsPerCluster);
        
        std::cout << "[Erasure] File occupies " << clusterCount << " clusters. Beginning Data wipe...\n";

        for (uint32_t i = 0; i < clusterCount; ++i) {
            uint64_t sector = ClusterToSector(currentCluster);
            std::cout << "  -> Wiping Data Cluster " << currentCluster << " (Sector " << sector << ")\n";
            
            // 1. Wipe Physical Data
            m_hardware->SecureEraseSectors(sector, m_sectorsPerCluster);
            
            // 2. Free Bitmap
            ClearBitmapBit(currentCluster);

            // 3. Clear FAT Chain
            if (!targetNoFatChain) {
                uint32_t nextCluster = ReadFatEntry(currentCluster);
                std::cout << "  -> Freeing FAT Entry for Cluster " << currentCluster << "\n";
                WriteFatEntry(currentCluster, 0x00000000); 
                
                if (nextCluster >= 0xFFFFFFF8) break; 
                currentCluster = nextCluster;
            } else {
                currentCluster++; 
            }
        }
    }

    // 4. Wipe Metadata on physical disk
    std::cout << "[Erasure] Committing wiped Directory Metadata to disk...\n";
    m_hardware->WriteSectors(rootDirSector, m_sectorsPerCluster, rootDirBuffer.data());
    
    std::cout << "--- DeleteFile Securely Completed! ---\n";
    return true;
}

bool ExFatDriver::WipeVolume() {
    // [STUB] 
    // Systematically walk every folder and delete every file logically.
    return false;
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

} // namespace FileSystems
} // namespace Erasure
