//################################################################################
// mumble_identity.h
//--------------------------------------------------------------------------------
// MumbleIdentity                name parsed from Mumble's Identity JSON
// ParseMumbleIdentity()          parses MumbleLink->Identity once
//--------------------------------------------------------------------------------
// Mumble::Data::Identity (Mumble.h) is a UTF-16 JSON string -
// {"name":"...","world_id":N,...} - not exposed as separate typed fields.
// GetMumbleCharacterName (subscriptions.cpp) needs the "name" field out of that
// JSON, so the UTF16->UTF8 narrow + json::parse happens here.
//
// "world_id" is in that same JSON but is no longer populated by the GW2 client -
// see GetLiveEventsRegion (gw2_api.h) for the account's home world/ NA-EU region,
// which now comes from the GW2 API v2 instead.
//
// This is its own file, not shard_id.h: ShardIdentity is derived from
// Mumble::Context (which physical map server the client is on); this is derived
// from Mumble::Data::Identity (which character the client is playing) - two
// different Mumble fields answering two different questions. Keeping them apart
// means a reader of shard_id.h never has to wonder whether "identity" there means
// player identity too.
//--------------------------------------------------------------------------------

#pragma once

#include <optional>
#include <string>

//********************************************************************************
// MumbleIdentity
//--------------------------------------------------------------------------------
// name       character name, UTF-8, straight from the JSON - see
//            GetMumbleCharacterName for the further ANSI narrow it applies
//--------------------------------------------------------------------------------
struct MumbleIdentity
{
    std::string name;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ParseMumbleIdentity
//--------------------------------------------------------------------------------
// std::nullopt on any failure: MumbleLink not ready yet, or a malformed/empty
// Identity string. Cheap enough to call every frame or on-demand - no reason to
// cache beyond a single read, since it's derived straight from Mumble's own live
// state.
//--------------------------------------------------------------------------------
std::optional<MumbleIdentity> ParseMumbleIdentity();
