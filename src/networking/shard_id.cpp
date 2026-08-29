//################################################################################
// shard_id.cpp
//--------------------------------------------------------------------------------
// Fnv1a64          fixed, portable 64-bit hash (see shard_id.h for why not
//                   std::hash)
// ComputeShardIdentity   extracts + validates + hashes ServerAddress
// GetShardLastAddressOctet   extracts just the last IPv4 byte (see shard_id.h)
//--------------------------------------------------------------------------------

#include "shard_id.h"

#include <cstdio>
#include <cstring>

namespace
{
    //_ WinSock AF_INET
    constexpr uint16_t AF_INET_WIN  = 2;
    //_ WinSock AF_INET6; any other non-zero family is treated as unrecognized (see shard_id.h).
    constexpr uint16_t AF_INET6_WIN = 23;

    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME        = 1099511628211ULL;

    uint64_t Fnv1a64(const void* data, size_t len)
    {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        uint64_t hash = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < len; ++i)
        {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }
}

std::string ShardIdentity::ToKey() const
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "map%u-%016llx", mapId,
        static_cast<unsigned long long>(addressHash));
    return std::string(buf);
}

ShardIdentity ComputeShardIdentity(const Mumble::Context& context)
{
    ShardIdentity id;
    id.mapId = context.MapID;

    const unsigned char* addr = context.ServerAddress;
    uint16_t family = static_cast<uint16_t>(addr[0] | (addr[1] << 8));

    //_ Canonical bytes to hash: family+port+IP only; sized for IPv6 (larger), IPv4 fills only the first 8.
    unsigned char canonical[20] = {};
    size_t        canonicalLen  = 0;

    if (family == AF_INET_WIN)
    {
        //_ sockaddr_in: family(2)+port(2)+addr(4); the 8 bytes of sin_zero padding after that aren't touched (not guaranteed zeroed).
        std::memcpy(canonical, addr, 8);
        canonicalLen = 8;
    }
    else if (family == AF_INET6_WIN)
    {
        //_ sockaddr_in6: keeps family+port+addr, skips flowinfo/scope_id - those describe the LOCAL connection, not the remote server, so including them would make same-shard clients disagree.
        std::memcpy(canonical,     addr,     4);   //. family + port
        std::memcpy(canonical + 4, addr + 8, 16);   //. IPv6 address
        canonicalLen = 20;
    }
    else
    {
        //_ Not connected (char select, loading screen) or unrecognized family - don't hash, or every client in this state would collide into the same fake shard.
        id.valid = false;
        return id;
    }

    //_ Family set but host/port still all-zero (also seen mid-connect/loading) - same collision risk, same guard.
    bool allZero = true;
    for (size_t i = 2; i < canonicalLen; ++i) //. skip the family bytes themselves
    {
        if (canonical[i] != 0) { allZero = false; break; }
    }
    if (allZero)
    {
        id.valid = false;
        return id;
    }

    id.addressHash = Fnv1a64(canonical, canonicalLen);
    id.valid       = true;
    return id;
}

std::optional<uint8_t> GetShardLastAddressOctet(const Mumble::Context& context)
{
    const unsigned char* addr = context.ServerAddress;
    uint16_t family = static_cast<uint16_t>(addr[0] | (addr[1] << 8));
    if (family != AF_INET_WIN)
        return std::nullopt;

    //_ Same all-zero guard as ComputeShardIdentity - mid-connect/loading state.
    bool allZero = true;
    for (size_t i = 2; i < 8; ++i)
    {
        if (addr[i] != 0) { allZero = false; break; }
    }
    if (allZero)
        return std::nullopt;

    return addr[7]; //. sockaddr_in: family(2)+port(2)+addr(4), so the last IPv4 byte sits at offset 7
}