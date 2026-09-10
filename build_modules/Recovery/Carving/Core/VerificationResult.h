#pragma once

#include "VerificationCheck.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

enum class VerificationClassification {
    VALID,
    PARTIAL,
    CORRUPTED,
    INVALID,
    UNKNOWN
};
using Classification = VerificationClassification;

struct VerificationResult {
    VerificationClassification classification = VerificationClassification::UNKNOWN;
    std::string detectedType;
    std::string path;
    uint64_t fileSize = 0;
    std::string sha256;
    std::string explanation;
    std::vector<VerificationCheck> checks;
    double score = 0.0; // Percentage of applicable weighted checks which passed.

    void AddCheck(const VerificationCheck& check) {
        checks.push_back(check);
        RecalculateScore();
    }

    void RecalculateScore() {
        double total = 0.0;
        double passed = 0.0;
        for (const auto& check : checks) {
            if (check.status == CheckStatus::NOT_APPLICABLE ||
                check.status == CheckStatus::NOT_PERFORMED) {
                continue;
            }
            const double weight = check.weight > 0.0 ? check.weight : 1.0;
            total += weight;
            if (check.status == CheckStatus::PASS) {
                passed += weight;
            }
        }
        score = total == 0.0 ? 0.0 : (passed * 100.0 / total);
        score = std::max(0.0, std::min(100.0, score));
    }

    bool IsValid() const {
        return classification == VerificationClassification::VALID;
    }
};

inline const char* ToString(CheckStatus status) {
    switch (status) {
    case CheckStatus::PASS: return "PASS";
    case CheckStatus::FAIL: return "FAIL";
    case CheckStatus::NOT_PERFORMED: return "NOT_PERFORMED";
    case CheckStatus::NOT_APPLICABLE: return "NOT_APPLICABLE";
    }
    return "NOT_PERFORMED";
}

inline const char* ToString(VerificationClassification classification) {
    switch (classification) {
    case VerificationClassification::VALID: return "VALID";
    case VerificationClassification::PARTIAL: return "PARTIAL";
    case VerificationClassification::CORRUPTED: return "CORRUPTED";
    case VerificationClassification::INVALID: return "INVALID";
    case VerificationClassification::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

} // namespace Carving
} // namespace Recovery
