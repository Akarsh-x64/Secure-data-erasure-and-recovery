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

    return true; // Successfully mounted and mapped the drive!
}

bool ExFatDriver::DeleteFile(const std::string& relativePath) {
    if (m_bytesPerSector == 0) return false; // Not mounted yet

    // [STUB] 
    // Step 1: Traverse the Root Directory starting at m_vbr.rootDirectoryFirstCluster
    // Step 2: Find the directory entries matching "relativePath"
    // Step 3: Get the starting cluster of the file from the Stream Extension entry
    // Step 4: Traverse the FAT table to get all clusters for the file
    // Step 5: Convert clusters to sectors using ClusterToSector()
    // Step 6: Call m_hardware->SecureEraseSectors() for all data sectors
    // Step 7: Call m_hardware->WriteSectors() to zero out the Directory Entries
    // Step 8: Update the Allocation Bitmap to free the clusters

    return false; // Not fully implemented yet
}

bool ExFatDriver::WipeVolume() {
    // [STUB] 
    // Systematically walk every folder and delete every file logically.
    return false;
}

} // namespace FileSystems
} // namespace Erasure
