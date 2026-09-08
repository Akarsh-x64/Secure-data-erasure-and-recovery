#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>

#include "../Erasure/Core/IStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/NTFS/NTFS.h"
#include "../Erasure/File Systems/NTFS/NTFS_Structures.h"

using namespace Erasure;
using namespace Erasure::FileSystems;
using namespace Erasure::FileSystems::NTFS;

// ============================================================================
// In-Memory Simulated Storage Device (OS Layer)
// ============================================================================
class MemoryDiskDevice : public Core::IStorageDevice {
private:
    std::vector<uint8_t> m_disk;
    uint32_t m_sectorSize;

public:
    explicit MemoryDiskDevice(size_t totalBytes, uint32_t sectorSize = 512)
        : m_disk(totalBytes, 0)
        , m_sectorSize(sectorSize)
    {}

    bool Open(const std::string&) override { return true; }
    void Close() override {}

    bool ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) override {
        size_t startByte = startSector * m_sectorSize;
        size_t byteCount = static_cast<size_t>(sectorCount) * m_sectorSize;
        if (startByte + byteCount > m_disk.size()) return false;
        std::memcpy(buffer, m_disk.data() + startByte, byteCount);
        return true;
    }

    bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) override {
        size_t startByte = startSector * m_sectorSize;
        size_t byteCount = static_cast<size_t>(sectorCount) * m_sectorSize;
        if (startByte + byteCount > m_disk.size()) return false;
        std::memcpy(m_disk.data() + startByte, buffer, byteCount);
        return true;
    }

    bool LockVolume() override { return true; }
    bool UnlockVolume() override { return true; }
    bool DismountVolume() override { return true; }
    bool SendDeviceCommand(uint32_t, void*, uint32_t, void*, uint32_t) override { return true; }

    Core::DeviceGeometry GetGeometry() const override {
        return { m_sectorSize, m_disk.size() / m_sectorSize, "MemoryDisk://NTFS_Demo" };
    }

    uint8_t* GetDiskData() { return m_disk.data(); }
    size_t GetDiskSize() const { return m_disk.size(); }
};

// ============================================================================
// Synthetic Disk Image Generator for NTFS
// ============================================================================
static void SetupSyntheticNtfsDisk(MemoryDiskDevice& dev) {
    uint8_t* disk = dev.GetDiskData();
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr uint32_t SEC_PER_CLUST = 8; // 4096 bytes per cluster
    constexpr uint32_t CLUSTER_SIZE = SECTOR_SIZE * SEC_PER_CLUST;
    constexpr uint32_t MFT_START_LCN = 4; // Sector 32

    // 1. Sector 0: Volume Boot Record
    NtfsBootSector vbr;
    std::memset(&vbr, 0, sizeof(vbr));
    vbr.jumpInstruction[0] = 0xEB;
    vbr.jumpInstruction[1] = 0x52;
    vbr.jumpInstruction[2] = 0x90;
    std::memcpy(vbr.oemId, "NTFS    ", 8);
    vbr.bytesPerSector = SECTOR_SIZE;
    vbr.sectorsPerCluster = SEC_PER_CLUST;
    vbr.mediaDescriptor = 0xF8;
    vbr.totalSectors = dev.GetDiskSize() / SECTOR_SIZE;
    vbr.mftStartLCN = MFT_START_LCN;
    vbr.mftMirrStartLCN = 2;
    vbr.clustersPerMftRecord = -10;   // 1024 bytes
    vbr.clustersPerIndexBuffer = -12; // 4096 bytes
    vbr.volumeSerialNumber = 0x4D5346545F534948ULL;
    vbr.bootSignature = NTFS_BOOT_SIGNATURE;
    std::memcpy(disk, &vbr, sizeof(vbr));

    // Helper to write an MFT record
    auto writeRecord = [&](uint64_t recordNum, const std::vector<uint8_t>& recordData) {
        size_t byteOffset = (MFT_START_LCN * CLUSTER_SIZE) + (recordNum * 1024);
        std::memcpy(disk + byteOffset, recordData.data(), std::min<size_t>(1024, recordData.size()));
    };

    // 2. MFT Record 0 ($MFT itself)
    std::vector<uint8_t> mft0(1024, 0);
    auto* hdr0 = reinterpret_cast<NtfsRecordHeader*>(mft0.data());
    hdr0->magic = NTFS_MAGIC_FILE;
    hdr0->updateSequenceOffset = 0x30;
    hdr0->updateSequenceSize = 3;
    hdr0->sequenceNumber = 1;
    hdr0->firstAttributeOffset = 0x38;
    hdr0->flags = FILE_RECORD_IN_USE;
    hdr0->allocatedBytes = 1024;
    hdr0->recordNumber = 0;

    // $DATA attribute for $MFT (non-resident pointing to clusters 4..11)
    uint8_t* a0 = mft0.data() + 0x38;
    auto* ah0 = reinterpret_cast<NtfsAttributeHeader*>(a0);
    ah0->type = ATTR_DATA;
    ah0->length = 0x48;
    ah0->nonResidentFlag = 1;
    auto* nrh0 = reinterpret_cast<NtfsNonResidentAttributeHeader*>(a0 + sizeof(NtfsAttributeHeader));
    nrh0->dataRunsOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsNonResidentAttributeHeader);
    nrh0->allocatedSize = 8 * CLUSTER_SIZE;
    nrh0->dataSize = 8 * CLUSTER_SIZE;
    // Runlist: 1 byte header (offset len 1, count len 1) = 0x11, count = 8, offset = 4
    uint8_t* r0 = a0 + nrh0->dataRunsOffset;
    r0[0] = 0x11; r0[1] = 0x08; r0[2] = 0x04; r0[3] = 0x00;
    *reinterpret_cast<uint32_t*>(a0 + ah0->length) = ATTR_END;
    writeRecord(MFT_REC_MFT, mft0);

    // 3. MFT Record 6 ($Bitmap)
    std::vector<uint8_t> mft6(1024, 0);
    auto* hdr6 = reinterpret_cast<NtfsRecordHeader*>(mft6.data());
    hdr6->magic = NTFS_MAGIC_FILE;
    hdr6->updateSequenceOffset = 0x30;
    hdr6->updateSequenceSize = 3;
    hdr6->sequenceNumber = 1;
    hdr6->firstAttributeOffset = 0x38;
    hdr6->flags = FILE_RECORD_IN_USE;
    hdr6->allocatedBytes = 1024;
    hdr6->recordNumber = 6;

    // $DATA attribute for $Bitmap (non-resident pointing to Cluster 12)
    uint8_t* a6 = mft6.data() + 0x38;
    auto* ah6 = reinterpret_cast<NtfsAttributeHeader*>(a6);
    ah6->type = ATTR_DATA;
    ah6->length = 0x48;
    ah6->nonResidentFlag = 1;
    auto* nrh6 = reinterpret_cast<NtfsNonResidentAttributeHeader*>(a6 + sizeof(NtfsAttributeHeader));
    nrh6->dataRunsOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsNonResidentAttributeHeader);
    nrh6->allocatedSize = CLUSTER_SIZE;
    nrh6->dataSize = 512;
    uint8_t* r6 = a6 + nrh6->dataRunsOffset;
    r6[0] = 0x11; r6[1] = 0x01; r6[2] = 12; r6[3] = 0x00; // Cluster 12
    *reinterpret_cast<uint32_t*>(a6 + ah6->length) = ATTR_END;
    writeRecord(MFT_REC_BITMAP, mft6);

    // Populate cluster bitmap in Cluster 12:
    // Clusters 0..15 in use (0xFF, 0xFF). Cluster 16 (bit 0 of byte 2) and Cluster 17 (bit 1 of byte 2) in use!
    uint8_t* bitmapData = disk + (12 * CLUSTER_SIZE);
    bitmapData[0] = 0xFF;
    bitmapData[1] = 0xFF;
    bitmapData[2] = 0x03; // Clusters 16 and 17 allocated

    // 4. MFT Record 16 (File: "passwords.txt")
    std::vector<uint8_t> mft16(1024, 0);
    auto* hdr16 = reinterpret_cast<NtfsRecordHeader*>(mft16.data());
    hdr16->magic = NTFS_MAGIC_FILE;
    hdr16->updateSequenceOffset = 0x30;
    hdr16->updateSequenceSize = 3;
    hdr16->sequenceNumber = 1;
    hdr16->firstAttributeOffset = 0x38;
    hdr16->flags = FILE_RECORD_IN_USE;
    hdr16->allocatedBytes = 1024;
    hdr16->recordNumber = 16;

    // $FILE_NAME attribute
    uint8_t* a16 = mft16.data() + 0x38;
    auto* ah16_fn = reinterpret_cast<NtfsAttributeHeader*>(a16);
    ah16_fn->type = ATTR_FILE_NAME;
    ah16_fn->length = 0x68;
    ah16_fn->nonResidentFlag = 0;
    auto* res16_fn = reinterpret_cast<NtfsResidentAttributeHeader*>(a16 + sizeof(NtfsAttributeHeader));
    res16_fn->valueOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsResidentAttributeHeader);
    res16_fn->valueLength = 0x44;
    auto* fn16 = reinterpret_cast<NtfsFileNameAttribute*>(a16 + res16_fn->valueOffset);
    fn16->parentDirectory = MFT_REC_ROOT;
    fn16->realSize = 64;
    fn16->allocatedSize = CLUSTER_SIZE;
    std::u16string fName16 = u"passwords.txt";
    fn16->fileNameLength = static_cast<uint8_t>(fName16.length());
    for (size_t i = 0; i < fName16.length(); ++i) fn16->fileName[i] = fName16[i];

    // $DATA attribute (non-resident pointing to Cluster 16)
    uint8_t* a16_d = a16 + ah16_fn->length;
    auto* ah16_d = reinterpret_cast<NtfsAttributeHeader*>(a16_d);
    ah16_d->type = ATTR_DATA;
    ah16_d->length = 0x48;
    ah16_d->nonResidentFlag = 1;
    auto* nrh16 = reinterpret_cast<NtfsNonResidentAttributeHeader*>(a16_d + sizeof(NtfsAttributeHeader));
    nrh16->dataRunsOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsNonResidentAttributeHeader);
    nrh16->allocatedSize = CLUSTER_SIZE;
    nrh16->dataSize = 48;
    uint8_t* r16 = a16_d + nrh16->dataRunsOffset;
    r16[0] = 0x11; r16[1] = 0x01; r16[2] = 16; r16[3] = 0x00; // Cluster 16
    *reinterpret_cast<uint32_t*>(a16_d + ah16_d->length) = ATTR_END;
    writeRecord(16, mft16);

    // Payload for File 1 at Cluster 16
    const char* secretPayload = "SUPER_SECRET_PAYLOAD: vault_key_9999_xyz_CONFIDENTIAL_DEFENSE_2026!";
    std::memcpy(disk + (16 * CLUSTER_SIZE), secretPayload, std::strlen(secretPayload));

    // 5. MFT Record 17 (Folder: "Finance")
    std::vector<uint8_t> mft17(1024, 0);
    auto* hdr17 = reinterpret_cast<NtfsRecordHeader*>(mft17.data());
    hdr17->magic = NTFS_MAGIC_FILE;
    hdr17->updateSequenceOffset = 0x30;
    hdr17->updateSequenceSize = 3;
    hdr17->sequenceNumber = 1;
    hdr17->firstAttributeOffset = 0x38;
    hdr17->flags = FILE_RECORD_IN_USE | FILE_RECORD_DIRECTORY;
    hdr17->allocatedBytes = 1024;
    hdr17->recordNumber = 17;

    // $FILE_NAME for Finance
    uint8_t* a17 = mft17.data() + 0x38;
    auto* ah17_fn = reinterpret_cast<NtfsAttributeHeader*>(a17);
    ah17_fn->type = ATTR_FILE_NAME;
    ah17_fn->length = 0x58;
    ah17_fn->nonResidentFlag = 0;
    auto* res17_fn = reinterpret_cast<NtfsResidentAttributeHeader*>(a17 + sizeof(NtfsAttributeHeader));
    res17_fn->valueOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsResidentAttributeHeader);
    res17_fn->valueLength = 0x34;
    auto* fn17 = reinterpret_cast<NtfsFileNameAttribute*>(a17 + res17_fn->valueOffset);
    fn17->parentDirectory = MFT_REC_ROOT;
    fn17->flags = 0x10000000; // Directory
    std::u16string fName17 = u"Finance";
    fn17->fileNameLength = static_cast<uint8_t>(fName17.length());
    for (size_t i = 0; i < fName17.length(); ++i) fn17->fileName[i] = fName17[i];

    // $INDEX_ROOT for Finance containing child Record 18 ("q4_report.xlsx")
    uint8_t* a17_ir = a17 + ah17_fn->length;
    auto* ah17_ir = reinterpret_cast<NtfsAttributeHeader*>(a17_ir);
    ah17_ir->type = ATTR_INDEX_ROOT;
    ah17_ir->nonResidentFlag = 0;

    auto* res17_ir = reinterpret_cast<NtfsResidentAttributeHeader*>(a17_ir + sizeof(NtfsAttributeHeader));
    res17_ir->valueOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsResidentAttributeHeader);
    auto* ir17 = reinterpret_cast<NtfsIndexRootHeader*>(a17_ir + res17_ir->valueOffset);
    ir17->attributeType = ATTR_FILE_NAME;
    ir17->collationRule = 1;
    ir17->indexAllocationEntrySize = 4096;
    ir17->clustersPerIndexRecord = 1;

    auto* idxHdr17 = reinterpret_cast<NtfsIndexHeader*>(ir17 + 1);
    idxHdr17->firstEntryOffset = sizeof(NtfsIndexHeader);

    // Entry in Finance pointing to Record 18
    uint8_t* e18Ptr = reinterpret_cast<uint8_t*>(idxHdr17 + 1);
    auto* e18 = reinterpret_cast<NtfsIndexEntry*>(e18Ptr);
    e18->fileReference = 18;
    e18->flags = 0;
    auto* fn18 = reinterpret_cast<NtfsFileNameAttribute*>(e18Ptr + sizeof(NtfsIndexEntry));
    fn18->parentDirectory = 17;
    std::u16string fName18 = u"q4_report.xlsx";
    fn18->fileNameLength = static_cast<uint8_t>(fName18.length());
    for (size_t i = 0; i < fName18.length(); ++i) fn18->fileName[i] = fName18[i];
    e18->keyLength = sizeof(NtfsFileNameAttribute) + (fn18->fileNameLength * 2);
    e18->length = sizeof(NtfsIndexEntry) + e18->keyLength;

    // End entry
    auto* e18_end = reinterpret_cast<NtfsIndexEntry*>(e18Ptr + e18->length);
    e18_end->length = sizeof(NtfsIndexEntry);
    e18_end->flags = INDEX_ENTRY_LAST;

    idxHdr17->totalEntriesSize = sizeof(NtfsIndexHeader) + e18->length + e18_end->length;
    idxHdr17->allocatedSize = idxHdr17->totalEntriesSize;
    res17_ir->valueLength = sizeof(NtfsIndexRootHeader) + idxHdr17->totalEntriesSize;
    ah17_ir->length = sizeof(NtfsAttributeHeader) + sizeof(NtfsResidentAttributeHeader) + res17_ir->valueLength;
    *reinterpret_cast<uint32_t*>(a17_ir + ah17_ir->length) = ATTR_END;
    writeRecord(17, mft17);

    // 6. MFT Record 18 (Child file inside Finance: "q4_report.xlsx")
    std::vector<uint8_t> mft18(1024, 0);
    auto* hdr18 = reinterpret_cast<NtfsRecordHeader*>(mft18.data());
    hdr18->magic = NTFS_MAGIC_FILE;
    hdr18->updateSequenceOffset = 0x30;
    hdr18->updateSequenceSize = 3;
    hdr18->sequenceNumber = 1;
    hdr18->firstAttributeOffset = 0x38;
    hdr18->flags = FILE_RECORD_IN_USE;
    hdr18->allocatedBytes = 1024;
    hdr18->recordNumber = 18;

    uint8_t* a18 = mft18.data() + 0x38;
    auto* ah18_d = reinterpret_cast<NtfsAttributeHeader*>(a18);
    ah18_d->type = ATTR_DATA;
    ah18_d->length = 0x48;
    ah18_d->nonResidentFlag = 1;
    auto* nrh18 = reinterpret_cast<NtfsNonResidentAttributeHeader*>(a18 + sizeof(NtfsAttributeHeader));
    nrh18->dataRunsOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsNonResidentAttributeHeader);
    nrh18->allocatedSize = CLUSTER_SIZE;
    nrh18->dataSize = 50;
    uint8_t* r18 = a18 + nrh18->dataRunsOffset;
    r18[0] = 0x11; r18[1] = 0x01; r18[2] = 17; r18[3] = 0x00; // Cluster 17
    *reinterpret_cast<uint32_t*>(a18 + ah18_d->length) = ATTR_END;
    writeRecord(18, mft18);

    const char* reportPayload = "FINANCE_Q4_BUDGET_REVENUE_REPORT_TOP_SECRET_AUDIT!";
    std::memcpy(disk + (17 * CLUSTER_SIZE), reportPayload, std::strlen(reportPayload));

    // 7. MFT Record 5 (Root Directory ".")
    // Contains entries for "passwords.txt" (Record 16) and "Finance" (Record 17)
    std::vector<uint8_t> mft5(1024, 0);
    auto* hdr5 = reinterpret_cast<NtfsRecordHeader*>(mft5.data());
    hdr5->magic = NTFS_MAGIC_FILE;
    hdr5->updateSequenceOffset = 0x30;
    hdr5->updateSequenceSize = 3;
    hdr5->sequenceNumber = 1;
    hdr5->firstAttributeOffset = 0x38;
    hdr5->flags = FILE_RECORD_IN_USE | FILE_RECORD_DIRECTORY;
    hdr5->allocatedBytes = 1024;
    hdr5->recordNumber = 5;

    uint8_t* a5 = mft5.data() + 0x38;
    auto* ah5_ir = reinterpret_cast<NtfsAttributeHeader*>(a5);
    ah5_ir->type = ATTR_INDEX_ROOT;
    ah5_ir->nonResidentFlag = 0;
    auto* res5_ir = reinterpret_cast<NtfsResidentAttributeHeader*>(a5 + sizeof(NtfsAttributeHeader));
    res5_ir->valueOffset = sizeof(NtfsAttributeHeader) + sizeof(NtfsResidentAttributeHeader);
    auto* ir5 = reinterpret_cast<NtfsIndexRootHeader*>(a5 + res5_ir->valueOffset);
    ir5->attributeType = ATTR_FILE_NAME;
    ir5->collationRule = 1;
    ir5->indexAllocationEntrySize = 4096;
    ir5->clustersPerIndexRecord = 1;

    auto* idxHdr5 = reinterpret_cast<NtfsIndexHeader*>(ir5 + 1);
    idxHdr5->firstEntryOffset = sizeof(NtfsIndexHeader);

    // Entry 1: passwords.txt (Record 16)
    uint8_t* e16Ptr = reinterpret_cast<uint8_t*>(idxHdr5 + 1);
    auto* e16 = reinterpret_cast<NtfsIndexEntry*>(e16Ptr);
    e16->fileReference = 16;
    auto* fn16_root = reinterpret_cast<NtfsFileNameAttribute*>(e16Ptr + sizeof(NtfsIndexEntry));
    fn16_root->parentDirectory = 5;
    fn16_root->fileNameLength = static_cast<uint8_t>(fName16.length());
    for (size_t i = 0; i < fName16.length(); ++i) fn16_root->fileName[i] = fName16[i];
    e16->keyLength = sizeof(NtfsFileNameAttribute) + (fn16_root->fileNameLength * 2);
    e16->length = sizeof(NtfsIndexEntry) + e16->keyLength;

    // Entry 2: Finance (Record 17)
    uint8_t* e17Ptr = e16Ptr + e16->length;
    auto* e17 = reinterpret_cast<NtfsIndexEntry*>(e17Ptr);
    e17->fileReference = 17;
    auto* fn17_root = reinterpret_cast<NtfsFileNameAttribute*>(e17Ptr + sizeof(NtfsIndexEntry));
    fn17_root->parentDirectory = 5;
    fn17_root->flags = 0x10000000; // Directory
    fn17_root->fileNameLength = static_cast<uint8_t>(fName17.length());
    for (size_t i = 0; i < fName17.length(); ++i) fn17_root->fileName[i] = fName17[i];
    e17->keyLength = sizeof(NtfsFileNameAttribute) + (fn17_root->fileNameLength * 2);
    e17->length = sizeof(NtfsIndexEntry) + e17->keyLength;

    // Entry 3: End Entry
    uint8_t* endPtr = e17Ptr + e17->length;
    auto* e_end = reinterpret_cast<NtfsIndexEntry*>(endPtr);
    e_end->length = sizeof(NtfsIndexEntry);
    e_end->flags = INDEX_ENTRY_LAST;

    idxHdr5->totalEntriesSize = sizeof(NtfsIndexHeader) + e16->length + e17->length + e_end->length;
    idxHdr5->allocatedSize = idxHdr5->totalEntriesSize;
    res5_ir->valueLength = sizeof(NtfsIndexRootHeader) + idxHdr5->totalEntriesSize;
    ah5_ir->length = sizeof(NtfsAttributeHeader) + sizeof(NtfsResidentAttributeHeader) + res5_ir->valueLength;
    *reinterpret_cast<uint32_t*>(a5 + ah5_ir->length) = ATTR_END;
    writeRecord(MFT_REC_ROOT, mft5);
}

// ============================================================================
// Main Verification Runner
// ============================================================================
int main(int argc, char* argv[]) {
    std::cout << "================================================================================\n";
    std::cout << "        NTFS SECURE DELETION & FORENSIC VERIFICATION SUITE                      \n";
    std::cout << "================================================================================\n";

    // Allocate 1MB synthetic in-memory disk
    constexpr size_t DISK_SIZE = 1024 * 1024; // 1 MB
    MemoryDiskDevice diskDevice(DISK_SIZE, 512);

    std::cout << "[Setup] Initializing synthetic NTFS volume in RAM (1MB)...\n";
    SetupSyntheticNtfsDisk(diskDevice);

    Hardware::HDDController hddController(&diskDevice);
    NtfsDriver driver(&hddController);

    std::cout << "[Setup] Mounting NTFS filesystem through decoupled controller...\n";
    if (!driver.Mount()) {
        std::cerr << "[-] Error: Failed to mount NTFS filesystem.\n";
        return 1;
    }
    driver.PrintBootInfo();

    std::string mode = "all";
    std::string inputPath = "";
    if (argc > 1) mode = argv[1];
    if (argc > 2) inputPath = argv[2];

    if (mode == "--file" || mode == "all") {
        std::string targetFile = inputPath.empty() ? "passwords.txt" : inputPath;
        std::cout << "\n################################################################################\n";
        std::cout << " TEST SCENARIO 1: VERIFICATION & ERASURE OF FILE INPUT: '" << targetFile << "'\n";
        std::cout << "################################################################################\n";

        bool ok = driver.VerifyAndErase(targetFile);
        assert(ok);
        std::cout << "[PASS] File verification and erasure completed successfully.\n";
    }

    if (mode == "--folder" || mode == "all") {
        std::string targetFolder = inputPath.empty() ? "Finance" : inputPath;
        std::cout << "\n################################################################################\n";
        std::cout << " TEST SCENARIO 2: VERIFICATION & RECURSIVE ERASURE OF FOLDER INPUT: '" << targetFolder << "'\n";
        std::cout << "################################################################################\n";

        bool ok = driver.VerifyAndErase(targetFolder);
        assert(ok);
        std::cout << "[PASS] Folder verification and recursive erasure completed successfully.\n";
    }

    if (mode == "--disk" || mode == "all") {
        std::cout << "\n################################################################################\n";
        std::cout << " TEST SCENARIO 3: COMPLETE DISK / VOLUME FORMAT VERIFICATION                   \n";
        std::cout << "################################################################################\n";

        bool ok = driver.VerifyAndFormatDrive(true);
        assert(ok);
        std::cout << "[PASS] Disk format and post-format verification completed successfully.\n";
    }

    std::cout << "\n================================================================================\n";
    std::cout << " [ALL TESTS PASSED] 100% FORENSIC ZERO-RECOVERY VERIFICATION CONFIRMED          \n";
    std::cout << "================================================================================\n";
    return 0;
}
