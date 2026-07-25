//################################################################################
// apikey_crypto.h
//--------------------------------------------------------------------------------
// Encrypt(addonDir, plaintext)   AES-256-GCM encrypts, returns a base64 blob
// Decrypt(addonDir, blob)        reverses Encrypt, returns "" if invalid
//--------------------------------------------------------------------------------
// Encrypts/decrypts the GW2 API key for storage in settings.ini, so the ini
// file itself never holds the raw key in plaintext (see Gw2ApiKey in
// settings_table.h for the field itself).
//
// Approach: AES-256-GCM via Windows' built-in bcrypt.dll (BCrypt/CNG) - no
// bundled crypto library, so the -static binary stays small and there's no
// third-party crypto code to audit. A random 256-bit master key is
// generated on first use and stored in its own file, "apikey.key", next to
// settings.ini - not inside it. The encrypted blob (nonce + tag +
// ciphertext, base64) is what actually gets written as the Gw2ApiKey value
// in the ini.
//
// Threat model: protects against settings.ini being leaked or shared on
// its own (bug reports, cloud sync, screen recordings, etc.) since the ini
// alone is no longer enough to recover the key. Does NOT protect against
// an attacker with full read access to the whole addon data directory
// (apikey.key sits right next to settings.ini) - no scheme that needs zero
// extra setup and zero extra user-managed secrets can promise that.
// Windows DPAPI would tie the key to the OS user account and defend
// further, but its behavior under Wine/Proton is inconsistent, so it's
// deliberately not used here - this scheme is pure software AES and
// behaves identically on native Windows and under Proton. bcrypt.dll ships
// with Windows 7+ and is exercised heavily by other software, so it's a
// safe bet under Proton too.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

namespace ApiKeyCrypto
{
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Encrypt / Decrypt
    //--------------------------------------------------------------------------------
    // plaintext <-> base64 blob safe to store as a single ini value
    // (Gw2ApiKey). Encrypt returns "" if encryption isn't available
    // (master key or bcrypt provider unavailable) - caller should treat
    // that as "couldn't save the key", not fall back to plaintext.
    // Decrypt returns "" (rather than throwing) if the blob can't be
    // decrypted (missing/rotated key, corrupted blob, moved install, GCM
    // tag mismatch) - LoadSettings treats "" as "not one of our blobs" and
    // falls back to the raw ini value, making upgrades from a
    // pre-encryption plaintext Gw2ApiKey transparent (see settings.cpp).
    //--------------------------------------------------------------------------------
    std::string Encrypt(const std::string& addonDir, const std::string& plaintext);
    std::string Decrypt(const std::string& addonDir, const std::string& blob);
}