// icon_whitener.cpp
// ---------------------------------------------------------------------------
// Popup window that converts a user-picked icon to a "white/gray + alpha"
// form suitable for use as a map-overlay icon in maprender.cpp.
//
// Image I/O uses the Windows Imaging Component (WIC) — no extra files or
// libraries needed beyond wincodec.h (mingw-w64) and -lwindowscodecs -lole32
// in the linker flags.
//
// Two conversion modes:
//
//   HSV Saturation (for colored images)
//   ─────────────────────────────────────
//   Converts each pixel to HSV, sets S = 0, converts back to RGB.
//   Equivalent to GIMP's Hue-Saturation tool at Saturation = -100.
//   Bright pixels stay bright; dark pixels stay dark.
//
//   Overlay (for already-gray images)
//   ────────────────────────────────────
//   Composites the image over a solid white layer using the Overlay blend
//   formula, lifting mid-tones toward white. Useful when the icon is already
//   monochrome but its highlights are only 70-80% luminance — this pushes
//   them to a clean 100% without destroying edge detail.
// ---------------------------------------------------------------------------

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

enum class WhitenMode { HSVSaturation = 0, Overlay = 1 };

static bool         s_open          = false;
static int          s_iconIndex     = 0;
static WhitenMode   s_mode          = WhitenMode::HSVSaturation;
static std::string  s_statusMessage;
static bool         s_statusIsError = false;

// ---------------------------------------------------------------------------
// Pixel conversion helpers
// ---------------------------------------------------------------------------
static void RGBtoHSV(float r, float g, float b, float& h, float& s, float& v)
{
    float cmax  = std::max({ r, g, b });
    float cmin  = std::min({ r, g, b });
    float delta = cmax - cmin;

    v = cmax;
    s = (cmax > 0.0f) ? (delta / cmax) : 0.0f;

    if (delta < 1e-6f) { h = 0.0f; return; }

    if      (cmax == r) h = std::fmod((g - b) / delta, 6.0f);
    else if (cmax == g) h = (b - r) / delta + 2.0f;
    else                h = (r - g) / delta + 4.0f;

    h /= 6.0f;
    if (h < 0.0f) h += 1.0f;
}

static void HSVtoRGB(float h, float s, float v, float& r, float& g, float& b)
{
    if (s < 1e-6f) { r = g = b = v; return; }

    float hh = h * 6.0f;
    int   i  = (int)std::floor(hh);
    float f  = hh - (float)i;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * f);
    float t  = v * (1.0f - s * (1.0f - f));

    switch (i % 6)
    {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default:r = v; g = p; b = q; break;
    }
}

static void ProcessPixels(UINT w, UINT h, BYTE* px, WhitenMode mode)
{
    // WIC decoded to GUID_WICPixelFormat32bppBGRA — byte order is B G R A.
    for (UINT i = 0; i < w * h; i++)
    {
        BYTE* p = px + i * 4;
        float b = p[0] / 255.0f;
        float g = p[1] / 255.0f;
        float r = p[2] / 255.0f;
        // p[3] = alpha, untouched

        float outR, outG, outB;

        if (mode == WhitenMode::HSVSaturation)
        {
            float hh, s, v;
            RGBtoHSV(r, g, b, hh, s, v);
            s = 0.0f;
            HSVtoRGB(hh, s, v, outR, outG, outB);
        }
        else // Overlay against white
        {
            // With a solid-white (1.0) base the ">= 0.5" branch always
            // collapses to 1.0, so only the "< 0.5" branch does real work.
            auto ov = [](float c) { return c < 0.5f ? 2.0f * c : 1.0f; };
            outR = ov(r);
            outG = ov(g);
            outB = ov(b);
        }

        auto to8 = [](float c) -> BYTE {
            return (BYTE)std::clamp((int)(c * 255.0f + 0.5f), 0, 255);
        };

        p[0] = to8(outB);
        p[1] = to8(outG);
        p[2] = to8(outR);
    }
}

// ---------------------------------------------------------------------------
// WIC helpers — wide string conversion for WIC APIs
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
static void DoConvert(const std::string& filename, WhitenMode mode)
{
    std::string texDir      = g_AddonDir + "\\textures";
    std::string inPath      = texDir + "\\" + filename;

    std::filesystem::path p(filename);
    std::string outFilename = p.stem().string() + "_white.png";
    std::string outPath     = texDir + "\\" + outFilename;

    std::wstring wInPath  = ToWide(inPath);
    std::wstring wOutPath = ToWide(outPath);

    IWICImagingFactory*   factory  = nullptr;
    IWICBitmapDecoder*    decoder  = nullptr;
    IWICBitmapFrameDecode* frame   = nullptr;
    IWICFormatConverter*  converter= nullptr;
    IWICBitmap*           bitmap   = nullptr;
    IWICBitmapLock*       lock     = nullptr;
    IWICStream*           outStream= nullptr;
    IWICBitmapEncoder*    encoder  = nullptr;
    IWICBitmapFrameEncode* outFrame= nullptr;

    HRESULT hr = S_OK;
    std::string errMsg;

    // Use COINIT_APARTMENTTHREADED — matches what the game's render thread
    // already uses; calling CoInitializeEx again on an already-initialized
    // thread is a no-op and returns S_FALSE, which is fine.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto fail = [&](const char* where) {
        errMsg = std::string(where) + " failed (HRESULT 0x" +
                 [](HRESULT h) {
                     char buf[16]; snprintf(buf, sizeof(buf), "%08X", (unsigned)h);
                     return std::string(buf);
                 }(hr) + ")";
    };

    do {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_IWICImagingFactory, (void**)&factory);
        if (FAILED(hr)) { fail("CoCreateInstance(WICImagingFactory)"); break; }

        // Decode source file
        hr = factory->CreateDecoderFromFilename(wInPath.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) { fail("CreateDecoderFromFilename"); break; }

        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) { fail("GetFrame"); break; }

        // Convert to 32bpp BGRA so we always get 4 bytes per pixel
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) { fail("CreateFormatConverter"); break; }

        hr = converter->Initialize(frame,
                GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) { fail("FormatConverter::Initialize"); break; }

        // Create a writable in-memory bitmap so we can modify pixels
        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);

        hr = factory->CreateBitmapFromSource(converter,
                WICBitmapCacheOnDemand, &bitmap);
        if (FAILED(hr)) { fail("CreateBitmapFromSource"); break; }

        // Lock for read+write
        WICRect rect = { 0, 0, (INT)w, (INT)h };
        hr = bitmap->Lock(&rect, WICBitmapLockRead | WICBitmapLockWrite, &lock);
        if (FAILED(hr)) { fail("Bitmap::Lock"); break; }

        UINT bufSize = 0; BYTE* buf = nullptr;
        hr = lock->GetDataPointer(&bufSize, &buf);
        if (FAILED(hr)) { fail("GetDataPointer"); break; }

        ProcessPixels(w, h, buf, mode);

        lock->Release(); lock = nullptr;

        // Encode to PNG
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

    // Release in reverse order
    if (outFrame)   outFrame->Release();
    if (encoder)    encoder->Release();
    if (outStream)  outStream->Release();
    if (lock)       lock->Release();
    if (bitmap)     bitmap->Release();
    if (converter)  converter->Release();
    if (frame)      frame->Release();
    if (decoder)    decoder->Release();
    if (factory)    factory->Release();

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
    if (ImGui::Button("Icon Whitener..."))
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

    // Explanation
    ImGui::TextWrapped(
        "Map icons are tinted at draw time with a multiplicative color blend. "
        "This only looks correct when the icon's RGB is neutral gray — a "
        "colored image will tint unpredictably instead of cleanly turning "
        "red / orange / gray.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Pick an icon from the textures/ folder and a conversion mode, then "
        "press Convert. The result is saved as <name>_white.png next to the original.");
    ImGui::Separator();
    ImGui::Spacing();

    // Mode
    ImGui::TextUnformatted("Conversion mode:");
    ImGui::Spacing();

    int modeInt = (int)s_mode;
    if (ImGui::RadioButton("HSV Saturation  (colored images)", &modeInt, 0))
        s_mode = WhitenMode::HSVSaturation;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Zeros the Saturation channel in HSV space.\n"
                          "Use for full-color icons.");

    if (ImGui::RadioButton("Overlay         (already-gray images)", &modeInt, 1))
        s_mode = WhitenMode::Overlay;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Blends over a white layer using the Overlay formula,\n"
                          "lifting mid-tones toward white.\n"
                          "Use for icons that are already gray but a bit dull.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Icon dropdown
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

    // Convert button
    bool canConvert = (s_iconIndex > 0);
    if (!canConvert)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }

    if (ImGui::Button("Convert & Save##whitener_go"))
    {
        s_statusMessage = "";
        DoConvert(iconFiles[s_iconIndex - 1], s_mode);
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

    // Status
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