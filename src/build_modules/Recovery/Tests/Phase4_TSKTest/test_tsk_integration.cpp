#include "../../Acquisition/LinuxReadOnlyStorage.h"
#include "../../Core/StorageRegion.h"
#include "../../TSK/TskFileSystem.h"

#include <iostream>
#include <vector>
#include <string>

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

int main(int argc, char** argv) {
    std::cout << "=============================================\n";
    std::cout << "  Recovery Module - Phase 4 Test\n";
    std::cout << "  TSK/libtsk Integration\n";
    std::cout << "=============================================\n\n";

    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <image_path> <partition_offset_bytes> <partition_size_bytes>\n";
        return 2;
    }

    const std::string imagePath = argv[1];
    const uint64_t partitionOffset = std::stoull(argv[2]);
    const uint64_t partitionSize = std::stoull(argv[3]);

    Recovery::Acquisition::LinuxReadOnlyStorage storage;
    Check("Open image read-only", storage.Open(imagePath));
    if (g_failed > 0) {
        return 1;
    }

    Recovery::Core::StorageRegion region(partitionOffset, partitionSize);
    Recovery::TSK::TskFileSystem fs(storage, region);

    Check("libtsk available at compile-time", fs.IsTskAvailable());

    const std::string version = fs.GetTskVersion();
    Check("TSK version string is not empty", !version.empty());
    std::cout << "  TSK Version: " << version << "\n";

    if (!fs.IsTskAvailable()) {
        Check("Unavailable TSK backend reports a clear error", !fs.Mount());
        Check("Unavailable TSK backend exposes an error",
              fs.GetLastError().find("not available") != std::string::npos);
        storage.Close();
        return g_failed > 0 ? 1 : 0;
    }

    Check("Mount partition via TSK", fs.Mount());
    if (g_failed > 0) {
        storage.Close();
        return 1;
    }

    auto fsType = fs.GetType();
    Check("Detected filesystem is not Unknown", fsType != Recovery::Core::FileSystemType::Unknown);

    std::vector<Recovery::Core::FileRecord> files;
    Check("Enumerate files", fs.EnumerateFiles(files));
    Check("At least one file discovered", !files.empty());

    const auto& metadataStore = fs.GetMetadataStore();
    Check("Metadata store mirrors enumerated records",
            metadataStore.All().size() == files.size());

    bool sawDeleted = false;
    bool sawAnyRanges = false;
    bool sawOffsetInsidePartition = false;
    bool rangeStatusConsistent = true;
    bool metadataRoundTrip = true;

    for (const auto& rec : files) {
        Recovery::Core::FileRecord metadata;
        if (!fs.GetFileMetadata(rec.id, metadata) || metadata.filesystemRecordId != rec.filesystemRecordId) {
            metadataRoundTrip = false;
        }

        if (rec.deleted) {
            sawDeleted = true;
        }

        for (const auto& r : rec.dataRanges) {
            sawAnyRanges = true;
            if (rec.dataRangeStatus != Recovery::Core::DataRangeStatus::Complete) {
                rangeStatusConsistent = false;
            }
            if (r.offset == 0) {
                continue;
            }
            if (r.offset >= partitionOffset && r.offset < (partitionOffset + partitionSize)) {
                sawOffsetInsidePartition = true;
            }
        }
    }

    Check("Metadata lookup round-trips TSK record IDs", metadataRoundTrip);

    Check("Data ranges surfaced where supported", sawAnyRanges);
    Check("Data range status is explicit for surfaced ranges",
          rangeStatusConsistent);
    Check("Non-sparse data ranges include absolute offsets in partition", sawOffsetInsidePartition);

    if (sawDeleted) {
        Check("Deleted file metadata discovered", true);

        bool readDeleted = false;
        for (const auto& rec : files) {
            if (!rec.deleted || rec.size == 0) {
                continue;
            }

            std::vector<uint8_t> content;
            if (fs.ReadFile(rec.id, content) && !content.empty()) {
                readDeleted = true;
                break;
            }
        }
        Check("Read deleted file through metadata record", readDeleted);
    } else {
        std::cout << "  [INFO] No deleted entries surfaced in this test image.\n";
    }

    // Read one regular file (if available) to verify read path.
    bool readAny = false;
    for (const auto& rec : files) {
        if (rec.size == 0) {
            continue;
        }

        std::vector<uint8_t> content;
        if (fs.ReadFile(rec.id, content)) {
            readAny = true;
            Check("ReadFile via TSK-backed adapter", true);
            break;
        }
    }

    if (!readAny) {
        std::cout << "  [INFO] No readable non-empty file found in this image.\n";
    }

    storage.Close();

    std::cout << "\n=============================================\n";
    std::cout << "  Results: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "=============================================\n";

    return g_failed > 0 ? 1 : 0;
}
