#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace Erasure {
namespace Verification {

enum class SignatureCategory {
    DOCUMENT,
    IMAGE,
    ARCHIVE,
    EXECUTABLE,
    AUDIO_VIDEO,
    DATABASE_SYSTEM
};

struct MagicSignature {
    std::string name;
    std::string extension;
    SignatureCategory category;
    std::vector<uint8_t> headerBytes;
    uint32_t headerOffset = 0; // Usually 0 within a cluster or sector
};

struct CarvedArtifact {
    std::string signatureName;
    std::string extension;
    SignatureCategory category;
    uint64_t byteOffset = 0;
    uint64_t lba = 0;
};

class SignatureCarver {
private:
    std::vector<MagicSignature> m_signatures;
    void InitializeSignatures();

public:
    SignatureCarver();

    /**
     * @brief Scans a memory buffer for all known magic file signatures.
     * Checks sector boundaries (512B) and cluster boundaries (4096B) as well as arbitrary alignments.
     *
     * @param buffer Raw sector bytes to scan
     * @param size Size in bytes
     * @param basePhysicalOffset Base disk byte offset
     * @param sectorSize Sector size for LBA mapping (e.g. 512)
     * @return List of detected file headers (target is 0 for sanitized sectors)
     */
    std::vector<CarvedArtifact> ScanBuffer(const uint8_t* buffer, size_t size, uint64_t basePhysicalOffset, uint32_t sectorSize = 512) const;

    size_t GetSignatureCount() const { return m_signatures.size(); }
    const std::vector<MagicSignature>& GetSignatures() const { return m_signatures; }
};

} // namespace Verification
} // namespace Erasure
