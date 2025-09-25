#pragma once

#include <string>
#include <string_view>

namespace puzzle71::utils {

enum class DigestStatus {
    kOk,
    kMismatch,
    kIoError,
    kNotImplemented,
};

struct DigestVerificationResult {
    DigestStatus status{DigestStatus::kNotImplemented};
    std::string message;
};

DigestVerificationResult VerifyManifestDigest(std::string_view manifest_path,
                                              std::string_view payload_path);

}  // namespace puzzle71::utils
