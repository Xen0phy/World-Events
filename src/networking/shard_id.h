//################################################################################
// shard_id.h
//--------------------------------------------------------------------------------
// ShardIdentity        stable per-instance fingerprint derived from MumbleLink
// ComputeShardIdentity builds one from the live Context
// GetShardLastAddressOctet   local-display-only IPv4 last octet (see below)
//--------------------------------------------------------------------------------
// Context::ShardID is not reliable for telling map INSTANCES apart - it does not
// vary the way its name implies. The actual signal for "which physical map server
// am I on" is Context::ServerAddress, the raw sockaddr the client is connected to
// (see Mumble.h - may be sockaddr_in or sockaddr_in6).
//
// We never send the raw address anywhere. ComputeShardIdentity folds the
// canonical (family, port, IP) bytes into a stable 64-bit FNV-1a hash, combined
// with MapID. Every client on the same instance computes the identical key, so it
// doubles as the partition key for the networking layer (e.g. a Cloudflare
// Durable Object name), without exposing anyone's literal server IP.
//
// Not std::hash: std::hash<T> is implementation-defined and can differ across STL
// versions/compilers, which would break the one property this whole thing depends
// on - every client deriving the same key.
//--------------------------------------------------------------------------------

#pragma once

#include "Mumble.h"

#include <cstdint>
#include <optional>
#include <string>

//********************************************************************************
// ShardIdentity
//--------------------------------------------------------------------------------
// mapId          Context::MapID at capture time
// addressHash    FNV-1a 64 of the canonical (family, port, IP) bytes only;
//                excludes sin_zero/flowinfo/scope_id (see below)
// valid          false unless connected to a map server; callers must check
//                this before using mapId/addressHash or calling ToKey()
// ToKey()        "map{mapId}-{addressHash as 16 hex chars}"; stable across
//                reconnects to the same instance, changes on map/shard change
//--------------------------------------------------------------------------------
// addressHash excludes sin_zero (IPv4 padding, not guaranteed zeroed) and
// flowinfo/scope_id (IPv6 fields describing the LOCAL connection, not the remote
// server, so they differ between clients on the same shard).
//
// valid is false at character select, on loading screens, or for an unrecognized
// address family - not just "never connected".
//--------------------------------------------------------------------------------
struct ShardIdentity
{
    uint32_t mapId       = 0;
    uint64_t addressHash = 0;
    bool     valid       = false;

    std::string ToKey() const;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ComputeShardIdentity
//--------------------------------------------------------------------------------
// Builds a ShardIdentity from the live MumbleLink context. Cheap enough to call
// every frame or on-demand (e.g. right before the report button sends) - no need
// to cache it beyond a single read, since it's already derived straight from
// Mumble's own live state.
//--------------------------------------------------------------------------------
ShardIdentity ComputeShardIdentity(const Mumble::Context& context);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetShardLastAddressOctet
//--------------------------------------------------------------------------------
// The last byte of Context::ServerAddress's IPv4 address (xxx.yyy.zzz.THIS) -
// local on-screen display only, telling apart two players seeing the same map
// name but on different physical servers, without exposing the full address
// (see file header on why the full address is never sent or stored anywhere
// else). nullopt for IPv6 (no single-octet analog) or wherever
// ComputeShardIdentity would itself report invalid - not connected to a map
// server, or an unrecognized address family.
//--------------------------------------------------------------------------------
std::optional<uint8_t> GetShardLastAddressOctet(const Mumble::Context& context);