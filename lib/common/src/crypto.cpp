// ============================================================================
//  Acadia Instrumentation
//
//  Copyright (C) 2026 Soccentric. All rights reserved.
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  This file is part of a proprietary product of Soccentric.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file lib/common/src/crypto.cpp
 * @brief Cryptographic primitives on OpenSSL libcrypto: SHA-256, HMAC-SHA256,
 *        X25519, ChaCha20, ChaCha20-Poly1305 AEAD and HKDF-SHA256 (REQ §6.2, §9,
 *        §10). T-P31 (INS-001..003): the former hand-rolled implementations are
 *        gone; the API is unchanged and still tested against published vectors.
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include "instr/common/crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>

#include <algorithm>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

namespace instr
{
/// @brief Self-contained cryptographic primitives (hashes, AEAD, X25519) and signature verification.
namespace crypto
{

/// @brief Render the digest as 64 lowercase hex characters.
/// @return 64 lowercase hex characters.
std::string Digest32::hex() const
{
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (uint8_t x : b)
    {
        s += d[x >> 4];
        s += d[x & 0xF];
    }
    return s;
}

// ---------------- OpenSSL-backed primitives (T-P31) ----------------

namespace
{
/// Owning EVP_CIPHER_CTX / EVP_PKEY / EVP_PKEY_CTX handles.
struct CipherCtx
{
    EVP_CIPHER_CTX* p = EVP_CIPHER_CTX_new();
    ~CipherCtx()
    {
        EVP_CIPHER_CTX_free(p);
    }
};
struct Pkey
{
    EVP_PKEY* p = nullptr;
    ~Pkey()
    {
        EVP_PKEY_free(p);
    }
};
struct PkeyCtx
{
    EVP_PKEY_CTX* p = nullptr;
    ~PkeyCtx()
    {
        EVP_PKEY_CTX_free(p);
    }
};

const uint8_t* bytes(const std::string& s)
{
    return reinterpret_cast<const uint8_t*>(s.data());
}

/// AEAD format v2: 0x02 | nonce(12) | ciphertext | Poly1305 tag(16).
constexpr uint8_t kAeadV2 = 0x02;
constexpr size_t kNonce = 12;
constexpr size_t kTag = 16;
} // namespace

Digest32 sha256(const uint8_t* data, size_t len)
{
    Digest32 d;
    unsigned int n = 0;
    if (EVP_Digest(data, len, d.b.data(), &n, EVP_sha256(), nullptr) != 1 || n != 32) throw std::runtime_error("EVP_Digest(sha256) failed");
    return d;
}

Digest32 hmac_sha256(const std::string& key, const std::string& msg)
{
    Digest32 d;
    unsigned int n = 0;
    if (!HMAC(EVP_sha256(), key.data(), int(key.size()), bytes(msg), msg.size(), d.b.data(), &n) || n != 32) throw std::runtime_error("HMAC-SHA256 failed");
    return d;
}

std::array<uint8_t, 32> x25519(const uint8_t priv_in[32], const uint8_t pub_in[32])
{
    std::array<uint8_t, 32> out{};
    Pkey priv, peer;
    priv.p = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv_in, 32);
    peer.p = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, pub_in, 32);
    if (!priv.p || !peer.p) return out;
    PkeyCtx ctx;
    ctx.p = EVP_PKEY_CTX_new(priv.p, nullptr);
    size_t n = out.size();
    // A low-order peer point makes the derive fail: the all-zero output is
    // returned, exactly as RFC 7748 §6.1 callers must check for.
    if (!ctx.p || EVP_PKEY_derive_init(ctx.p) != 1 || EVP_PKEY_derive_set_peer(ctx.p, peer.p) != 1 || EVP_PKEY_derive(ctx.p, out.data(), &n) != 1) out.fill(0);
    return out;
}

X25519Key x25519_generate(const Rng32& rng)
{
    X25519Key k;
    rng(k.priv.data(), k.priv.size());
    Pkey key;
    key.p = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, k.priv.data(), 32);
    size_t n = k.pub.size();
    if (!key.p || EVP_PKEY_get_raw_public_key(key.p, k.pub.data(), &n) != 1) throw std::runtime_error("X25519 public key derivation failed");
    return k;
}

void chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t* in, uint8_t* out, size_t len)
{
    // OpenSSL's ChaCha20 IV is the RFC 8439 block counter (LE) || nonce.
    uint8_t iv[16] = {uint8_t(counter), uint8_t(counter >> 8), uint8_t(counter >> 16), uint8_t(counter >> 24)};
    std::memcpy(iv + 4, nonce, 12);
    CipherCtx c;
    int n = 0;
    if (!c.p || EVP_EncryptInit_ex(c.p, EVP_chacha20(), nullptr, key, iv) != 1) throw std::runtime_error("ChaCha20 init failed");
    size_t off = 0;
    while (off < len)
    {
        const int chunk = int(std::min<size_t>(len - off, 1u << 30));
        if (EVP_EncryptUpdate(c.p, out + off, &n, in + off, chunk) != 1) throw std::runtime_error("ChaCha20 failed");
        off += size_t(chunk);
    }
}

Result<std::string> aead_seal_checked(const std::string& key32, const std::string& plaintext)
{
    // INS-004: a CSPRNG failure fails the seal — never a zero/repeated nonce.
    uint8_t nonce[kNonce];
    Error e = random_bytes(nonce, kNonce);
    if (!e.ok()) return Result<std::string>::fail(e);
    return Result<std::string>::good(aead_seal(key32, plaintext, [&](uint8_t* out, size_t n) { std::memcpy(out, nonce, std::min(n, kNonce)); }));
}

std::string aead_seal(const std::string& key32, const std::string& plaintext, const Rng32& rng)
{
    if (key32.size() != 32) throw std::invalid_argument("aead_seal: key must be 32 bytes");
    // INS-002: standard ChaCha20-Poly1305 (RFC 8439) — one key, AEAD-internal
    // key separation — replaces the custom ChaCha20 + HMAC(same key) scheme.
    uint8_t nonce[kNonce];
    rng(nonce, kNonce);
    std::string out(1 + kNonce + plaintext.size() + kTag, '\0');
    out[0] = char(kAeadV2);
    std::memcpy(&out[1], nonce, kNonce);
    CipherCtx c;
    int n = 0;
    auto* ct = reinterpret_cast<uint8_t*>(&out[1 + kNonce]);
    if (!c.p || EVP_EncryptInit_ex(c.p, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(c.p, EVP_CTRL_AEAD_SET_IVLEN, int(kNonce), nullptr) != 1 || EVP_EncryptInit_ex(c.p, nullptr, nullptr, bytes(key32), nonce) != 1) throw std::runtime_error("AEAD init failed");
    // The version byte is authenticated as associated data.
    if (EVP_EncryptUpdate(c.p, nullptr, &n, &kAeadV2, 1) != 1) throw std::runtime_error("AEAD aad failed");
    if (!plaintext.empty() && EVP_EncryptUpdate(c.p, ct, &n, bytes(plaintext), int(plaintext.size())) != 1) throw std::runtime_error("AEAD encrypt failed");
    if (EVP_EncryptFinal_ex(c.p, ct + plaintext.size(), &n) != 1 || EVP_CIPHER_CTX_ctrl(c.p, EVP_CTRL_AEAD_GET_TAG, int(kTag), ct + plaintext.size()) != 1) throw std::runtime_error("AEAD finalise failed");
    return out;
}

namespace
{
Result<std::string> open_v2(const std::string& key32, const std::string& sealed)
{
    const size_t ct_len = sealed.size() - 1 - kNonce - kTag;
    const auto* nonce = bytes(sealed) + 1;
    const auto* ct = nonce + kNonce;
    std::string tag = sealed.substr(sealed.size() - kTag);
    std::string pt(ct_len, '\0');
    CipherCtx c;
    int n = 0;
    if (!c.p || EVP_DecryptInit_ex(c.p, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(c.p, EVP_CTRL_AEAD_SET_IVLEN, int(kNonce), nullptr) != 1 || EVP_DecryptInit_ex(c.p, nullptr, nullptr, bytes(key32), nonce) != 1 ||
        EVP_DecryptUpdate(c.p, nullptr, &n, &kAeadV2, 1) != 1)
        return Result<std::string>::fail(Errc::Internal, "AEAD init failed", "aead_open");
    if (ct_len && EVP_DecryptUpdate(c.p, reinterpret_cast<uint8_t*>(&pt[0]), &n, ct, int(ct_len)) != 1) return Result<std::string>::fail(Errc::SignatureInvalid, "decrypt failed", "aead_open");
    if (EVP_CIPHER_CTX_ctrl(c.p, EVP_CTRL_AEAD_SET_TAG, int(kTag), &tag[0]) != 1 || EVP_DecryptFinal_ex(c.p, reinterpret_cast<uint8_t*>(&pt[0]) + ct_len, &n) != 1) return Result<std::string>::fail(Errc::SignatureInvalid, "authentication tag mismatch", "aead_open");
    return Result<std::string>::good(pt);
}

/// Pre-T-P31 format (nonce | ChaCha20 ct | HMAC-SHA256(key, nonce|ct)), kept
/// read-only so archives sealed before the migration still open. The tag
/// comparison is constant-time now (INS-003).
Result<std::string> open_legacy_v1(const std::string& key32, const std::string& sealed)
{
    if (sealed.size() < 12 + 32) return Result<std::string>::fail(Errc::SignatureInvalid, "sealed blob too short", "aead_open");
    std::string nonce_s = sealed.substr(0, 12);
    std::string ct = sealed.substr(12, sealed.size() - 44);
    std::string tag = sealed.substr(sealed.size() - 32, 32);
    Digest32 expect = hmac_sha256(key32, nonce_s + ct);
    if (CRYPTO_memcmp(expect.b.data(), tag.data(), 32) != 0) return Result<std::string>::fail(Errc::SignatureInvalid, "authentication tag mismatch", "aead_open");
    std::string pt(ct.size(), '\0');
    if (!ct.empty()) chacha20(bytes(key32), bytes(nonce_s), 1, bytes(ct), reinterpret_cast<uint8_t*>(&pt[0]), ct.size());
    return Result<std::string>::good(pt);
}
} // namespace

Result<std::string> aead_open(const std::string& key32, const std::string& sealed)
{
    if (key32.size() != 32) return Result<std::string>::fail(Errc::InvalidArgument, "key must be 32 bytes", "aead_open");
    if (sealed.size() >= 1 + kNonce + kTag && uint8_t(sealed[0]) == kAeadV2)
    {
        auto v2 = open_v2(key32, sealed);
        if (v2.ok()) return v2;
        // A legacy blob whose random nonce happened to start with 0x02.
        auto v1 = open_legacy_v1(key32, sealed);
        return v1.ok() ? v1 : v2;
    }
    return open_legacy_v1(key32, sealed);
}

std::string hkdf_sha256(const std::string& ikm, const std::string& salt, const std::string& info, size_t len)
{
    std::string okm(len, '\0');
    PkeyCtx ctx;
    ctx.p = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    // RFC 5869: an absent salt is HashLen zero bytes (OpenSSL's default too;
    // set explicitly so an empty `salt` keeps the documented meaning).
    const std::string actual_salt = salt.empty() ? std::string(32, '\0') : salt;
    if (!ctx.p || EVP_PKEY_derive_init(ctx.p) != 1 || EVP_PKEY_CTX_set_hkdf_md(ctx.p, EVP_sha256()) != 1 || EVP_PKEY_CTX_set1_hkdf_salt(ctx.p, bytes(actual_salt), int(actual_salt.size())) != 1 || EVP_PKEY_CTX_set1_hkdf_key(ctx.p, bytes(ikm), int(ikm.size())) != 1 ||
        EVP_PKEY_CTX_add1_hkdf_info(ctx.p, bytes(info), int(info.size())) != 1)
        throw std::runtime_error("HKDF init failed");
    size_t n = len;
    if (len && EVP_PKEY_derive(ctx.p, reinterpret_cast<uint8_t*>(&okm[0]), &n) != 1) throw std::runtime_error("HKDF derive failed");
    return okm;
}

// ---------------- encodings ----------------

static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Standard base64 encode (with padding).
/// @param raw Bytes to encode.
/// @return Base64 text.
std::string base64_encode(const std::string& raw)
{
    std::string out;
    size_t i = 0;
    while (i + 2 < raw.size())
    {
        uint32_t v = (uint8_t(raw[i]) << 16) | (uint8_t(raw[i + 1]) << 8) | uint8_t(raw[i + 2]);
        out += B64[(v >> 18) & 63];
        out += B64[(v >> 12) & 63];
        out += B64[(v >> 6) & 63];
        out += B64[v & 63];
        i += 3;
    }
    if (i + 1 == raw.size())
    {
        uint32_t v = uint8_t(raw[i]) << 16;
        out += B64[(v >> 18) & 63];
        out += B64[(v >> 12) & 63];
        out += "==";
    }
    else if (i + 2 == raw.size())
    {
        uint32_t v = (uint8_t(raw[i]) << 16) | (uint8_t(raw[i + 1]) << 8);
        out += B64[(v >> 18) & 63];
        out += B64[(v >> 12) & 63];
        out += B64[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

/// @brief Standard base64 decode.
/// @param b64 Base64 text.
/// @param b64in Base64 text to decode.
/// @return Decoded bytes, or an InvalidArgument error for malformed input.
/// @return Decoded bytes, or an InvalidArgument error.
Result<std::string> base64_decode(const std::string& b64in)
{
    static int8_t rev[256];
    static bool init = false;
    if (!init)
    {
        std::memset(rev, -1, sizeof(rev));
        for (int i = 0; i < 64; ++i) rev[uint8_t(B64[i])] = int8_t(i);
        rev['-'] = 62;
        rev['_'] = 63; // accept URL-safe too
        init = true;
    }
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : b64in)
    {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int8_t v = rev[uint8_t(c)];
        if (v < 0) return Result<std::string>::fail(Errc::InvalidArgument, "invalid base64 character", "base64_decode");
        buf = (buf << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += char((buf >> bits) & 0xFF);
        }
    }
    return Result<std::string>::good(out);
}

/// @brief Lowercase hex encode.
/// @param raw Bytes to encode.
/// @return Hex text.
std::string hex_encode(const std::string& raw)
{
    static const char* d = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw)
    {
        out += d[c >> 4];
        out += d[c & 0xF];
    }
    return out;
}

/// @brief Hex decode (upper or lower case accepted).
/// @param hex Hex text.
/// @param hexin Hex text to decode.
/// @return Decoded bytes, or an InvalidArgument error for malformed input.
/// @return Decoded bytes, or an InvalidArgument error.
Result<std::string> hex_decode(const std::string& hexin)
{
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (hexin.size() % 2 != 0) return Result<std::string>::fail(Errc::InvalidArgument, "hex length must be even", "hex_decode");
    std::string out;
    out.reserve(hexin.size() / 2);
    for (size_t i = 0; i < hexin.size(); i += 2)
    {
        int hi = val(hexin[i]), lo = val(hexin[i + 1]);
        if (hi < 0 || lo < 0) return Result<std::string>::fail(Errc::InvalidArgument, "invalid hex digit", "hex_decode");
        out += char((hi << 4) | lo);
    }
    return Result<std::string>::good(out);
}

// ---------------- randomness ----------------

/// @brief Fill @p n bytes from the OS CSPRNG (getrandom(2)).
///
/// Fails loudly rather than silently degrading — key generation must not run
/// without entropy (§6.2).
///
/// @param out Buffer to fill.
/// @param n Number of bytes.
/// @return ok() on success, or the OS error.
Error random_bytes(uint8_t* out, size_t n)
{
    size_t off = 0;
    while (off < n)
    {
        ssize_t r = getrandom(out + off, n - off, 0);
        if (r < 0)
        {
            if (errno == EINTR) continue;
            return Error::make(Errc::Unavailable, "CSPRNG unavailable (getrandom failed)", "random_bytes");
        }
        off += size_t(r);
    }
    return Error::none();
}

} // namespace crypto
} // namespace instr