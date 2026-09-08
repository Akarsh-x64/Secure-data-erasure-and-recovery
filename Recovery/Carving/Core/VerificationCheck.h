#pragma once

#include <string>
#include <utility>

namespace Recovery {
namespace Carving {

enum class CheckStatus {
    PASS,
    FAIL,
    NOT_PERFORMED,
    NOT_APPLICABLE
};
using VerificationStatus = CheckStatus;

/** A named, weighted observation.  Keeping observations separate makes the score auditable. */
struct VerificationCheck {
    std::string name;
    CheckStatus status = CheckStatus::NOT_PERFORMED;
    std::string explanation;
    double weight = 1.0;

    VerificationCheck() = default;
    VerificationCheck(std::string checkName, CheckStatus checkStatus,
                      std::string reason = {}, double checkWeight = 1.0)
        : name(std::move(checkName)), status(checkStatus),
          explanation(std::move(reason)), weight(checkWeight) {}

    // "message" is a convenient spelling for clients displaying a check.
    const std::string& message() const { return explanation; }
};

} // namespace Carving
} // namespace Recovery
