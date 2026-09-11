#include "../../Acquisition/LinuxReadOnlyStorage.h"
#include "../../Core/ByteReader.h"
#include "../../Partitions/MBRParser.h"
#include "../../Partitions/GPTParser.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <vector>
#include <functional>

// ---- Helpers ----

static int g_passed = 0;
static int g_failed = 0;

static void Check(const char* name, bool condition) {
    if (condition) {
        std::cout << "  [PASS] " << name << "\n";
        ++g_passed;
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failed;
    }
}

// Write raw bytes to a temp .img file, open with LinuxReadOnlyStorage
static const char* TEMP_IMG = "./phase2_test_temp.img";

static bool WriteImage(const std::vector<uint8_t>& data) {
    std::ofstream f(TEMP_IMG, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    return true;
}

static void WriteLE32(uint8_t* buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

static void WriteLE64(uint8_t* buf, uint64_t val) {
    for (int i = 0; i < 8; ++i) buf[i] = (val >> (i * 8)) & 0xFF;
}

// Build a minimal valid MBR (512 bytes) with given partition entries
// Each entry: {typeByte, bootable, startLBA, sectorCount}
struct MBREntry { uint8_t type; bool boot; uint32_t startLBA; uint32_t sectors; };

static std::vector<uint8_t> BuildMBR(const std::vector<MBREntry>& entries) {
    std::vector<uint8_t> mbr(512, 0);
    // Signature
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    for (size_t i = 0; i < entries.size() && i < 4; ++i) {
        uint8_t* e = mbr.data() + 0x1BE + (i * 16);
        e[0] = entries[i].boot ? 0x80 : 0x00;
        e[4] = entries[i].type;
        WriteLE32(e + 8, entries[i].startLBA);
        WriteLE32(e + 12, entries[i].sectors);
    }
    return mbr;
}

// Build a minimal GPT image: protective MBR + GPT header + partition entries
struct GPTEntry {
    uint8_t typeGUID[16];
    uint8_t uniqueGUID[16];
    uint64_t firstLBA;
    uint64_t lastLBA;
    uint64_t attributes;
    std::string name; // ASCII only for tests
};

// Microsoft Basic Data GUID (mixed-endian on-disk)
static const uint8_t GUID_MSDATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

// EFI System Partition GUID
static const uint8_t GUID_EFI[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static std::vector<uint8_t> BuildGPT(const std::vector<GPTEntry>& entries, uint32_t sectorSize = 512) {
    // Minimum image: protective MBR + GPT header sector + enough sectors for entries
    uint32_t numEntries = 128; // Standard GPT
    uint32_t entrySize = 128;
    uint32_t entrySectors = (numEntries * entrySize + sectorSize - 1) / sectorSize;
    uint32_t totalSectors = 2 + entrySectors + 1; // MBR + header + entries + margin
    std::vector<uint8_t> img(totalSectors * sectorSize, 0);

    // Protective MBR
    img[0x1BE + 4] = 0xEE; // type = protective
    WriteLE32(img.data() + 0x1BE + 8, 1); // start LBA
    WriteLE32(img.data() + 0x1BE + 12, totalSectors - 1); // sectors
    img[510] = 0x55;
    img[511] = 0xAA;

    // GPT header at LBA 1
    uint8_t* hdr = img.data() + sectorSize;
    std::memcpy(hdr, "EFI PART", 8);          // Signature
    WriteLE32(hdr + 8, 0x00010000);            // Revision 1.0
    WriteLE32(hdr + 12, 92);                   // Header size
    WriteLE32(hdr + 16, 0);                    // CRC (not validated)
    WriteLE64(hdr + 24, 1);                    // Current LBA
    WriteLE64(hdr + 32, totalSectors - 1);     // Backup LBA
    WriteLE64(hdr + 40, 34);                   // First usable LBA
    WriteLE64(hdr + 48, totalSectors - 34);    // Last usable LBA
    // Disk GUID at 56 (leave zero for test)
    WriteLE64(hdr + 72, 2);                    // Partition entry array at LBA 2
    WriteLE32(hdr + 80, numEntries);           // Number of entries
    WriteLE32(hdr + 84, entrySize);            // Entry size

    // Write partition entries at LBA 2
    uint8_t* entryBase = img.data() + (2 * sectorSize);
    for (size_t i = 0; i < entries.size() && i < numEntries; ++i) {
        uint8_t* e = entryBase + (i * entrySize);
        std::memcpy(e, entries[i].typeGUID, 16);
        std::memcpy(e + 16, entries[i].uniqueGUID, 16);
        WriteLE64(e + 32, entries[i].firstLBA);
        WriteLE64(e + 40, entries[i].lastLBA);
        WriteLE64(e + 48, entries[i].attributes);
        // Write name as UTF-16LE
        for (size_t c = 0; c < entries[i].name.size() && (56 + c * 2 + 1) < entrySize; ++c) {
            e[56 + c * 2] = static_cast<uint8_t>(entries[i].name[c]);
            e[56 + c * 2 + 1] = 0;
        }
    }

    return img;
}

// ---- Tests ----

static void TestEmptyImage() {
    std::cout << "\nTest: Empty image (0 bytes)\n";
    WriteImage({});
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    // Empty file might fail to open or have 0 size
    if (!storage.Open(TEMP_IMG)) {
        Check("CanParse MBR returns false (open failed)", true);
        Check("CanParse GPT returns false (open failed)", true);
        return;
    }
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    Recovery::Partitions::GPTParser gpt;
    Check("CanParse MBR returns false", !mbr.CanParse(reader));
    Check("CanParse GPT returns false", !gpt.CanParse(reader));
    storage.Close();
}

static void TestTinyImage() {
    std::cout << "\nTest: Image < 512 bytes\n";
    std::vector<uint8_t> data(256, 0xAB);
    WriteImage(data);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    Recovery::Partitions::GPTParser gpt;
    Check("CanParse MBR returns false", !mbr.CanParse(reader));
    Check("CanParse GPT returns false", !gpt.CanParse(reader));
    storage.Close();
}

static void TestInvalidMBRSignature() {
    std::cout << "\nTest: Invalid MBR signature\n";
    std::vector<uint8_t> data(512, 0);
    data[510] = 0x00; data[511] = 0x00; // No signature
    WriteImage(data);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    Check("CanParse returns false", !mbr.CanParse(reader));
    storage.Close();
}

static void TestValidMBRZeroPartitions() {
    std::cout << "\nTest: Valid MBR, 0 partitions\n";
    auto img = BuildMBR({});
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    Check("CanParse returns true", mbr.CanParse(reader));
    auto parts = mbr.Parse(reader, 512);
    Check("Parse returns empty vector", parts.empty());
    storage.Close();
}

static void TestValidMBROnePartition() {
    std::cout << "\nTest: Valid MBR, 1 partition\n";
    auto img = BuildMBR({{0x07, true, 2048, 1024}});
    // Extend image to cover the partition
    img.resize(2048 * 512 + 1024 * 512, 0);
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    Check("CanParse returns true", mbr.CanParse(reader));
    auto parts = mbr.Parse(reader, 512);
    Check("1 partition found", parts.size() == 1);
    if (!parts.empty()) {
        Check("index == 0", parts[0].index == 0);
        Check("startLBA == 2048", parts[0].startLBA == 2048);
        Check("sectorCount == 1024", parts[0].sectorCount == 1024);
        Check("startOffset == 2048*512", parts[0].startOffset == 2048ULL * 512);
        Check("sizeBytes == 1024*512", parts[0].sizeBytes == 1024ULL * 512);
        Check("bootable == true", parts[0].bootable == true);
        Check("mbrTypeByte == 0x07", parts[0].mbrTypeByte == 0x07);
        Check("scheme == MBR", parts[0].scheme == Recovery::Core::PartitionScheme::MBR);
        Check("description contains NTFS", parts[0].description.find("NTFS") != std::string::npos);
    }
    storage.Close();
}

static void TestValidMBRFourPartitions() {
    std::cout << "\nTest: Valid MBR, 4 partitions\n";
    auto img = BuildMBR({
        {0x07, true,  2048,   204800},
        {0x0C, false, 206848, 409600},
        {0x83, false, 616448, 819200},
        {0x82, false, 1435648, 16384}
    });
    img.resize(2 * 1024 * 1024, 0); // 2MB image
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    auto parts = mbr.Parse(reader, 512);
    Check("4 partitions found", parts.size() == 4);
    if (parts.size() == 4) {
        Check("part[0] type=0x07 NTFS", parts[0].mbrTypeByte == 0x07);
        Check("part[1] type=0x0C FAT32", parts[1].mbrTypeByte == 0x0C);
        Check("part[2] type=0x83 Linux", parts[2].mbrTypeByte == 0x83);
        Check("part[3] type=0x82 Swap", parts[3].mbrTypeByte == 0x82);
        Check("indices are sequential", parts[0].index == 0 && parts[1].index == 1
              && parts[2].index == 2 && parts[3].index == 3);
    }
    storage.Close();
}

static void TestProtectiveMBR() {
    std::cout << "\nTest: Protective MBR (0xEE)\n";
    auto img = BuildMBR({{0xEE, false, 1, 999}});
    img.resize(1024 * 512, 0);
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    auto parts = mbr.Parse(reader, 512);
    Check("No normal partitions returned", parts.empty());
    Check("HasProtectiveMBR() == true", mbr.HasProtectiveMBR());
    storage.Close();
}

static void TestInvalidGPTSignature() {
    std::cout << "\nTest: Invalid GPT signature\n";
    std::vector<uint8_t> img(4096, 0);
    img[510] = 0x55; img[511] = 0xAA;
    // Write garbage at LBA 1 instead of "EFI PART"
    std::memcpy(img.data() + 512, "NOTAPART", 8);
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::GPTParser gpt;
    Check("CanParse returns false", !gpt.CanParse(reader));
    storage.Close();
}

static void TestValidGPTOnePartition() {
    std::cout << "\nTest: Valid GPT, 1 partition\n";
    uint8_t uniqueGUID[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    auto img = BuildGPT({
        {.typeGUID = {}, .uniqueGUID = {}, .firstLBA = 34, .lastLBA = 2047,
         .attributes = 0, .name = "TestPart"}
    });
    // Fixup: copy the actual GUIDs
    uint8_t* e = img.data() + (2 * 512); // First entry at LBA 2
    std::memcpy(e, GUID_MSDATA, 16);
    std::memcpy(e + 16, uniqueGUID, 16);

    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::GPTParser gpt;
    Check("CanParse returns true", gpt.CanParse(reader));
    auto parts = gpt.Parse(reader, 512);
    Check("1 partition found", parts.size() == 1);
    if (!parts.empty()) {
        Check("index == 0", parts[0].index == 0);
        Check("startLBA == 34", parts[0].startLBA == 34);
        Check("sectorCount == 2014", parts[0].sectorCount == 2014);
        Check("scheme == GPT", parts[0].scheme == Recovery::Core::PartitionScheme::GPT);
        Check("name == TestPart", parts[0].gptName == "TestPart");
        Check("description is Microsoft Basic Data",
              parts[0].description.find("Microsoft Basic Data") != std::string::npos);
        Check("typeGUID matches", std::memcmp(parts[0].typeGUID, GUID_MSDATA, 16) == 0);
    }
    storage.Close();
}

static void TestValidGPTMultiplePartitions() {
    std::cout << "\nTest: Valid GPT, multiple partitions\n";
    GPTEntry e1, e2;
    std::memcpy(e1.typeGUID, GUID_EFI, 16);
    std::memset(e1.uniqueGUID, 0xAA, 16);
    e1.firstLBA = 34; e1.lastLBA = 2047; e1.attributes = 0; e1.name = "EFI";

    std::memcpy(e2.typeGUID, GUID_MSDATA, 16);
    std::memset(e2.uniqueGUID, 0xBB, 16);
    e2.firstLBA = 2048; e2.lastLBA = 10239; e2.attributes = 0; e2.name = "Windows";

    auto img = BuildGPT({e1, e2});
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::GPTParser gpt;
    auto parts = gpt.Parse(reader, 512);
    Check("2 partitions found", parts.size() == 2);
    if (parts.size() == 2) {
        Check("part[0] name=EFI", parts[0].gptName == "EFI");
        Check("part[0] is EFI System",
              parts[0].description.find("EFI System") != std::string::npos);
        Check("part[1] name=Windows", parts[1].gptName == "Windows");
        Check("part[1] is Microsoft Basic Data",
              parts[1].description.find("Microsoft Basic Data") != std::string::npos);
        Check("indices sequential", parts[0].index == 0 && parts[1].index == 1);
    }
    storage.Close();
}

static void TestGPTEmptyEntries() {
    std::cout << "\nTest: GPT with empty entries skipped\n";
    // Create GPT with entry at slot 0 being empty (all zero GUID) and slot 1 valid
    auto img = BuildGPT({}); // All entries empty
    // Put one valid entry at slot 2
    uint8_t* e = img.data() + (2 * 512) + (2 * 128); // slot 2
    std::memcpy(e, GUID_MSDATA, 16);
    std::memset(e + 16, 0xCC, 16);
    WriteLE64(e + 32, 100);  // firstLBA
    WriteLE64(e + 40, 199);  // lastLBA
    // Name
    e[56] = 'D'; e[57] = 0; e[58] = 'a'; e[59] = 0;
    e[60] = 't'; e[61] = 0; e[62] = 'a'; e[63] = 0;

    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::GPTParser gpt;
    auto parts = gpt.Parse(reader, 512);
    Check("1 partition found (empties skipped)", parts.size() == 1);
    if (!parts.empty()) {
        Check("name=Data", parts[0].gptName == "Data");
        Check("startLBA=100", parts[0].startLBA == 100);
    }
    storage.Close();
}

static void TestProtectiveMBRToGPT() {
    std::cout << "\nTest: Protective MBR -> GPT integration\n";
    GPTEntry e1;
    std::memcpy(e1.typeGUID, GUID_MSDATA, 16);
    std::memset(e1.uniqueGUID, 0xDD, 16);
    e1.firstLBA = 34; e1.lastLBA = 1023; e1.attributes = 0; e1.name = "Recovery";

    auto img = BuildGPT({e1}); // BuildGPT already includes protective MBR
    WriteImage(img);

    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);

    // Step 1: MBR parser detects protective MBR
    Recovery::Partitions::MBRParser mbr;
    auto mbrParts = mbr.Parse(reader, 512);
    Check("MBR returns no normal partitions", mbrParts.empty());
    Check("MBR detects protective MBR", mbr.HasProtectiveMBR());

    // Step 2: Since protective MBR detected, try GPT
    Recovery::Partitions::GPTParser gpt;
    Check("GPT CanParse returns true", gpt.CanParse(reader));
    auto gptParts = gpt.Parse(reader, 512);
    Check("GPT returns 1 partition", gptParts.size() == 1);
    if (!gptParts.empty()) {
        Check("name=Recovery", gptParts[0].gptName == "Recovery");
    }
    storage.Close();
}

static void TestTruncatedGPTEntryArray() {
    std::cout << "\nTest: Truncated GPT entry array\n";
    GPTEntry e1;
    std::memcpy(e1.typeGUID, GUID_MSDATA, 16);
    std::memset(e1.uniqueGUID, 0xEE, 16);
    e1.firstLBA = 34; e1.lastLBA = 99; e1.attributes = 0; e1.name = "Part1";

    auto img = BuildGPT({e1});
    // Truncate the image to cut off most of the entry array.
    // Keep MBR + header + 1 full sector of entries (3 sectors total).
    // ByteReader performs sector-aligned reads, so we need full sectors.
    size_t truncSize = 3 * 512; // 3 sectors: MBR + header + 1 sector of entries
    img.resize(truncSize);
    WriteImage(img);

    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::GPTParser gpt;
    auto parts = gpt.Parse(reader, 512);
    // Should parse at least the first entry, not crash
    Check("At least 1 partition parsed from truncated image", parts.size() >= 1);
    Check("No crash on truncated array", true);
    storage.Close();
}

static void TestPartitionBeyondImage() {
    std::cout << "\nTest: Partition extends beyond image\n";
    // MBR partition claims to be huge but image is small
    auto img = BuildMBR({{0x07, false, 2048, 999999999}});
    // Keep image small - only 4096 bytes
    img.resize(4096, 0);
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    auto parts = mbr.Parse(reader, 512);
    // Parser should still report the entry (it's metadata), just not crash
    Check("Partition entry parsed (metadata only)", parts.size() == 1);
    if (!parts.empty()) {
        Check("sectorCount matches claimed value", parts[0].sectorCount == 999999999);
    }
    Check("No crash on oversized partition", true);
    storage.Close();
}

static void TestOverflowLBA() {
    std::cout << "\nTest: Large LBA values (overflow safety)\n";
    // Use max uint32 values for LBA
    auto img = BuildMBR({{0x83, false, 0xFFFFFFFF, 0xFFFFFFFF}});
    img.resize(4096, 0);
    WriteImage(img);
    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    storage.Open(TEMP_IMG);
    Recovery::Core::ByteReader reader(&storage);
    Recovery::Partitions::MBRParser mbr;
    auto parts = mbr.Parse(reader, 512);
    Check("Entry parsed without crash", parts.size() == 1);
    if (!parts.empty()) {
        uint64_t expectedOffset = 0xFFFFFFFFULL * 512ULL;
        uint64_t expectedSize   = 0xFFFFFFFFULL * 512ULL;
        Check("startOffset uses uint64", parts[0].startOffset == expectedOffset);
        Check("sizeBytes uses uint64", parts[0].sizeBytes == expectedSize);
    }
    storage.Close();
}

// ---- Main ----

int main() {
    std::cout << "=============================================\n";
    std::cout << "  Recovery Module - Phase 2 Test\n";
    std::cout << "  Partition Detection\n";
    std::cout << "=============================================\n";

    TestEmptyImage();
    TestTinyImage();
    TestInvalidMBRSignature();
    TestValidMBRZeroPartitions();
    TestValidMBROnePartition();
    TestValidMBRFourPartitions();
    TestProtectiveMBR();
    TestInvalidGPTSignature();
    TestValidGPTOnePartition();
    TestValidGPTMultiplePartitions();
    TestGPTEmptyEntries();
    TestProtectiveMBRToGPT();
    TestTruncatedGPTEntryArray();
    TestPartitionBeyondImage();
    TestOverflowLBA();

    // Cleanup temp file
    std::remove(TEMP_IMG);

    std::cout << "\n=============================================\n";
    std::cout << "  Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "=============================================\n";

    return g_failed > 0 ? 1 : 0;
}
