//################################################################################
// apikey_crypto.cpp
//--------------------------------------------------------------------------------
// See apikey_crypto.h for the overall approach and threat model.
//--------------------------------------------------------------------------------

#include "apikey_crypto.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h> //. has to come after windows.h

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr size_t kKeyBytes   = 32; //. AES-256
    constexpr size_t kNonceBytes = 12; //. standard GCM nonce size
    constexpr size_t kTagBytes   = 16; //. full GCM tag

    const char kB64Alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Base64Encode / Base64Decode
    //--------------------------------------------------------------------------------
    // Minimal base64 codec - avoids pulling in crypt32 just for
    // CryptBinaryToString/CryptStringToBinary. Decode returns false on any malformed
    // input (bad length, invalid character, etc.) - callers treat that as "not one of
    // our blobs".
    //--------------------------------------------------------------------------------
    std::string Base64Encode(const std::vector<uint8_t>& data)
    {
        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);
        size_t i = 0;
        while (i + 3 <= data.size())
        {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out += kB64Alphabet[(n >> 18) & 0x3F];
            out += kB64Alphabet[(n >> 12) & 0x3F];
            out += kB64Alphabet[(n >> 6) & 0x3F];
            out += kB64Alphabet[n & 0x3F];
            i += 3;
        }
        size_t rem = data.size() - i;
        if (rem == 1)
        {
            uint32_t n = data[i] << 16;
            out += kB64Alphabet[(n >> 18) & 0x3F];
            out += kB64Alphabet[(n >> 12) & 0x3F];
            out += "==";
        }
        else if (rem == 2)
        {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
            out += kB64Alphabet[(n >> 18) & 0x3F];
            out += kB64Alphabet[(n >> 12) & 0x3F];
            out += kB64Alphabet[(n >> 6) & 0x3F];
            out += "=";
        }
        return out;
    }

    bool Base64Decode(const std::string& in, std::vector<uint8_t>& out)
    {
        auto val = [](char c) -> int
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };

        std::string s = in;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s.size() % 4 != 0) return false;

        //_ '=' is only legal in the final group's last one or two slots -
        // reject it anywhere else so a malformed middle-of-string '=' can't
        // silently decode instead of being caught here.
        for (size_t i = 0; i + 2 < s.size(); i++)
            if (s[i] == '=') return false;
        if (s.size() >= 2 && s[s.size() - 2] == '=' && s[s.size() - 1] != '=') return false;

        size_t padding = 0;
        if (s.back() == '=') padding++;
        if (s.size() >= 2 && s[s.size() - 2] == '=') padding++;

        out.clear();
        out.reserve((s.size() / 4) * 3);
        for (size_t i = 0; i < s.size(); i += 4)
        {
            int v0 = val(s[i]);
            int v1 = val(s[i + 1]);
            int v2 = (s[i + 2] == '=') ? 0 : val(s[i + 2]);
            int v3 = (s[i + 3] == '=') ? 0 : val(s[i + 3]);
            if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;

            uint32_t n = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;
            out.push_back((n >> 16) & 0xFF);
            if (s[i + 2] != '=') out.push_back((n >> 8) & 0xFF);
            if (s[i + 3] != '=') out.push_back(n & 0xFF);
        }
        if (padding > 0 && out.empty()) return false;
        return true;
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // LoadOrCreateMasterKey
    //--------------------------------------------------------------------------------
    // Loads "<addonDir>\apikey.key" (32 raw bytes) if present and the right size,
    // otherwise generates one via BCryptGenRandom. Kept separate from settings.ini on
    // purpose (see apikey_crypto.h).
    //--------------------------------------------------------------------------------
    bool LoadOrCreateMasterKey(const std::string& addonDir, std::vector<uint8_t>& outKey)
    {
        try
        {
            fs::create_directories(addonDir);
            std::string keyPath = addonDir + "\\apikey.key";

            //_ Scoped so `in` closes here - a regenerate below reopens the
            // same path for write, which must not race a still-open read handle.
            {
                std::ifstream in(keyPath, std::ios::binary);
                if (in.is_open())
                {
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                                std::istreambuf_iterator<char>());
                    if (bytes.size() == kKeyBytes)
                    {
                        outKey = std::move(bytes);
                        return true;
                    }
                    //_ Wrong size (corrupted/hand-edited) - regenerate;
                    // anything encrypted under the old key becomes undecryptable.
                }
            }

            outKey.resize(kKeyBytes);
            NTSTATUS status = BCryptGenRandom(
                nullptr, outKey.data(), (ULONG)outKey.size(),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (status != 0) return false;

            std::ofstream out(keyPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            out.write(reinterpret_cast<const char*>(outKey.data()), outKey.size());
            if (!out.good()) return false;

            //_ Best-effort: hides the key file from casual browsing, not
            // real access control - just reduces the odds it gets zipped up.
            SetFileAttributesA(keyPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
            return true;
        }
        catch (...) { return false; }
    }

    //********************************************************************************
    // AesGcm
    //--------------------------------------------------------------------------------
    // hAlg   BCrypt algorithm provider handle
    // hKey   BCrypt symmetric key handle
    //--------------------------------------------------------------------------------
    // AES-256-GCM via BCrypt. Blob layout: nonce(12) || tag(16) || ciphertext. Open()
    // sets up the provider/key pair; the destructor tears both down.
    //--------------------------------------------------------------------------------
    struct AesGcm
    {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_KEY_HANDLE hKey = nullptr;

        bool Open(const std::vector<uint8_t>& key)
        {
            if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
                return false;
            if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                    (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                    sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0)
                return false;
            if (BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                    (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0)
                return false;
            return true;
        }

        ~AesGcm()
        {
            if (hKey) BCryptDestroyKey(hKey);
            if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
        }
    };
}

namespace ApiKeyCrypto
{
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Encrypt / Decrypt
    //--------------------------------------------------------------------------------
    // See apikey_crypto.h for behavior/return-value conventions.
    //--------------------------------------------------------------------------------
    std::string Encrypt(const std::string& addonDir, const std::string& plaintext)
    {
        try
        {
            if (plaintext.empty()) return "";

            std::vector<uint8_t> masterKey;
            if (!LoadOrCreateMasterKey(addonDir, masterKey)) return "";

            AesGcm ctx;
            if (!ctx.Open(masterKey)) return "";

            uint8_t nonce[kNonceBytes];
            if (BCryptGenRandom(nullptr, nonce, kNonceBytes,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
                return "";

            uint8_t tag[kTagBytes] = {};
            std::vector<uint8_t> ciphertext(plaintext.size());

            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
            BCRYPT_INIT_AUTH_MODE_INFO(info);
            info.pbNonce = nonce;
            info.cbNonce = kNonceBytes;
            info.pbTag = tag;
            info.cbTag = kTagBytes;

            ULONG resultLen = 0;
            NTSTATUS status = BCryptEncrypt(
                ctx.hKey,
                (PUCHAR)plaintext.data(), (ULONG)plaintext.size(),
                &info, nullptr, 0,
                ciphertext.empty() ? nullptr : ciphertext.data(), (ULONG)ciphertext.size(),
                &resultLen, 0);
            if (status != 0) return "";

            std::vector<uint8_t> blob;
            blob.reserve(kNonceBytes + kTagBytes + ciphertext.size());
            blob.insert(blob.end(), nonce, nonce + kNonceBytes);
            blob.insert(blob.end(), tag, tag + kTagBytes);
            blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());

            return Base64Encode(blob);
        }
        catch (...) { return ""; }
    }

    std::string Decrypt(const std::string& addonDir, const std::string& blob)
    {
        try
        {
            if (blob.empty()) return "";

            std::vector<uint8_t> raw;
            if (!Base64Decode(blob, raw)) return "";
            if (raw.size() < kNonceBytes + kTagBytes) return "";

            std::vector<uint8_t> masterKey;
            if (!LoadOrCreateMasterKey(addonDir, masterKey)) return "";

            AesGcm ctx;
            if (!ctx.Open(masterKey)) return "";

            uint8_t* nonce = raw.data();
            uint8_t* tag = raw.data() + kNonceBytes;
            uint8_t* ciphertext = raw.data() + kNonceBytes + kTagBytes;
            size_t ciphertextLen = raw.size() - kNonceBytes - kTagBytes;

            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
            BCRYPT_INIT_AUTH_MODE_INFO(info);
            info.pbNonce = nonce;
            info.cbNonce = kNonceBytes;
            info.pbTag = tag;
            info.cbTag = kTagBytes;

            std::vector<uint8_t> plaintext(ciphertextLen);
            ULONG resultLen = 0;
            NTSTATUS status = BCryptDecrypt(
                ctx.hKey,
                ciphertextLen ? ciphertext : nullptr, (ULONG)ciphertextLen,
                &info, nullptr, 0,
                plaintext.empty() ? nullptr : plaintext.data(), (ULONG)plaintext.size(),
                &resultLen, 0);
            //_ Non-zero status includes STATUS_AUTH_TAG_MISMATCH (wrong key
            // or tampered/corrupted blob) - either way, not decryptable.
            if (status != 0) return "";

            return std::string(reinterpret_cast<char*>(plaintext.data()), resultLen);
        }
        catch (...) { return ""; }
    }
}