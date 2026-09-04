//################################################################################
// mumble_identity.cpp   (see: mumble_identity.h)
//--------------------------------------------------------------------------------

#include "mumble_identity.h"

#include "addon.h" //. extern MumbleLink
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using json = nlohmann::json;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ParseMumbleIdentity   (see: mumble_identity.h)
//--------------------------------------------------------------------------------
std::optional<MumbleIdentity> ParseMumbleIdentity()
{
    if (!MumbleLink || MumbleLink->Identity[0] == L'\0')
        return std::nullopt;

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, MumbleLink->Identity, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0)
        return std::nullopt;
    std::string utf8Identity(utf8Len - 1, '\0');   //. -1 drops the counted null terminator
    WideCharToMultiByte(CP_UTF8, 0, MumbleLink->Identity, -1, utf8Identity.data(), utf8Len, nullptr, nullptr);

    try
    {
        json j = json::parse(utf8Identity);
        MumbleIdentity id;
        id.name = j.value("name", "");
        return id;
    }
    catch (...) { return std::nullopt; }
}
