// apikey_crypto.h
//
// Encrypts/decrypts the GW2 API key for storage in settings.ini, so the
// ini file itself never holds the raw key in plaintext (see the comment on
// Gw2ApiKey in settings_table.h for the field itself).
//
// Approach: AES-256-GCM via Windows' built-in bcrypt.dll (BCrypt/CNG) —
// no bundled crypto library, so the -static binary stays small and there's
// no third-party crypto code to audit. A random 256-bit master key is
// generated on first use and stored in its own file, "apikey.key", next to
// settings.ini — not inside it. The encrypted blob (nonce + tag +
// ciphertext, base64) is what actually gets written as the Gw2ApiKey value
// in the ini.
//
// Threat model: this protects against settings.ini being leaked or shared
// on its own — pasted into a bug report, synced to a cloud drive, scraped
// by some other plaintext-ini-reading tool, caught in a screen recording of
// the addon folder, etc. — since the ini alone is no longer enough to
// recover the key. It does NOT protect against an attacker with full read
// access to the whole addon data directory (apikey.key sits right next to
// settings.ini); no scheme that needs zero extra setup and zero extra
// user-managed secrets can promise that. Windows DPAPI would tie the key to
// the OS user account and defend a bit further, but its behavior under
// Wine/Proton is inconsistent across versions, so it's deliberately not
// used here — this scheme is pure software AES and behaves identically on
// native Windows and under Proton.
//
// bcrypt.dll ships with Windows 7+ and is exercised heavily by other
// software (TLS, hashing, etc.), so it's a safe bet under Proton too, but
// as with anything Wine-related it's worth confirming on your setup.
#pragma once
#include <string>

namespace ApiKeyCrypto
{
    // plaintext -> base64 blob safe to write as a single ini value.
    // Returns "" if encryption isn't available for some reason (master
    // key couldn't be created/read, bcrypt provider unavailable, etc.) —
    // caller should treat that as "couldn't save the key" rather than
    // silently falling back to writing plaintext.
    std::string Encrypt(const std::string& addonDir, const std::string& plaintext);

    // base64 blob -> plaintext, or "" if it can't be decrypted (missing or
    // rotated master key, corrupted/truncated blob, moved to a different
    // install without apikey.key, GCM tag mismatch, etc.).
    //
    // Deliberately returns "" rather than throwing: LoadSettings treats an
    // empty result as "not decryptable as our own blob" and falls back to
    // using the raw ini value as-is, which is what makes upgrading from a
    // pre-encryption settings.ini (plaintext Gw2ApiKey) transparent — see
    // settings.cpp.
    std::string Decrypt(const std::string& addonDir, const std::string& blob);
}
