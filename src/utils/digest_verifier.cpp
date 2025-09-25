#include "utils/digest_verifier.h"

#include <filesystem>
#include <fstream>

namespace puzzle71::utils {

DigestVerificationResult VerifyManifestDigest(std::string_view manifest_path,
                                              std::string_view /*payload_path*/) {
    DigestVerificationResult result{};

    if (!std::filesystem::exists(manifest_path)) {
        result.status = DigestStatus::kIoError;
        result.message = "Manifest file missing";
        return result;
    }

    // TODO(T031): Parse manifest, compute SHA-256 digest of payload, and compare against stored value.
    result.status = DigestStatus::kNotImplemented;
    result.message = "Digest verification not implemented";
    return result;
}

}  // namespace puzzle71::utils
