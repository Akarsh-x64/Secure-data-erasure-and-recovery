#include "StatisticalTests.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Erasure {
namespace Verification {

// =============================================================================
// Self-Contained FIPS 180-4 SHA-256 Implementation
// =============================================================================
namespace {
    inline uint32_t RoTr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }
    inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }
    inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }
    inline uint32_t Sig0(uint32_t x) {
        return RoTr(x, 2) ^ RoTr(x, 13) ^ RoTr(x, 22);
    }
    inline uint32_t Sig1(uint32_t x) {
        return RoTr(x, 6) ^ RoTr(x, 11) ^ RoTr(x, 25);
    }
    inline uint32_t sig0(uint32_t x) {
        return RoTr(x, 7) ^ RoTr(x, 18) ^ (x >> 3);
    }
    inline uint32_t sig1(uint32_t x) {
        return RoTr(x, 17) ^ RoTr(x, 19) ^ (x >> 10);
    }

    const uint32_t K256[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    struct Sha256Ctx {
        uint32_t state[8];
        uint64_t bitCount;
        uint8_t  buffer[64];
    };

    void Sha256Init(Sha256Ctx* ctx) {
        ctx->state[0] = 0x6a09e667;
        ctx->state[1] = 0xbb67ae85;
        ctx->state[2] = 0x3c6ef372;
        ctx->state[3] = 0xa54ff53a;
        ctx->state[4] = 0x510e527f;
        ctx->state[5] = 0x9b05688c;
        ctx->state[6] = 0x1f83d9ab;
        ctx->state[7] = 0x5be0cd19;
        ctx->bitCount = 0;
    }

    void Sha256Transform(Sha256Ctx* ctx, const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = sig1(w[i - 2]) + w[i - 7] + sig0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
        uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + Sig1(e) + Ch(e, f, g) + K256[i] + w[i];
            uint32_t t2 = Sig0(a) + Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        ctx->state[0] += a;
        ctx->state[1] += b;
        ctx->state[2] += c;
        ctx->state[3] += d;
        ctx->state[4] += e;
        ctx->state[5] += f;
        ctx->state[6] += g;
        ctx->state[7] += h;
    }

    void Sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
        size_t bufIndex = static_cast<size_t>((ctx->bitCount / 8) % 64);
        ctx->bitCount += static_cast<uint64_t>(len) * 8;

        size_t dataPos = 0;
        if (bufIndex > 0) {
            size_t needed = 64 - bufIndex;
            if (len < needed) {
                std::memcpy(ctx->buffer + bufIndex, data, len);
                return;
            }
            std::memcpy(ctx->buffer + bufIndex, data, needed);
            Sha256Transform(ctx, ctx->buffer);
            dataPos += needed;
        }

        while (dataPos + 64 <= len) {
            Sha256Transform(ctx, data + dataPos);
            dataPos += 64;
        }

        if (dataPos < len) {
            std::memcpy(ctx->buffer, data + dataPos, len - dataPos);
        }
    }

    void Sha256Final(Sha256Ctx* ctx, uint8_t digest[32]) {
        size_t bufIndex = static_cast<size_t>((ctx->bitCount / 8) % 64);
        ctx->buffer[bufIndex++] = 0x80;

        if (bufIndex > 56) {
            std::memset(ctx->buffer + bufIndex, 0, 64 - bufIndex);
            Sha256Transform(ctx, ctx->buffer);
            bufIndex = 0;
        }
        std::memset(ctx->buffer + bufIndex, 0, 56 - bufIndex);

        uint64_t totalBits = ctx->bitCount;
        for (int i = 7; i >= 0; --i) {
            ctx->buffer[56 + (7 - i)] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);
        }
        Sha256Transform(ctx, ctx->buffer);

        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>((ctx->state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((ctx->state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((ctx->state[i] >> 8)  & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>((ctx->state[i])       & 0xFF);
        }
    }

    // Normal Cumulative Distribution Function approximation for p-value
    double NormalCdf(double z) {
        return 0.5 * std::erfc(-z / std::sqrt(2.0));
    }
} // anonymous namespace

// =============================================================================
// Cryptographic Hashing
// =============================================================================

std::string StatisticalTests::ComputeSha256(const uint8_t* buffer, size_t size) {
    if (!buffer || size == 0) {
        return "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"; // Empty hash
    }

    Sha256Ctx ctx;
    Sha256Init(&ctx);
    Sha256Update(&ctx, buffer, size);

    uint8_t digest[32];
    Sha256Final(&ctx, digest);

    std::ostringstream oss;
    for (int i = 0; i < 32; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return oss.str();
}

// =============================================================================
// Mathematical & Statistical Metrics
// =============================================================================

std::vector<uint64_t> StatisticalTests::ComputeHistogram(const uint8_t* buffer, size_t size) {
    std::vector<uint64_t> hist(256, 0);
    if (!buffer || size == 0) return hist;

    for (size_t i = 0; i < size; ++i) {
        hist[buffer[i]]++;
    }
    return hist;
}

double StatisticalTests::CalculateShannonEntropy(const uint8_t* buffer, size_t size) {
    if (!buffer || size == 0) return 0.0;

    std::vector<uint64_t> hist = ComputeHistogram(buffer, size);
    double entropy = 0.0;
    double dSize = static_cast<double>(size);

    for (int i = 0; i < 256; ++i) {
        if (hist[i] > 0) {
            double p = static_cast<double>(hist[i]) / dSize;
            entropy -= p * (std::log(p) / std::log(2.0)); // log2(p)
        }
    }
    return entropy;
}

double StatisticalTests::CalculateChiSquare(const uint8_t* buffer, size_t size, double& outPValue) {
    if (!buffer || size == 0) {
        outPValue = 0.0;
        return 0.0;
    }

    std::vector<uint64_t> hist = ComputeHistogram(buffer, size);
    double expected = static_cast<double>(size) / 256.0;
    double chiSquare = 0.0;

    for (int i = 0; i < 256; ++i) {
        double diff = static_cast<double>(hist[i]) - expected;
        chiSquare += (diff * diff) / expected;
    }

    // Wilson-Hilferty transformation for chi-square with degrees of freedom df = 255
    double df = 255.0;
    double term1 = std::pow(chiSquare / df, 1.0 / 3.0);
    double term2 = 1.0 - (2.0 / (9.0 * df));
    double denom = std::sqrt(2.0 / (9.0 * df));
    double z = (term1 - term2) / denom;

    // Two-tailed p-value for uniformity hypothesis
    double cdfVal = NormalCdf(z);
    outPValue = 2.0 * (1.0 - std::max(cdfVal, 1.0 - cdfVal));
    if (outPValue < 0.0) outPValue = 0.0;
    if (outPValue > 1.0) outPValue = 1.0;

    return chiSquare;
}

double StatisticalTests::CalculateSerialCorrelation(const uint8_t* buffer, size_t size) {
    if (!buffer || size < 2) return 0.0;

    double sumX = 0.0;
    double sumY = 0.0;
    double sumX2 = 0.0;
    double sumY2 = 0.0;
    double sumXY = 0.0;
    size_t n = size - 1;

    for (size_t i = 0; i < n; ++i) {
        double x = buffer[i];
        double y = buffer[i + 1];
        sumX += x;
        sumY += y;
        sumX2 += x * x;
        sumY2 += y * y;
        sumXY += x * y;
    }

    double dn = static_cast<double>(n);
    double numerator = (dn * sumXY) - (sumX * sumY);
    double denomX = (dn * sumX2) - (sumX * sumX);
    double denomY = (dn * sumY2) - (sumY * sumY);

    if (denomX <= 0.0 || denomY <= 0.0) return 0.0;
    return numerator / std::sqrt(denomX * denomY);
}

double StatisticalTests::EstimateMonteCarloPi(const uint8_t* buffer, size_t size, double& outErrorPercent) {
    if (!buffer || size < 4) {
        outErrorPercent = 100.0;
        return 0.0;
    }

    size_t pairs = size / 4; // Each pair uses 2 bytes for X (0..65535) and 2 bytes for Y (0..65535)
    if (pairs == 0) {
        outErrorPercent = 100.0;
        return 0.0;
    }

    uint64_t hits = 0;
    for (size_t i = 0; i < pairs; ++i) {
        uint16_t xRaw = static_cast<uint16_t>(buffer[i * 4 + 0]) | (static_cast<uint16_t>(buffer[i * 4 + 1]) << 8);
        uint16_t yRaw = static_cast<uint16_t>(buffer[i * 4 + 2]) | (static_cast<uint16_t>(buffer[i * 4 + 3]) << 8);

        double x = static_cast<double>(xRaw) / 65535.0;
        double y = static_cast<double>(yRaw) / 65535.0;

        if ((x * x + y * y) <= 1.0) {
            hits++;
        }
    }

    double piEstimate = 4.0 * (static_cast<double>(hits) / static_cast<double>(pairs));
    const double REAL_PI = 3.14159265358979323846;
    outErrorPercent = (std::abs(piEstimate - REAL_PI) / REAL_PI) * 100.0;

    return piEstimate;
}

StatisticalAuditResult StatisticalTests::RunFullAudit(const uint8_t* buffer, size_t size) {
    StatisticalAuditResult res;
    res.totalBytesAnalyzed = size;
    if (!buffer || size == 0) return res;

    res.byteHistogram = ComputeHistogram(buffer, size);
    res.shannonEntropy = CalculateShannonEntropy(buffer, size);
    res.chiSquareValue = CalculateChiSquare(buffer, size, res.chiSquarePValue);
    res.serialCorrelation = CalculateSerialCorrelation(buffer, size);
    res.monteCarloPi = EstimateMonteCarloPi(buffer, size, res.monteCarloPiErrorPercent);

    return res;
}

// =============================================================================
// ASCII Graph & Visual Renderers
// =============================================================================

std::string StatisticalTests::RenderAsciiGauge(double value, double maxVal, int width, const std::string& unit) {
    if (maxVal <= 0.0) maxVal = 1.0;
    double ratio = std::clamp(value / maxVal, 0.0, 1.0);
    int filled = static_cast<int>(std::round(ratio * width));

    std::ostringstream oss;
    oss << "[";
    for (int i = 0; i < width; ++i) {
        if (i < filled) oss << "=";
        else oss << " ";
    }
    oss << "] " << std::fixed << std::setprecision(4) << value << " / "
        << std::setprecision(4) << maxVal << " " << unit;
    return oss.str();
}

std::string StatisticalTests::RenderConfidenceGauge(double confidencePercent, int width) {
    double ratio = std::clamp(confidencePercent / 100.0, 0.0, 1.0);
    int filled = static_cast<int>(std::round(ratio * width));

    std::ostringstream oss;
    oss << "[";
    for (int i = 0; i < width; ++i) {
        if (i < filled) oss << "#";
        else oss << ".";
    }
    oss << "] " << std::fixed << std::setprecision(3) << confidencePercent << "% CONFIDENCE";
    return oss.str();
}

std::string StatisticalTests::RenderByteDistributionHistogram(const std::vector<uint64_t>& hist256, int width, int height) {
    if (hist256.size() < 256) return "No histogram data available.\n";

    // Aggregate 256 bins into `width` columns (e.g., 16 or 32 columns)
    if (width <= 0) width = 16;
    if (height <= 0) height = 6;

    int binsPerCol = 256 / width;
    if (binsPerCol == 0) binsPerCol = 1;

    std::vector<double> colAverages(width, 0.0);
    double maxAvg = 0.0;

    for (int c = 0; c < width; ++c) {
        double sum = 0.0;
        int count = 0;
        for (int b = c * binsPerCol; b < (c + 1) * binsPerCol && b < 256; ++b) {
            sum += static_cast<double>(hist256[b]);
            count++;
        }
        colAverages[c] = (count > 0) ? (sum / count) : 0.0;
        if (colAverages[c] > maxAvg) maxAvg = colAverages[c];
    }

    if (maxAvg <= 0.0) maxAvg = 1.0;

    std::ostringstream oss;
    oss << "  Frequency Distribution across 256 Byte Values (0x00 .. 0xFF):\n";
    oss << "  Max: " << static_cast<uint64_t>(maxAvg) << " occurrences\n";

    for (int row = height; row >= 1; --row) {
        double threshold = (static_cast<double>(row) / height) * maxAvg;
        oss << "  │ ";
        for (int c = 0; c < width; ++c) {
            if (colAverages[c] >= threshold) {
                oss << "█";
            } else if (colAverages[c] >= threshold - (maxAvg / (height * 2.0))) {
                oss << "▄";
            } else {
                oss << " ";
            }
        }
        oss << "\n";
    }

    oss << "  └──";
    for (int c = 0; c < width; ++c) oss << "─";
    oss << "\n      0x00";
    for (int c = 0; c < width - 10; ++c) oss << " ";
    oss << "0xFF\n";

    return oss.str();
}

} // namespace Verification
} // namespace Erasure
