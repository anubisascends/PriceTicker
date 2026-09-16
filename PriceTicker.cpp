#include "PriceTicker.h"

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>          // PlaySoundW (preview audio cue)
#pragma comment(lib, "winmm.lib")

#include "TextRenderer.h"

#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>

/* Font family names, in the same order as FONT_MENU_STR in PriceTicker.h.
** The popup value is 1-based, so index with (value - 1). */
static const wchar_t* const PT_kFontFamilies[FONT_COUNT] = {
    L"Segoe UI", L"Arial", L"Arial Black", L"Verdana", L"Tahoma",
    L"Times New Roman", L"Georgia", L"Trebuchet MS", L"Courier New",
    L"Consolas", L"Impact", L"Comic Sans MS"
};

// Map a 1-based popup value to a font family name (clamped to the valid range).
static const wchar_t* PT_FontFamilyFromPopup(A_long value)
{
    int idx = (int)value - 1;
    if (idx < 0 || idx >= FONT_COUNT) idx = FONT_DFLT - 1;
    return PT_kFontFamilies[idx];
}

/* ========================================================================= */
/*  Sequence data helpers (the CSV store lives in the effect sequence data)   */
/* ========================================================================= */

// Lock the effect's sequence-data handle and return the store, or nullptr.
static PT_CardStore* LockStore(PF_InData* in_data)
{
    if (!in_data->sequence_data) return nullptr;
    return reinterpret_cast<PT_CardStore*>(PF_LOCK_HANDLE(in_data->sequence_data));
}

static void UnlockStore(PF_InData* in_data)
{
    if (in_data->sequence_data)
        PF_UNLOCK_HANDLE(in_data->sequence_data);
}

static PF_Err SequenceSetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_Err err = PF_Err_NONE;

    PF_Handle h = PF_NEW_HANDLE(sizeof(PT_CardStore));
    if (!h) return PF_Err_OUT_OF_MEMORY;

    PT_CardStore* store = reinterpret_cast<PT_CardStore*>(PF_LOCK_HANDLE(h));
    if (store) {
        PT_InitStore(store);
        PF_UNLOCK_HANDLE(h);
    }
    out_data->sequence_data = h;
    return err;
}

static PF_Err SequenceResetup(PF_InData* in_data, PF_OutData* out_data)
{
    // A fresh handle we own, seeded from whatever the host restored.
    PF_Handle h = PF_NEW_HANDLE(sizeof(PT_CardStore));
    if (!h) return PF_Err_OUT_OF_MEMORY;

    PT_CardStore* dst = reinterpret_cast<PT_CardStore*>(PF_LOCK_HANDLE(h));
    if (!dst) return PF_Err_OUT_OF_MEMORY;
    PT_InitStore(dst);

    if (in_data->sequence_data) {
        PT_CardStore* src =
            reinterpret_cast<PT_CardStore*>(PF_LOCK_HANDLE(in_data->sequence_data));
        if (src && src->version == PT_CARDSTORE_VERSION) {
            std::memcpy(dst, src, sizeof(PT_CardStore));
        }
        if (src) PF_UNLOCK_HANDLE(in_data->sequence_data);
    }

    // If the file is still on disk, refresh from it (picks up CSV edits).
    if (dst->csvPath[0] != L'\0')
        PT_ReloadStore(dst);

    PF_UNLOCK_HANDLE(h);
    out_data->sequence_data = h;
    return PF_Err_NONE;
}

static PF_Err SequenceFlatten(PF_InData* in_data, PF_OutData* out_data)
{
    // Store is already POD/flat; hand the host an owned copy.
    if (!in_data->sequence_data) return PF_Err_NONE;

    PF_Handle h = PF_NEW_HANDLE(sizeof(PT_CardStore));
    if (!h) return PF_Err_OUT_OF_MEMORY;

    PT_CardStore* dst = reinterpret_cast<PT_CardStore*>(PF_LOCK_HANDLE(h));
    PT_CardStore* src =
        reinterpret_cast<PT_CardStore*>(PF_LOCK_HANDLE(in_data->sequence_data));
    if (dst && src)
        std::memcpy(dst, src, sizeof(PT_CardStore));
    if (src) PF_UNLOCK_HANDLE(in_data->sequence_data);
    if (dst) PF_UNLOCK_HANDLE(h);

    out_data->sequence_data = h;
    return PF_Err_NONE;
}

static PF_Err SequenceSetdown(PF_InData* in_data, PF_OutData* out_data)
{
    if (in_data->sequence_data)
        PF_DISPOSE_HANDLE(in_data->sequence_data);
    out_data->sequence_data = nullptr;
    return PF_Err_NONE;
}

/* ========================================================================= */
/*  Global setup / params                                                     */
/* ========================================================================= */

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data)
{
    out_data->my_version =
        PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);

    // We keep the CSV in sequence data and want it saved with the project.
    out_data->out_flags  |= PF_OutFlag_SEQUENCE_DATA_NEEDS_FLATTENING |
                            PF_OutFlag_USE_OUTPUT_EXTENT;   // 0x10 | 0x40 = 0x50 (see .r)

    if (in_data->appl_id == 'PrMr') {
        // BGRA float lets us composite DirectWrite's premultiplied BGRA output
        // with no color-space conversion.
        AEFX_SuiteScoper<PF_PixelFormatSuite1> pixelFormatSuite(
            in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
        (*pixelFormatSuite->ClearSupportedPixelFormats)(in_data->effect_ref);
        (*pixelFormatSuite->AddSupportedPixelFormat)(
            in_data->effect_ref, PrPixelFormat_BGRA_4444_32f);
    }

    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_ParamDef def;

    // 1) Choose CSV button
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Card List", "Choose CSV\xE2\x80\xA6",
                  0, PF_ParamFlag_SUPERVISE, CHOOSE_CSV_DISK_ID);

    // 2) Choose Markers button: load the exported timeline markers. Marker times
    //    drive the reveal schedule, so no keyframing is needed.
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Reveal Markers", "Choose Markers\xE2\x80\xA6",
                  0, PF_ParamFlag_SUPERVISE, CHOOSE_MARKERS_DISK_ID);

    // 3) Marker Time Offset (seconds) - align sequence-time markers to clip start.
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Marker Time Offset (s)",
                         OFFSET_MIN, OFFSET_MAX, -OFFSET_SLIDER, OFFSET_SLIDER,
                         OFFSET_DFLT, PF_Precision_HUNDREDTHS, 0, 0,
                         MARKER_OFFSET_DISK_ID);

    // 4) Card Display Duration (frames)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Card Display (frames)",
                         DURATION_MIN, DURATION_MAX, DURATION_MIN, 120.0,
                         DURATION_DFLT, PF_Precision_INTEGER, 0, 0,
                         DURATION_DISK_ID);

    // 5) Position X (% of width)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Position X (%)",
                         0.0, 100.0, 0.0, 100.0,
                         POS_X_DFLT, PF_Precision_TENTHS, 0, 0,
                         POS_X_DISK_ID);

    // 6) Position Y (% of height)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Position Y (%)",
                         0.0, 100.0, 0.0, 100.0,
                         POS_Y_DFLT, PF_Precision_TENTHS, 0, 0,
                         POS_Y_DISK_ID);

    // 7) Font family
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Font", FONT_COUNT, FONT_DFLT, FONT_MENU_STR, FONT_DISK_ID);

    // 8) Font Size (px)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Font Size (px)",
                         FONT_SIZE_MIN, FONT_SIZE_MAX, FONT_SIZE_MIN, 200.0,
                         FONT_SIZE_DFLT, PF_Precision_INTEGER, 0, 0,
                         FONT_SIZE_DISK_ID);

    // 9) Text Color (the "Total: $X" line)
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Text Color", 255, 255, 255, TEXT_COLOR_DISK_ID);

    // 10) Card Value Color (the "+$value  Name" reveal line)
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Card Value Color", 255, 205, 70, CARD_VALUE_COLOR_DISK_ID);

    // 11) Show Card Value during reveal window
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Show Card Value", "", TRUE, 0, SHOW_CARD_VALUE_DISK_ID);

    // 12) Show Card Name on the reveal line
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Show Card Name", "", TRUE, 0, SHOW_CARD_NAME_DISK_ID);

    // 13) Enable Price Thresholds — color the reveal line by the card's price band.
    //     When off, the single "Card Value Color" above is used (as before).
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Price Thresholds", "", TRUE, 0, ENABLE_THRESHOLDS_DISK_ID);

    // 14) Low band upper bound (price <= this -> Low)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Low if price \xE2\x89\xA4 ($)",
                         THRESH_MIN, THRESH_MAX, THRESH_MIN, THRESH_SLIDER_MAX,
                         THRESH_LOW_DFLT, PF_Precision_HUNDREDTHS, 0, 0,
                         THRESH_LOW_MAX_DISK_ID);

    // 15) Medium band upper bound (price <= this -> Medium; above -> High)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Medium if price \xE2\x89\xA4 ($)",
                         THRESH_MIN, THRESH_MAX, THRESH_MIN, THRESH_SLIDER_MAX,
                         THRESH_MED_DFLT, PF_Precision_HUNDREDTHS, 0, 0,
                         THRESH_MED_MAX_DISK_ID);

    // 16) Low band color
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Low Color", 150, 150, 150, COLOR_LOW_DISK_ID);

    // 17) Medium band color
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Medium Color", 255, 205, 70, COLOR_MED_DISK_ID);

    // 18) High band color
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("High Color", 90, 235, 120, COLOR_HIGH_DISK_ID);

    // 19) Subtract marker color (the "-$amount" reveal line)
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Subtract Color", 240, 90, 90, SUBTRACT_COLOR_DISK_ID);

    // 20) Enable preview sound — plays a WAV cue on reveal during interactive
    //     editing only (never reaches the export; turn OFF before exporting).
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Preview Sound", "", TRUE, 0, ENABLE_SOUND_DISK_ID);

    // 21) Choose the Low-band reveal sound
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Low Sound", "Choose WAV\xE2\x80\xA6",
                  0, PF_ParamFlag_SUPERVISE, CHOOSE_SOUND_LOW_DISK_ID);

    // 22) Choose the Medium-band reveal sound
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("Medium Sound", "Choose WAV\xE2\x80\xA6",
                  0, PF_ParamFlag_SUPERVISE, CHOOSE_SOUND_MED_DISK_ID);

    // 23) Choose the High-band reveal sound
    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON("High Sound", "Choose WAV\xE2\x80\xA6",
                  0, PF_ParamFlag_SUPERVISE, CHOOSE_SOUND_HIGH_DISK_ID);

    out_data->num_params = PT_NUM_PARAMS;
    return PF_Err_NONE;
}

/* ========================================================================= */
/*  Button handling: open a file dialog and load the CSV / markers            */
/* ========================================================================= */

static PF_Err UserChangedParam(
    PF_InData*                    in_data,
    PF_OutData*                   out_data,
    PF_ParamDef*                  params[],
    const PF_UserChangedParamExtra* extra)
{
    const A_long idx = extra->param_index;
    bool isCsv       = (idx == PT_CHOOSE_CSV);
    bool isMarkers   = (idx == PT_CHOOSE_MARKERS);
    bool isSoundLow  = (idx == PT_CHOOSE_SOUND_LOW);
    bool isSoundMed  = (idx == PT_CHOOSE_SOUND_MED);
    bool isSoundHigh = (idx == PT_CHOOSE_SOUND_HIGH);
    bool isSound     = isSoundLow || isSoundMed || isSoundHigh;
    if (!isCsv && !isMarkers && !isSound)
        return PF_Err_NONE;

    wchar_t fileBuf[PT_MAX_PATH_LEN];
    fileBuf[0] = L'\0';

    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    ofn.lpstrFilter = isSound ? L"WAV Audio\0*.wav\0All Files\0*.*\0"
                              : L"CSV Files\0*.csv\0All Files\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = PT_MAX_PATH_LEN;
    ofn.lpstrTitle  = isCsv     ? L"Select card price CSV"
                    : isMarkers ? L"Select exported markers file"
                    : isSoundLow  ? L"Select WAV for Low-band reveals"
                    : isSoundMed  ? L"Select WAV for Medium-band reveals"
                                  : L"Select WAV for High-band reveals";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn))
        return PF_Err_NONE;                 // user cancelled

    PT_CardStore* store = LockStore(in_data);
    if (store) {
        if (isCsv)              PT_LoadCardsFromCsv(fileBuf, store);
        else if (isMarkers)     PT_LoadMarkersFromCsv(fileBuf, store);
        else {
            // Record the chosen sound path in the (persisted) store.
            wchar_t* dst = isSoundLow ? store->soundLowPath
                         : isSoundMed ? store->soundMedPath
                                      : store->soundHighPath;
            std::wcsncpy(dst, fileBuf, PT_MAX_PATH_LEN - 1);
            dst[PT_MAX_PATH_LEN - 1] = L'\0';
        }
        UnlockStore(in_data);
        out_data->out_flags |= PF_OutFlag_FORCE_RERENDER | PF_OutFlag_REFRESH_UI;
    }
    return PF_Err_NONE;
}

/* ========================================================================= */
/*  Render                                                                    */
/* ========================================================================= */

// Composite a premultiplied-BGRA text bitmap over a BGRA_4444_32f frame.
static void CompositeText(PF_LayerDef* dest, const PT_TextBitmap* txt,
                          int topLeftX, int topLeftY)
{
    for (int ty = 0; ty < txt->height; ++ty) {
        int fy = topLeftY + ty;
        if (fy < 0 || fy >= dest->height) continue;

        const unsigned char* srcRow = txt->pixels + (size_t)ty * txt->width * 4;
        float* dstRow = (float*)((char*)dest->data + (size_t)fy * dest->rowbytes);

        for (int tx = 0; tx < txt->width; ++tx) {
            int fx = topLeftX + tx;
            if (fx < 0 || fx >= dest->width) continue;

            const unsigned char* s = srcRow + tx * 4;   // B,G,R,A (premultiplied)
            float tb = s[0] / 255.0f;
            float tg = s[1] / 255.0f;
            float tr = s[2] / 255.0f;
            float ta = s[3] / 255.0f;
            if (ta <= 0.0f) continue;                   // nothing to draw

            float* d = dstRow + fx * 4;                 // B,G,R,A float
            float inv = 1.0f - ta;
            d[0] = tb + d[0] * inv;
            d[1] = tg + d[1] * inv;
            d[2] = tr + d[2] * inv;
            d[3] = ta + d[3] * inv;
        }
    }
}

// Rasterize one UTF-8 line in color (r,g,b) and composite it so the text's
// top-left ink lands at (inkX, inkY) pixels. Returns the line's ink height so
// the caller can stack the next line directly below it; 0 on empty/failure.
static float DrawLine(PF_LayerDef* output, const std::string& utf8,
                      const wchar_t* fontFamily, float fontPx,
                      float r, float g, float b,
                      float inkX, float inkY)
{
    if (utf8.empty()) return 0.0f;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return 0.0f;

    std::wstring wtext(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wtext[0], wlen);

    PT_TextBitmap bmp = {};
    if (!PT_RenderText(wtext.c_str(), fontFamily, fontPx, r, g, b, 1.0f, &bmp))
        return 0.0f;

    int topLeftX = (int)std::floor(inkX + 0.5f) - bmp.originX;
    int topLeftY = (int)std::floor(inkY + 0.5f) - bmp.originY;
    CompositeText(output, &bmp, topLeftX, topLeftY);

    float inkH = (float)(bmp.height - 2 * bmp.originY);   // one line's height
    PT_FreeTextBitmap(&bmp);
    return inkH;
}

// Price band for a card: 0 = Low (price <= lowMax), 1 = Medium (<= medMax),
// 2 = High (above medMax). Boundaries are inclusive of the band's upper bound.
static int PT_BandForPrice(double price, double lowMax, double medMax)
{
    if (price <= lowMax) return 0;
    if (price <= medMax) return 1;
    return 2;
}

// Best-effort preview cue: play 'path' asynchronously through the Windows audio
// device. INTERACTIVE ONLY by nature — a Premiere video effect has no audio
// output path, so this cannot reach the export; it just gives the editor an
// audible marker while scrubbing/previewing. Debounced so repeated renders of
// the same reveal (scrub jitter, re-renders of one frame) don't machine-gun it.
static void PT_PlayRevealCue(const wchar_t* path, int revealIdx)
{
    static int   s_lastIdx  = -1;
    static DWORD s_lastTick = 0;

    if (!path || path[0] == L'\0') return;

    DWORD now = GetTickCount();
    if (revealIdx == s_lastIdx && (now - s_lastTick) < 750)
        return;                             // same reveal, too soon -> skip

    s_lastIdx  = revealIdx;
    s_lastTick = now;
    PlaySoundW(path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

static PF_Err Render(
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;

    // Non-Premiere hosts: just pass the frame through unchanged.
    if (in_data->appl_id != 'PrMr') {
        return PF_COPY(&params[PT_INPUT]->u.ld, output, NULL, NULL);
    }

    PF_LayerDef* src = &params[PT_INPUT]->u.ld;

    // Start with a straight copy of the source frame.
    {
        const char* srcData = (const char*)src->data;
        char* destData = (char*)output->data;
        size_t rowCopy = (size_t)output->width * 4 * sizeof(float);
        for (int y = 0; y < output->height; ++y,
             srcData += src->rowbytes, destData += output->rowbytes) {
            std::memcpy(destData, srcData, rowCopy);
        }
    }

    // --- Read parameters ---------------------------------------------------
    double offsetSec   = params[PT_MARKER_OFFSET]->u.fs_d.value;
    float durationF    = (float)params[PT_DURATION]->u.fs_d.value;
    float posXpct      = (float)params[PT_POS_X]->u.fs_d.value;
    float posYpct      = (float)params[PT_POS_Y]->u.fs_d.value;
    float fontSizeBase = (float)params[PT_FONT_SIZE]->u.fs_d.value;
    bool  showCard     = params[PT_SHOW_CARD_VALUE]->u.bd.value != 0;
    bool  showCardName = params[PT_SHOW_CARD_NAME]->u.bd.value != 0;

    const wchar_t* fontFamily = PT_FontFamilyFromPopup(params[PT_FONT]->u.pd.value);

    PF_Pixel color = params[PT_TEXT_COLOR]->u.cd.value;
    float cr = color.red   / 255.0f;
    float cg = color.green / 255.0f;
    float cb = color.blue  / 255.0f;

    // Reveal-line color: defaults to the single Card Value Color, overridden
    // per-reveal below by the price-threshold band or the subtract color.
    PF_Pixel vcolor = params[PT_CARD_VALUE_COLOR]->u.cd.value;   // reveal line
    float vr = vcolor.red   / 255.0f;
    float vg = vcolor.green / 255.0f;
    float vb = vcolor.blue  / 255.0f;

    // v4: thresholds, per-band colors/sounds, subtract color.
    bool   enableThresholds = params[PT_ENABLE_THRESHOLDS]->u.bd.value != 0;
    bool   enableSound      = params[PT_ENABLE_SOUND]->u.bd.value != 0;
    double lowMax           = params[PT_THRESH_LOW_MAX]->u.fs_d.value;
    double medMax           = params[PT_THRESH_MED_MAX]->u.fs_d.value;

    PF_Pixel bandCol[3] = {
        params[PT_COLOR_LOW]->u.cd.value,
        params[PT_COLOR_MED]->u.cd.value,
        params[PT_COLOR_HIGH]->u.cd.value,
    };
    PF_Pixel subCol = params[PT_SUBTRACT_COLOR]->u.cd.value;

    // Scale font to the current (possibly reduced) preview resolution.
    float dsy = (in_data->downsample_y.den != 0)
                ? (float)in_data->downsample_y.num / in_data->downsample_y.den : 1.0f;
    float fontPx = fontSizeBase * dsy;
    if (fontPx < 1.0f) fontPx = 1.0f;

    // --- Total from the marker schedule up to this frame -------------------
    // current_time is in units where time_scale == units/second; time_step is
    // one frame. Convert both to seconds to match the (seconds-based) schedule.
    double nowSec = (in_data->time_scale != 0)
                    ? (double)in_data->current_time / (double)in_data->time_scale
                    : 0.0;
    double fps = (in_data->time_step != 0)
                 ? (double)in_data->time_scale / (double)in_data->time_step
                 : 0.0;

    PT_CardStore* store = LockStore(in_data);

    int    curIdx = -1;
    double total  = PT_TotalAtSeconds(store, nowSec, fps, offsetSec, &curIdx);

    // --- Build the two overlay lines (UTF-8) -------------------------------
    char totalStr[64];
    PT_FormatCurrency(total, totalStr, sizeof(totalStr));

    std::string totalLine = "Total: ";
    totalLine += totalStr;

    // Flash the most-recently-revealed reveal's line for 'durationF' frames after
    // its marker time. A card reveal shows "+value[  Name]" colored by its price
    // band (or the single Card Value Color when thresholds are off); a subtract
    // reveal shows "-amount" in the subtract color. Also arm the preview cue.
    std::string cardLine;
    wchar_t     cuePath[PT_MAX_PATH_LEN];
    cuePath[0] = L'\0';
    int  cueIdx  = -1;
    bool wantCue = false;

    if (store && curIdx >= 0) {
        const PT_Reveal* rv = &store->reveals[curIdx];
        double framesSince = (nowSec - PT_RevealSeconds(rv, fps, offsetSec)) * fps;
        bool   inWindow    = framesSince < (double)durationF;

        int band = (rv->kind == PT_REVEAL_CARD)
                   ? PT_BandForPrice(rv->price, lowMax, medMax) : 0;

        // Pick the reveal-line color for this reveal.
        if (rv->kind == PT_REVEAL_SUBTRACT) {
            vr = subCol.red / 255.0f; vg = subCol.green / 255.0f; vb = subCol.blue / 255.0f;
        } else if (enableThresholds) {
            const PF_Pixel& c = bandCol[band];
            vr = c.red / 255.0f; vg = c.green / 255.0f; vb = c.blue / 255.0f;
        }

        if (showCard && inWindow) {
            char valStr[64];
            if (rv->kind == PT_REVEAL_SUBTRACT) {
                // price is stored negative; show the positive amount with a "-".
                PT_FormatCurrency(-rv->price, valStr, sizeof(valStr));
                cardLine = "-";
                cardLine += valStr;
            } else {
                PT_FormatCurrency(rv->price, valStr, sizeof(valStr));
                cardLine = "+";
                cardLine += valStr;
                if (showCardName) {
                    cardLine += "  ";
                    cardLine += rv->name;
                    if (!rv->matched)
                        cardLine += "  (?)";   // marker name didn't match any card
                }
            }
        }

        // Arm the preview cue for a *card* reveal at its onset (~1 frame window).
        // Copy the band's WAV path out while the store is still locked.
        if (enableSound && rv->kind == PT_REVEAL_CARD &&
            framesSince >= 0.0 && framesSince < 1.5) {
            const wchar_t* p = (band == 0) ? store->soundLowPath
                             : (band == 1) ? store->soundMedPath
                                           : store->soundHighPath;
            if (p[0] != L'\0') {
                std::wcsncpy(cuePath, p, PT_MAX_PATH_LEN - 1);
                cuePath[PT_MAX_PATH_LEN - 1] = L'\0';
                cueIdx  = curIdx;
                wantCue = true;
            }
        }
    }

    if (store) UnlockStore(in_data);

    // Fire the preview cue after unlocking (interactive editing only; see helper).
    if (wantCue)
        PT_PlayRevealCue(cuePath, cueIdx);

    // --- Rasterize + composite each line in its own color ------------------
    // Anchor the top-left ink at (posX, posY); the total line stays put and the
    // reveal line stacks directly beneath it in its own color.
    float inkX = posXpct / 100.0f * output->width;
    float inkY = posYpct / 100.0f * output->height;

    float totalH = DrawLine(output, totalLine, fontFamily, fontPx,
                            cr, cg, cb, inkX, inkY);
    DrawLine(output, cardLine, fontFamily, fontPx,
             vr, vg, vb, inkX, inkY + totalH);

    return err;
}

/* ========================================================================= */
/*  Entry point                                                               */
/* ========================================================================= */

extern "C" DllExport PF_Err EffectMain(
    PF_Cmd       inCmd,
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* inOutput,
    void*        extra)
{
    PF_Err err = PF_Err_NONE;

    try {
        switch (inCmd) {
        case PF_Cmd_GLOBAL_SETUP:
            err = GlobalSetup(in_data, out_data);
            break;
        case PF_Cmd_PARAMS_SETUP:
            err = ParamsSetup(in_data, out_data);
            break;
        case PF_Cmd_SEQUENCE_SETUP:
            err = SequenceSetup(in_data, out_data);
            break;
        case PF_Cmd_SEQUENCE_RESETUP:
            err = SequenceResetup(in_data, out_data);
            break;
        case PF_Cmd_SEQUENCE_FLATTEN:
            err = SequenceFlatten(in_data, out_data);
            break;
        case PF_Cmd_SEQUENCE_SETDOWN:
            err = SequenceSetdown(in_data, out_data);
            break;
        case PF_Cmd_USER_CHANGED_PARAM:
            err = UserChangedParam(in_data, out_data, params,
                                   reinterpret_cast<const PF_UserChangedParamExtra*>(extra));
            break;
        case PF_Cmd_RENDER:
            err = Render(in_data, out_data, params, inOutput);
            break;
        }
    } catch (PF_Err& thrown) {
        err = thrown;
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    return err;
}
