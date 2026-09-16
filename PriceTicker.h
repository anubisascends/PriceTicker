/*
** PriceTicker  -  Adobe Premiere Pro / After Effects video effect (C++ SDK)
**
** Drag onto a clip. Point it at a CSV of (CardName, Price). Keyframe the
** "Reveal Count" parameter (use HOLD keyframes) so it steps 0 -> 1 -> 2 ...
** at the frames where each card should appear. The effect draws a cumulative
** running total (and, optionally, the value of the most-recently-revealed card
** for a configurable number of frames) as a text overlay on the frame.
**
** Built on the AE-style effect API that Premiere hosts (PF_* / EffectMain),
** modelled on the SDK's GPUVideoFilter\SDK_ProcAmp and Vignette examples.
**
** Requires BOTH the Premiere Pro C++ SDK and the After Effects SDK on the
** include path (AE_SDK_BASE_PATH). See README.md.
*/

#ifndef PRICETICKER_H
#define PRICETICKER_H

#include "AEConfig.h"

#include "PrSDKTypes.h"
#include "AE_Effect.h"
#include "A.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHandlerTemplate.h"
#include "Param_Utils.h"

#include "PrSDKAESupport.h"
#include "PrSDKPixelFormat.h"
#include "PrSDKPixelFormatSuite.h"

#include "CardData.h"

/* Effect identity ---------------------------------------------------------- */
#define PRICETICKER_NAME          "Card Price Ticker"
#define PRICETICKER_MATCH_NAME    "SDK Card Price Ticker"
#define PRICETICKER_CATEGORY      "SDK"

#define MAJOR_VERSION   1
#define MINOR_VERSION   0
#define BUG_VERSION     0
#define STAGE_VERSION   PF_Stage_DEVELOP
#define BUILD_VERSION   0

/* Parameter indices -------------------------------------------------------- */
enum {
    PT_INPUT = 0,
    PT_CHOOSE_CSV,        // button: open file dialog, load the price CSV
    PT_CHOOSE_MARKERS,    // button: open file dialog, load the exported markers file.
                          //   Each marker (name + time) is joined to a card by name;
                          //   the running total is driven entirely by marker times,
                          //   so there are no keyframes to set. See export_markers.jsx.
    PT_MARKER_OFFSET,     // float slider: seconds to subtract from marker times, to
                          //   align sequence-time markers to the effect's clip start
    PT_DURATION,          // float slider: frames a freshly revealed card is shown
    PT_POS_X,             // float slider: horizontal position, % of frame width
    PT_POS_Y,             // float slider: vertical position, % of frame height
    PT_FONT,              // popup: font family used to render the overlay text
    PT_FONT_SIZE,         // float slider: font size in px (at full resolution)
    PT_TEXT_COLOR,        // color: fill color of the "Total: $X" line
    PT_CARD_VALUE_COLOR,  // color: fill color of the "+$value  Name" reveal line
                          //   (used when Price Thresholds are OFF)
    PT_SHOW_CARD_VALUE,   // checkbox: show the "+$value  Name" line during reveal window
    PT_SHOW_CARD_NAME,    // checkbox: include the card name on that line (else "+$value")

    /* --- v4: price thresholds, per-band sounds, subtract markers ----------- */
    PT_ENABLE_THRESHOLDS, // checkbox: color the reveal line by the card's price band
    PT_THRESH_LOW_MAX,    // float: price <= this  -> Low band
    PT_THRESH_MED_MAX,    // float: price <= this  -> Medium band; above -> High band
    PT_COLOR_LOW,         // color: reveal-line fill for a Low-band card
    PT_COLOR_MED,         // color: reveal-line fill for a Medium-band card
    PT_COLOR_HIGH,        // color: reveal-line fill for a High-band card
    PT_SUBTRACT_COLOR,    // color: reveal-line fill for a [subtract] marker
    PT_ENABLE_SOUND,      // checkbox: play a preview cue on reveal (interactive only)
    PT_CHOOSE_SOUND_LOW,  // button: pick the WAV played when a Low-band card reveals
    PT_CHOOSE_SOUND_MED,  // button: pick the WAV played when a Medium-band card reveals
    PT_CHOOSE_SOUND_HIGH, // button: pick the WAV played when a High-band card reveals
    PT_NUM_PARAMS
};

/* Parameter UI IDs (must be unique & stable across versions) --------------- */
enum {
    CHOOSE_CSV_DISK_ID = 1,
    CHOOSE_MARKERS_DISK_ID,
    MARKER_OFFSET_DISK_ID,
    DURATION_DISK_ID,
    POS_X_DISK_ID,
    POS_Y_DISK_ID,
    FONT_DISK_ID,
    FONT_SIZE_DISK_ID,
    TEXT_COLOR_DISK_ID,
    SHOW_CARD_VALUE_DISK_ID,
    CARD_VALUE_COLOR_DISK_ID,
    SHOW_CARD_NAME_DISK_ID,
    /* v4 additions — append only; never reorder/reuse the IDs above. */
    ENABLE_THRESHOLDS_DISK_ID,
    THRESH_LOW_MAX_DISK_ID,
    THRESH_MED_MAX_DISK_ID,
    COLOR_LOW_DISK_ID,
    COLOR_MED_DISK_ID,
    COLOR_HIGH_DISK_ID,
    SUBTRACT_COLOR_DISK_ID,
    ENABLE_SOUND_DISK_ID,
    CHOOSE_SOUND_LOW_DISK_ID,
    CHOOSE_SOUND_MED_DISK_ID,
    CHOOSE_SOUND_HIGH_DISK_ID
};

/* Parameter ranges / defaults --------------------------------------------- */
// Seconds subtracted from every marker time. Use it when markers are exported in
// sequence time but the effect's clip starts later in the timeline: set it to the
// clip's start time. 0 = markers already start at the clip's first frame.
#define OFFSET_MIN       -36000.0    // -10 h
#define OFFSET_MAX        36000.0    //  10 h
#define OFFSET_SLIDER     600.0      // soft slider extent (10 min); type beyond it
#define OFFSET_DFLT       0.0

#define DURATION_MIN      1.0
#define DURATION_MAX      600.0
#define DURATION_DFLT     20.0

// Position is the TOP-LEFT corner of the text block (text grows right & down).
#define POS_X_DFLT        6.0      // just in from the left edge
#define POS_Y_DFLT        78.0     // lower third, leaving room to grow downward

#define FONT_SIZE_MIN     8.0
#define FONT_SIZE_MAX     400.0
#define FONT_SIZE_DFLT    72.0

/* Price-threshold band boundaries (in dollars). A card whose price is
** <= LOW_MAX is "Low", <= MED_MAX is "Medium", otherwise "High". Wide max so
** high-value cards (e.g. a $25,000 Black Lotus) still fit; type past the slider. */
#define THRESH_MIN         0.0
#define THRESH_MAX         1000000.0
#define THRESH_SLIDER_MAX  100.0     // soft slider extent; type beyond it
#define THRESH_LOW_DFLT    1.00
#define THRESH_MED_DFLT    10.00

/* Font popup. The menu string order MUST match PT_kFontFamilies[] in the .cpp.
** All families ship with Windows; DirectWrite falls back gracefully if one is
** ever missing. FONT_DFLT is the 1-based index of the default entry. */
#define FONT_MENU_STR \
    "Segoe UI|Arial|Arial Black|Verdana|Tahoma|Times New Roman|" \
    "Georgia|Trebuchet MS|Courier New|Consolas|Impact|Comic Sans MS"
#define FONT_COUNT        12
#define FONT_DFLT         1        // Segoe UI

/* out_flags used by GlobalSetup. These MUST match the numeric values baked
** into PriceTicker.r (see the AE_Effect_Global_OutFlags block there):
**   PF_OutFlag_SEQUENCE_DATA_NEEDS_FLATTENING (1<<4 = 0x10)
** + PF_OutFlag_USE_OUTPUT_EXTENT             (1<<6 = 0x40)
** = 0x50.  If Premiere/AE warns about a flag mismatch on first load, update
** the .r numbers to match whatever GlobalSetup sets here. */

#ifdef MSWindows
#define DllExport   __declspec( dllexport )
#else
#define DllExport   __attribute__((visibility("default")))
#endif

extern "C" DllExport PF_Err EffectMain(
    PF_Cmd       inCmd,
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* inOutput,
    void*        extra);

#endif // PRICETICKER_H
