#pragma once
#include <cstdint>
#include <string>
namespace Recovery { namespace TSK {
struct TskMetadataScanStatus {
    bool metadataWalkCompleted=false, directoryWalkCompleted=false;
    uint64_t metadataEntriesSeen=0, directoryEntriesSeen=0;
    uint64_t recordsProduced=0, recordsMerged=0, recordsSkipped=0;
    std::string lastError;
};
}}
