#include "utils/digest_verifier.h"

#include "checkpoint_manifest.h"

#include <openssl/evp.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace puzzle71::utils {

namespace {

std::string ToHex(const unsigned char* data, std::size_t length) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out(length * 2, '\0');
    for (std::size_t i = 0; i < length; ++i) {
        out[2 * i] = kHexDigits[(data[i] >> 4) & 0x0F];
        out[2 * i + 1] = kHexDigits[data[i] & 0x0F];
    }
    return out;
}

std::optional<std::string> ComputeSha256(const std::filesystem::path& payload_path) {
    std::ifstream ifs(payload_path, std::ios::binary);
    if (!ifs) {
        return std::nullopt;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return std::nullopt;
    }

    const EVP_MD* md = EVP_sha256();
    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::nullopt;
    }

    std::vector<char> buffer(4096);
    while (ifs) {
        ifs.read(buffer.data(), buffer.size());
        std::streamsize read = ifs.gcount();
        if (read <= 0) {
            break;
        }
        if (EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(read)) != 1) {
            EVP_MD_CTX_free(ctx);
            return std::nullopt;
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::nullopt;
    }

    EVP_MD_CTX_free(ctx);
    return ToHex(digest, digest_len);
}

}  // namespace

DigestVerificationResult VerifyManifestDigest(std::string_view manifest_path,
                                              std::string_view payload_path) {
    DigestVerificationResult result{};

    std::filesystem::path manifest_file{manifest_path};
    if (!std::filesystem::exists(manifest_file)) {
        result.status = DigestStatus::kIoError;
        result.message = "Manifest file missing";
        return result;
    }

    auto manifest = checkpoint::LoadManifestFromFile(manifest_file);
    if (!manifest) {
        result.status = DigestStatus::kParseError;
        result.message = "Unable to parse manifest";
        return result;
    }

    std::filesystem::path payload = payload_path.empty()
                                        ? std::filesystem::path(manifest->path)
                                        : std::filesystem::path(payload_path);
    if (payload.empty() || !std::filesystem::exists(payload)) {
        result.status = DigestStatus::kIoError;
        result.message = "Payload file missing";
        return result;
    }

    auto digest = ComputeSha256(payload);
    if (!digest) {
        result.status = DigestStatus::kIoError;
        result.message = "Failed to compute SHA-256";
        return result;
    }

    if (manifest->payload_sha256.empty()) {
        result.status = DigestStatus::kMismatch;
        result.message = "Manifest does not contain payload_sha256";
        return result;
    }

    if (*digest != manifest->payload_sha256) {
        result.status = DigestStatus::kMismatch;
        result.message = "SHA-256 mismatch";
        return result;
    }

    result.status = DigestStatus::kOk;
    result.message = "Digest verified";
    return result;
}

}  // namespace puzzle71::utils
