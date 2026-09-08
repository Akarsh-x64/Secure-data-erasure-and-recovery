#include "../../Acquisition/LinuxReadOnlyStorage.h"
#include "../../Core/ByteReader.h"
#include "../../Core/StorageRegion.h"
#include "../../Filesystems/FilesystemDetector.h"
#include "../../Partitions/MBRParser.h"
#include "../../Partitions/GPTParser.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <vector>

// ---- Test helpers ----

static int g_passed = 0;
static int g_failed = 0;

static void Check(const char* name, bool condition) {
    if (condition) { std::cout << "  [PASS] " << name << "\n"; ++g_passed; }
    else           { std::cout << "  [FAIL] " << name << "\n"; ++g_failed; }
}

static const char* TEMP_IMG = "./phase3_test_temp.img";

static bool WriteImage(const std::vector<uint8_t>& data) {
    std::ofstream f(TEMP_IMG, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    return true;
}

static void WriteLE16(uint8_t* buf, uint16_t val) {
    buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF;
}
static void WriteLE32(uint8_t* buf, uint32_t val) {
    buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF; buf[3] = (val >> 24) & 0xFF;
}
static void WriteLE64(uint8_t* buf, uint64_t val) {
    for (int i = 0; i < 8; ++i) buf[i] = (val >> (i * 8)) & 0xFF;
}
static void WriteBE16(uint8_t* buf, uint16_t val) {
    buf[0] = (val >> 8) & 0xFF; buf[1] = val & 0xFF;
}
static void WriteBE32(uint8_t* buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF; buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF; buf[3] = val & 0xFF;
}

using FSType = Recovery::Core::FileSystemType;
using Region = Recovery::Core::StorageRegion;

// Detect helper: writes image, opens storage, detects FS at given region
static FSType DetectFromImage(const std::vector<uint8_t>& img, Region region) {
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    if (!storage.Open(TEMP_IMG)) return FSType::Unknown;
    Recovery::Core::ByteReader reader(&storage);
    auto result = Recovery::Filesystems::FilesystemDetector::Detect(reader, region);
    storage.Close();
    return result;
}

// ---- Filesystem image builders ----

// Build a minimal NTFS VBR at a given offset within an image
static void PlaceNTFS(std::vector<uint8_t>& img, uint64_t offset) {
    std::memcpy(img.data() + offset + 0x03, "NTFS    ", 8);
    WriteLE16(img.data() + offset + 0x0B, 512);   // bytes per sector
    img[offset + 0x0D] = 8;                        // sectors per cluster
}

// Build a minimal exFAT VBR
static void PlaceExFAT(std::vector<uint8_t>& img, uint64_t offset) {
    std::memcpy(img.data() + offset + 0x03, "EXFAT   ", 8);
    img[offset + 0x6C] = 9;  // BytesPerSectorShift = 9 (512 bytes)
}

// Build a minimal valid FAT32 VBR
static void PlaceFAT32(std::vector<uint8_t>& img, uint64_t offset) {
    uint8_t* v = img.data() + offset;
    v[0x00] = 0xEB; v[0x01] = 0x58; v[0x02] = 0x90;  // Jump
    WriteLE16(v + 0x0B, 512);   // BytsPerSec
    v[0x0D] = 8;                // SecPerClus
    WriteLE16(v + 0x0E, 32);    // RsvdSecCnt
    v[0x10] = 2;                // NumFATs
    WriteLE16(v + 0x11, 0);     // RootEntCnt = 0 (FAT32)
    WriteLE16(v + 0x13, 0);     // TotSec16 = 0 (FAT32)
    v[0x15] = 0xF8;             // Media
    WriteLE16(v + 0x16, 0);     // FATSz16 = 0 (FAT32)
    WriteLE32(v + 0x24, 1024);  // FATSz32 > 0
    WriteLE32(v + 0x2C, 2);     // RootClus
    std::memcpy(v + 0x52, "FAT32   ", 8);  // FilSysType
}

// Build a minimal ext superblock at partition offset 1024
static void PlaceExt(std::vector<uint8_t>& img, uint64_t partBase,
                     uint32_t revLevel, uint32_t compatFeatures, uint32_t incompatFeatures) {
    uint64_t sb = partBase + 1024;
    // Magic at superblock+0x38
    WriteLE16(img.data() + sb + 0x38, 0xEF53);
    // Revision at superblock+0x4C
    WriteLE32(img.data() + sb + 0x4C, revLevel);
    // Compat features at superblock+0x5C
    WriteLE32(img.data() + sb + 0x5C, compatFeatures);
    // Incompat features at superblock+0x60
    WriteLE32(img.data() + sb + 0x60, incompatFeatures);
}

// Build a minimal XFS superblock
static void PlaceXFS(std::vector<uint8_t>& img, uint64_t offset) {
    WriteBE32(img.data() + offset, 0x58465342);      // "XFSB"
    WriteBE32(img.data() + offset + 4, 4096);         // block size
}

// Build a minimal HFS+ volume header at partition offset 1024
static void PlaceHFSPlus(std::vector<uint8_t>& img, uint64_t partBase) {
    WriteBE16(img.data() + partBase + 1024, 0x482B);  // "H+" signature
    WriteBE16(img.data() + partBase + 1026, 4);        // version
}

// Build a minimal APFS container superblock
static void PlaceAPFS(std::vector<uint8_t>& img, uint64_t offset) {
    WriteLE32(img.data() + offset + 32, 0x4253584E);  // "NXSB" LE
    WriteLE32(img.data() + offset + 36, 4096);         // block size
}

// ---- Individual filesystem detection tests ----

static void TestNTFS() {
    std::cout << "\nTest: NTFS detection\n";
    std::vector<uint8_t> img(4096, 0);
    PlaceNTFS(img, 0);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as NTFS", r == FSType::NTFS);
}

static void TestExFAT() {
    std::cout << "\nTest: exFAT detection\n";
    std::vector<uint8_t> img(4096, 0);
    PlaceExFAT(img, 0);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as exFAT", r == FSType::ExFAT);
}

static void TestFAT32() {
    std::cout << "\nTest: FAT32 detection\n";
    std::vector<uint8_t> img(4096, 0);
    PlaceFAT32(img, 0);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as FAT32", r == FSType::FAT32);
}

static void TestFAT32RejectsPartialMatch() {
    std::cout << "\nTest: FAT32 rejects string-only match (no structural validity)\n";
    std::vector<uint8_t> img(4096, 0);
    // Only place the "FAT32   " string, no valid BPB
    std::memcpy(img.data() + 0x52, "FAT32   ", 8);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("NOT detected as FAT32 (missing BPB)", r != FSType::FAT32);
}

static void TestExt4() {
    std::cout << "\nTest: ext4 detection (EXTENTS flag)\n";
    std::vector<uint8_t> img(8192, 0);
    PlaceExt(img, 0, 1, 0x0004, 0x0040);  // HAS_JOURNAL + EXTENTS
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as ext4", r == FSType::EXT4);
}

static void TestExt3() {
    std::cout << "\nTest: ext3 detection (HAS_JOURNAL, no ext4 flags)\n";
    std::vector<uint8_t> img(8192, 0);
    PlaceExt(img, 0, 1, 0x0004, 0x0000);  // HAS_JOURNAL, no ext4 incompat
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as ext3", r == FSType::EXT3);
}

static void TestExt2() {
    std::cout << "\nTest: ext2 detection (no journal, no ext4 flags)\n";
    std::vector<uint8_t> img(8192, 0);
    PlaceExt(img, 0, 1, 0x0000, 0x0000);  // No journal, no ext4
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as ext2", r == FSType::EXT2);
}

static void TestExt2Rev0() {
    std::cout << "\nTest: ext2 revision 0 (original, no feature fields)\n";
    std::vector<uint8_t> img(8192, 0);
    PlaceExt(img, 0, 0, 0, 0);  // Rev 0
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as ext2 (rev 0)", r == FSType::EXT2);
}

static void TestXFS() {
    std::cout << "\nTest: XFS detection\n";
    std::vector<uint8_t> img(8192, 0);
    PlaceXFS(img, 0);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as XFS", r == FSType::XFS);
}

static void TestHFSPlus() {
    std::cout << "\nTest: HFS+ detection\n";
    std::vector<uint8_t> img(8192, 0);
    PlaceHFSPlus(img, 0);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as HFS+", r == FSType::HFSPlus);
}

static void TestAPFS() {
    std::cout << "\nTest: APFS container detection\n";
    std::vector<uint8_t> img(8192, 0);
    PlaceAPFS(img, 0);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as APFS", r == FSType::APFS);
}

// ---- Edge case tests ----

static void TestUnknownFilesystem() {
    std::cout << "\nTest: All-zero partition → Unknown\n";
    std::vector<uint8_t> img(8192, 0);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as Unknown", r == FSType::Unknown);
}

static void TestTinyPartition() {
    std::cout << "\nTest: Tiny partition (16 bytes) → Unknown without crash\n";
    std::vector<uint8_t> img(512, 0);  // File must be >= sector for storage
    auto r = DetectFromImage(img, Region(0, 16));
    Check("Detected as Unknown", r == FSType::Unknown);
    Check("No crash", true);
}

static void TestZeroSizePartition() {
    std::cout << "\nTest: Zero-size partition → Unknown\n";
    std::vector<uint8_t> img(512, 0);
    auto r = DetectFromImage(img, Region(0, 0));
    Check("Detected as Unknown", r == FSType::Unknown);
}

static void TestRandomGarbage() {
    std::cout << "\nTest: Random garbage data → Unknown\n";
    std::vector<uint8_t> img(8192);
    for (size_t i = 0; i < img.size(); ++i) img[i] = static_cast<uint8_t>((i * 37 + 13) & 0xFF);
    auto r = DetectFromImage(img, Region(0, img.size()));
    Check("Detected as Unknown", r == FSType::Unknown);
}

// ---- Offset-isolation tests ----
// Ensure detection at partition offset X doesn't bleed into adjacent partitions

static void TestOffsetIsolationNTFS() {
    std::cout << "\nTest: Offset isolation — NTFS at offset 1MB, not at offset 0\n";
    uint64_t partOffset = 1048576;  // 1 MB
    std::vector<uint8_t> img(partOffset + 8192, 0);
    PlaceNTFS(img, partOffset);

    // Detection at partition offset should find NTFS
    auto r1 = DetectFromImage(img, Region(partOffset, 8192));
    Check("NTFS detected at correct offset", r1 == FSType::NTFS);

    // Detection at offset 0 should NOT find NTFS
    auto r2 = DetectFromImage(img, Region(0, partOffset));
    Check("Unknown at offset 0 (no bleed)", r2 == FSType::Unknown);
}

static void TestOffsetIsolationTwoFilesystems() {
    std::cout << "\nTest: Offset isolation — ext4 at offset 0, NTFS at offset 64K\n";
    uint64_t part2Offset = 65536;  // 64 KB
    std::vector<uint8_t> img(part2Offset + 8192, 0);
    PlaceExt(img, 0, 1, 0x0004, 0x0040);  // ext4 at base 0
    PlaceNTFS(img, part2Offset);            // NTFS at 64K

    auto r1 = DetectFromImage(img, Region(0, part2Offset));
    Check("ext4 at offset 0", r1 == FSType::EXT4);

    auto r2 = DetectFromImage(img, Region(part2Offset, 8192));
    Check("NTFS at offset 64K", r2 == FSType::NTFS);
}

// ---- GPT partition integration ----

static void TestGPTPartitionDetection() {
    std::cout << "\nTest: GPT partition → filesystem detection integration\n";

    // Build a GPT disk image with 2 partitions:
    //   Partition 1: starts at LBA 34, contains NTFS
    //   Partition 2: starts at LBA 2048, contains ext4
    uint32_t sectorSize = 512;
    uint32_t numEntries = 128;
    uint32_t entrySize = 128;
    uint32_t totalSectors = 4096;  // ~2MB image
    std::vector<uint8_t> img(totalSectors * sectorSize, 0);

    // Protective MBR
    img[0x1BE + 4] = 0xEE;
    WriteLE32(img.data() + 0x1BE + 8, 1);
    WriteLE32(img.data() + 0x1BE + 12, totalSectors - 1);
    img[510] = 0x55; img[511] = 0xAA;

    // GPT header at LBA 1
    uint8_t* hdr = img.data() + sectorSize;
    std::memcpy(hdr, "EFI PART", 8);
    WriteLE32(hdr + 8, 0x00010000);
    WriteLE32(hdr + 12, 92);
    WriteLE64(hdr + 24, 1);
    WriteLE64(hdr + 32, totalSectors - 1);
    WriteLE64(hdr + 40, 34);
    WriteLE64(hdr + 48, totalSectors - 34);
    WriteLE64(hdr + 72, 2);
    WriteLE32(hdr + 80, numEntries);
    WriteLE32(hdr + 84, entrySize);

    // Microsoft Basic Data GUID
    static const uint8_t GUID_MSDATA[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };
    // Linux Filesystem GUID
    static const uint8_t GUID_LINUX[16] = {
        0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
        0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
    };

    // Partition entry 1: LBA 34..2047 (NTFS)
    uint8_t* e1 = img.data() + (2 * sectorSize);
    std::memcpy(e1, GUID_MSDATA, 16);
    std::memset(e1 + 16, 0xAA, 16);
    WriteLE64(e1 + 32, 34);
    WriteLE64(e1 + 40, 2047);

    // Partition entry 2: LBA 2048..4000 (ext4)
    uint8_t* e2 = img.data() + (2 * sectorSize) + entrySize;
    std::memcpy(e2, GUID_LINUX, 16);
    std::memset(e2 + 16, 0xBB, 16);
    WriteLE64(e2 + 32, 2048);
    WriteLE64(e2 + 40, 4000);

    // Place filesystem signatures inside the partitions
    PlaceNTFS(img, 34 * sectorSize);
    PlaceExt(img, 2048 * sectorSize, 1, 0x0004, 0x0240);  // ext4 with FLEX_BG + EXTENTS

    WriteImage(img);

    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    Check("Image opened", storage.Open(TEMP_IMG));
    Recovery::Core::ByteReader reader(&storage);

    // Parse partitions via GPT
    Recovery::Partitions::GPTParser gpt;
    Check("GPT CanParse", gpt.CanParse(reader));
    auto parts = gpt.Parse(reader, sectorSize);
    Check("2 GPT partitions found", parts.size() == 2);

    if (parts.size() == 2) {
        // Detect filesystem for each partition
        Region r1(parts[0].startOffset, parts[0].sizeBytes);
        auto fs1 = Recovery::Filesystems::FilesystemDetector::Detect(reader, r1);
        Check("Partition 1 = NTFS", fs1 == FSType::NTFS);

        Region r2(parts[1].startOffset, parts[1].sizeBytes);
        auto fs2 = Recovery::Filesystems::FilesystemDetector::Detect(reader, r2);
        Check("Partition 2 = ext4", fs2 == FSType::EXT4);
    }

    storage.Close();
}

// ---- GetFileSystemName test ----

static void TestGetFileSystemName() {
    std::cout << "\nTest: GetFileSystemName\n";
    using D = Recovery::Filesystems::FilesystemDetector;
    Check("NTFS name",    D::GetFileSystemName(FSType::NTFS) == "NTFS");
    Check("exFAT name",   D::GetFileSystemName(FSType::ExFAT) == "exFAT");
    Check("FAT32 name",   D::GetFileSystemName(FSType::FAT32) == "FAT32");
    Check("ext2 name",    D::GetFileSystemName(FSType::EXT2) == "ext2");
    Check("ext3 name",    D::GetFileSystemName(FSType::EXT3) == "ext3");
    Check("ext4 name",    D::GetFileSystemName(FSType::EXT4) == "ext4");
    Check("XFS name",     D::GetFileSystemName(FSType::XFS) == "XFS");
    Check("HFS+ name",    D::GetFileSystemName(FSType::HFSPlus) == "HFS+");
    Check("APFS name",    D::GetFileSystemName(FSType::APFS) == "APFS");
    Check("Unknown name", D::GetFileSystemName(FSType::Unknown) == "Unknown");
}

// ---- Main ----

int main() {
    std::cout << "=============================================\n";
    std::cout << "  Recovery Module — Phase 3 Test\n";
    std::cout << "  Filesystem Detection\n";
    std::cout << "=============================================\n";

    // Individual filesystem detection
    TestNTFS();
    TestExFAT();
    TestFAT32();
    TestFAT32RejectsPartialMatch();
    TestExt4();
    TestExt3();
    TestExt2();
    TestExt2Rev0();
    TestXFS();
    TestHFSPlus();
    TestAPFS();

    // Edge cases
    TestUnknownFilesystem();
    TestTinyPartition();
    TestZeroSizePartition();
    TestRandomGarbage();

    // Offset isolation
    TestOffsetIsolationNTFS();
    TestOffsetIsolationTwoFilesystems();

    // GPT integration
    TestGPTPartitionDetection();

    // Utility
    TestGetFileSystemName();

    // Cleanup
    std::remove(TEMP_IMG);

    std::cout << "\n=============================================\n";
    std::cout << "  Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "=============================================\n";

    return g_failed > 0 ? 1 : 0;
}
