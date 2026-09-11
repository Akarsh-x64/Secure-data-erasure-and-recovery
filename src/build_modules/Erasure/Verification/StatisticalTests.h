#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace Erasure {
namespace Verification {

/**
 * @brief Results from statistical uniformity and randomness auditing.
 */
struct StatisticalAuditResult {
    double shannonEntropy = 0.0;           // Bits per byte (0.0 to 8.0)
    double chiSquareValue = 0.0;           // Chi-square sum across 256 byte bins
    double chiSquarePValue = 0.0;          // Statistical p-value (null hypothesis: uniform)
    double serialCorrelation = 0.0;        // Correlation between adjacent bytes (-1.0 to 1.0)
    double monteCarloPi = 0.0;             // Pi approximation
    double monteCarloPiErrorPercent = 0.0; // Percentage error from real Pi (3.14159...)
    uint64_t totalBytesAnalyzed = 0;
    std::vector<uint64_t> byteHistogram;   // 256 frequency bins
};

/**
 * @brief High-performance statistical test suite for forensic data verification.
 */
class StatisticalTests {
public:
    // Core Mathematical Metrics
    static double CalculateShannonEntropy(const uint8_t* buffer, size_t size);
    static double CalculateChiSquare(const uint8_t* buffer, size_t size, double& outPValue);
    static double CalculateSerialCorrelation(const uint8_t* buffer, size_t size);
    static double EstimateMonteCarloPi(const uint8_t* buffer, size_t size, double& outErrorPercent);
    static std::vector<uint64_t> ComputeHistogram(const uint8_t* buffer, size_t size);

    // Full comprehensive audit
    static StatisticalAuditResult RunFullAudit(const uint8_t* buffer, size_t size);

    // Cryptographic Hashing (FIPS 180-4 SHA-256, portable zero-dependency)
    static std::string ComputeSha256(const uint8_t* buffer, size_t size);

    // ASCII Graph & Visual Renderers
    static std::string RenderAsciiGauge(double value, double maxVal, int width, const std::string& unit);
    static std::string RenderByteDistributionHistogram(const std::vector<uint64_t>& hist256, int width = 32, int height = 8);
    static std::string RenderConfidenceGauge(double confidencePercent, int width = 30);
};

} // namespace Verification
} // namespace Erasure
