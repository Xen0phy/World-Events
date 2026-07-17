// icon_whitener.cpp
// Popup window that converts a user-picked icon to a "white/gray + alpha"
// form suitable for use as a map-overlay icon in maprender.cpp.
//
// Image I/O uses the Windows Imaging Component (WIC) — no extra files or
// libraries needed beyond wincodec.h (mingw-w64) and -lwindowscodecs -lole32
// in the linker flags.
//
// Conversion pipeline (single pass):
//   1. Luminance desaturation (ITU-R BT.709) with proper sRGB linearisation.
//      Matches GIMP's Colors -> Desaturate -> Luminance.
//   2. Normalize: scale all pixels up so the brightest maps to white,
//      preserving relative shading between light and dark areas.
//
// The result is saved as "<textures dir>/<stem>_white.png".

#include "icon_whitener.h"
#include "maprender.h"   // GetEventIconFilenames, ScanEventIconFiles
#include "addon.h"       // g_AddonDir
#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_Disabled

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace {

static bool        s_open          = false;
static int         s_iconIndex     = 0;
static std::string s_statusMessage;
static bool        s_statusIsError = false;

// ---------------------------------------------------------------------------
// ProcessPixels — desaturate (luminance) then normalize
// WIC decodes to GUID_WICPixelFormat32bppBGRA: byte order is B G R A.
// Alpha is untouched.
// ---------------------------------------------------------------------------
static void ProcessPixels(UINT w, UINT h, BYTE* px)
{
    auto toLinear = [](float c) -> float {
        return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    auto toSRGB = [](float c) -> float {
        return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    };
    auto to8 = [](float c) -> BYTE {
        return (BYTE)std::clamp((int)(c * 255.0f + 0.5f), 0, 255);
    };

    const UINT n = w * h;

    // Pass 1 — desaturate to luminance, store result back as gray sRGB.
    // Also track the brightest value for the normalize pass.
    float brightest = 0.0f;
    for (UINT i = 0; i < n; i++)
    {
        BYTE* p = px + i * 4;
        float b = toLinear(p[0] / 255.0f);
        float g = toLinear(p[1] / 255.0f);
        float r = toLinear(p[2] / 255.0f);

        float lum  = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        float srgb = toSRGB(lum);

        p[0] = p[1] = p[2] = to8(srgb);
        brightest = std::max(brightest, srgb);
    }

    if (brightest < 1e-6f) return; // fully black, nothing to normalize

    // Pass 2 — normalize so the brightest pixel becomes white.
    float scale = 1.0f / brightest;
    for (UINT i = 0; i < n; i++)
    {
        BYTE* p  = px + i * 4;
        float v  = (p[0] / 255.0f) * scale; // r == g == b after pass 1
        BYTE  out = to8(v);
        p[0] = p[1] = p[2] = out;
    }
}

// ---------------------------------------------------------------------------
// WIC helper
// ---------------------------------------------------------------------------
static std::wstring ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

// ---------------------------------------------------------------------------
// DoConvert — load via WIC, process pixels, save via WIC
// ---------------------------------------------------------------------------
static void DoConvert(const std::string& filename)
{
    std::string texDir      = g_AddonDir + "\\textures";
    std::string inPath      = texDir + "\\" + filename;

    std::filesystem::path p(filename);
    std::string outFilename = p.stem().string() + "_white.png";
    std::string outPath     = texDir + "\\" + outFilename;

    std::wstring wInPath  = ToWide(inPath);
    std::wstring wOutPath = ToWide(outPath);

    IWICImagingFactory*    factory  = nullptr;
    IWICBitmapDecoder*     decoder  = nullptr;
    IWICBitmapFrameDecode* frame    = nullptr;
    IWICFormatConverter*   converter= nullptr;
    IWICBitmap*            bitmap   = nullptr;
    IWICBitmapLock*        lock     = nullptr;
    IWICStream*            outStream= nullptr;
    IWICBitmapEncoder*     encoder  = nullptr;
    IWICBitmapFrameEncode* outFrame = nullptr;

    HRESULT hr = S_OK;
    std::string errMsg;

    // CoInitializeEx must be paired with exactly one CoUninitialize per
    // thread that successfully initialized (or re-initialized) COM — S_OK
    // means this call did the initializing, S_FALSE means COM was already
    // initialized on this thread (e.g. by ImGui/the host) and this call
    // just bumped its per-thread refcount, and in both cases a matching
    // CoUninitialize is this function's responsibility to make. Any other
    // result (e.g. RPC_E_CHANGED_MODE, if the thread is already COM-
    // initialized with an incompatible concurrency model) means COM was
    // NOT initialized by this call, so it must NOT be uninitialized by it
    // either. Without this check, every "Convert & Save" click leaked one
    // COM apartment reference on the UI thread that was never released.
    HRESULT coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comNeedsUninit = (coInitResult == S_OK || coInitResult == S_FALSE);

    auto fail = [&](const char* where) {
        char buf[16]; snprintf(buf, sizeof(buf), "%08X", (unsigned)hr);
        errMsg = std::string(where) + " failed (HRESULT 0x" + buf + ")";
    };

    do {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_IWICImagingFactory, (void**)&factory);
        if (FAILED(hr)) { fail("CoCreateInstance(WICImagingFactory)"); break; }

        hr = factory->CreateDecoderFromFilename(wInPath.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) { fail("CreateDecoderFromFilename"); break; }

        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) { fail("GetFrame"); break; }

        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) { fail("CreateFormatConverter"); break; }

        hr = converter->Initialize(frame,
                GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) { fail("FormatConverter::Initialize"); break; }

        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);

        hr = factory->CreateBitmapFromSource(converter, WICBitmapCacheOnDemand, &bitmap);
        if (FAILED(hr)) { fail("CreateBitmapFromSource"); break; }

        WICRect rect = { 0, 0, (INT)w, (INT)h };
        hr = bitmap->Lock(&rect, WICBitmapLockRead | WICBitmapLockWrite, &lock);
        if (FAILED(hr)) { fail("Bitmap::Lock"); break; }

        UINT bufSize = 0; BYTE* buf = nullptr;
        hr = lock->GetDataPointer(&bufSize, &buf);
        if (FAILED(hr)) { fail("GetDataPointer"); break; }

        ProcessPixels(w, h, buf);

        lock->Release(); lock = nullptr;

        hr = factory->CreateStream(&outStream);
        if (FAILED(hr)) { fail("CreateStream"); break; }

        hr = outStream->InitializeFromFilename(wOutPath.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) { fail("Stream::InitializeFromFilename"); break; }

        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr)) { fail("CreateEncoder(PNG)"); break; }

        hr = encoder->Initialize(outStream, WICBitmapEncoderNoCache);
        if (FAILED(hr)) { fail("Encoder::Initialize"); break; }

        hr = encoder->CreateNewFrame(&outFrame, nullptr);
        if (FAILED(hr)) { fail("CreateNewFrame"); break; }

        hr = outFrame->Initialize(nullptr);
        if (FAILED(hr)) { fail("Frame::Initialize"); break; }

        hr = outFrame->WriteSource(bitmap, nullptr);
        if (FAILED(hr)) { fail("Frame::WriteSource"); break; }

        hr = outFrame->Commit();
        if (FAILED(hr)) { fail("Frame::Commit"); break; }

        hr = encoder->Commit();
        if (FAILED(hr)) { fail("Encoder::Commit"); break; }

    } while (false);

    if (outFrame)   outFrame->Release();
    if (encoder)    encoder->Release();
    if (outStream)  outStream->Release();
    if (lock)       lock->Release();
    if (bitmap)     bitmap->Release();
    if (converter)  converter->Release();
    if (frame)      frame->Release();
    if (decoder)    decoder->Release();
    if (factory)    factory->Release();

    if (comNeedsUninit)
        CoUninitialize();

    if (!errMsg.empty())
    {
        s_statusMessage = errMsg;
        s_statusIsError = true;
        return;
    }

    ScanEventIconFiles();
    s_statusMessage = "Saved as: " + outFilename;
    s_statusIsError = false;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void DrawIconWhitenerButton()
{
    if (ImGui::Button("Icon Whitener"))
    {
        s_open          = true;
        s_statusMessage = "";
        s_iconIndex     = 0;
        ImGui::OpenPopup("Icon Whitener##popup");
    }
}

void DrawIconWhitenerPopup()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Icon Whitener##popup", &s_open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    ImGui::TextWrapped(
        "Map icons are tinted at draw time with a multiplicative color blend. "
        "This only looks correct when the icon's RGB is neutral gray — a "
        "colored image will tint unpredictably instead of cleanly turning "
        "red / orange / gray.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Pick an icon from the textures/ folder and press Convert. "
        "The image will be desaturated to luminance and normalized so the "
        "brightest pixel becomes white. "
        "The result is saved as <name>_white.png next to the original.");
    ImGui::Separator();
    ImGui::Spacing();

    const std::vector<std::string>& iconFiles = GetEventIconFilenames();

    std::vector<const char*> labels;
    labels.push_back("(select an icon)");
    for (const auto& fn : iconFiles)
        labels.push_back(fn.c_str());

    ImGui::SetNextItemWidth(300.0f);
    ImGui::Combo("Icon##whitener_pick", &s_iconIndex, labels.data(), (int)labels.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##whitener_rescan"))
    {
        ScanEventIconFiles();
        s_iconIndex     = 0;
        s_statusMessage = "";
    }

    ImGui::Spacing();

    bool canConvert = (s_iconIndex > 0);
    if (!canConvert)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }

    if (ImGui::Button("Convert & Save##whitener_go"))
    {
        s_statusMessage = "";
        DoConvert(iconFiles[s_iconIndex - 1]);
    }

    if (!canConvert)
    {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    ImGui::SameLine();
    if (ImGui::Button("Close##whitener_close"))
    {
        s_open = false;
        ImGui::CloseCurrentPopup();
    }

    if (!s_statusMessage.empty())
    {
        ImGui::Spacing();
        ImVec4 col = s_statusIsError
            ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
            : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        ImGui::TextColored(col, "%s", s_statusMessage.c_str());
    }

    ImGui::Spacing();
    ImGui::EndPopup();
}