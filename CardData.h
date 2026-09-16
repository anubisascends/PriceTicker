/*
** CardData - CSV loading, the flat sequence-data store, and currency
** formatting for the PriceTicker effect.
**
** Deliberately free of Adobe SDK dependencies so the parsing/formatting
** logic is self-contained and unit-testable. The store is a plain-old-data
** struct so it can live directly in an effect sequence-data handle and be
** flattened/saved by simply copying the bytes.
*/

#ifndef PRICETICKER_CARDDATA_H
#define PRICETICKER_CARDDATA_H

#include <cstdint>
#include <cstddef>

#define PT_MAX_CARDS      512
#define PT_MAX_MARKERS    512
#define PT_MAX_NAME       64      // bytes, UTF-8, incl. null terminator
#define PT_MAX_PATH_LEN   1024    // wchar_t count, incl. null terminator
#define PT_CARDSTORE_VERSION  4   // v4: reveal 'kind' (card/subtract) + per-band sound paths

// Reveal kinds. A "card" reveal adds a card's price to the running total; a
// "subtract" reveal (marker named "[subtract]", amount in its comment) subtracts
// a fixed amount. Subtract reveals store the amount as a negative price so the
// running-total math (PT_TotalAtSeconds) needs no special case.
#define PT_REVEAL_CARD       0u
#define PT_REVEAL_SUBTRACT   1u

struct PT_Card {
    char   name[PT_MAX_NAME];
    double price;
};

// One scheduled reveal: a timeline marker joined to a card by name. The reveal
// time is 'timeSec' whole seconds (from H:M:S) plus 'frames' extra frames (from
// an HH:MM:SS:FF timecode); the frame part is converted to seconds at render
// time using the sequence frame rate. See PT_RevealSeconds().
struct PT_Reveal {
    double   timeSec;           // whole seconds (H*3600 + M*60 + S)
    double   frames;            // frame remainder from a timecode (0 for plain seconds)
    double   price;             // card reveal: card price (0 if unmatched).
                                // subtract reveal: NEGATIVE amount (e.g. -1.00)
    char     name[PT_MAX_NAME]; // marker name (== card name; "[subtract]" for a subtract)
    uint32_t matched;           // 1 if name matched a card (always 1 for subtract), else 0
    uint32_t kind;              // PT_REVEAL_CARD or PT_REVEAL_SUBTRACT
};

// Flat, POD, self-contained: safe to memcpy into/out of a sequence-data handle.
struct PT_CardStore {
    uint32_t  version;                       // == PT_CARDSTORE_VERSION
    uint32_t  count;                         // valid entries in cards[]
    uint32_t  markerCount;                   // valid entries in reveals[]
    wchar_t   csvPath[PT_MAX_PATH_LEN];      // card CSV, for reload after project load
    wchar_t   markersPath[PT_MAX_PATH_LEN];  // markers file, likewise
    wchar_t   soundLowPath[PT_MAX_PATH_LEN]; // WAV played when a Low-band card reveals ("" = none)
    wchar_t   soundMedPath[PT_MAX_PATH_LEN]; // WAV played when a Medium-band card reveals
    wchar_t   soundHighPath[PT_MAX_PATH_LEN];// WAV played when a High-band card reveals
    PT_Card   cards[PT_MAX_CARDS];
    PT_Reveal reveals[PT_MAX_MARKERS];       // sorted ascending by timeSec
};

// Zero the store and stamp the current version.
void PT_InitStore(PT_CardStore* store);

// Parse the CSV at 'path' into 'store' (replacing existing contents) and record
// the path. Returns true on success (at least one valid row parsed).
// CSV format: two columns "Name,Price" per line. A header row (non-numeric
// price) is skipped. Currency symbols, thousands separators and surrounding
// quotes/whitespace in the price are tolerated.
bool PT_LoadCardsFromCsv(const wchar_t* path, PT_CardStore* store);

// Re-parse the card CSV AND the markers file from the paths already stored (used
// after a project reload if the files are still present), then re-join them.
// No-op returning false if no card path is set.
bool PT_ReloadStore(PT_CardStore* store);

// Parse a markers file into 'store->reveals' (replacing existing) and record the
// path, then join to the loaded cards. Returns true if >=1 marker was parsed.
// Accepts BOTH Premiere's native "Export Markers" CSV (columns
// "Marker Name,Description,In,Out,..." with an HH:MM:SS:FF timecode in 'In') and
// a simple "Name,Seconds" file. Quoted names containing commas are handled.
//
// Subtract markers: a marker named "[subtract]" (case-insensitive) subtracts a
// fixed amount from the running total at its time. The amount is read from the
// marker's comment/description (the first non-time field after the name that
// parses as a number). Such a reveal is stored with kind == PT_REVEAL_SUBTRACT
// and a negative price; it is never joined to a card.
bool PT_LoadMarkersFromCsv(const wchar_t* path, PT_CardStore* store);

// Effective reveal time in seconds, given the sequence frame rate and a
// user offset (seconds to subtract, e.g. the effect clip's sequence start).
double PT_RevealSeconds(const PT_Reveal* rv, double fps, double offsetSec);

// (Re)resolve each reveal's price by matching its name to a card (case-
// insensitive) and sort the schedule ascending by time. Call after either the
// cards or the markers change.
void PT_ResolveReveals(PT_CardStore* store);

// Sum of the prices of every reveal whose effective time (see PT_RevealSeconds)
// is at or before 'nowSec'. If 'outCurrentIdx' is non-null it receives the index
// (into reveals[]) of the latest such reveal, or -1 if none have occurred yet.
double PT_TotalAtSeconds(const PT_CardStore* store, double nowSec,
                         double fps, double offsetSec, int* outCurrentIdx);

// Format 'value' as en-US currency, e.g. -1234.5 -> "-$1,234.50".
// Always writes a null-terminated string within [out, out+outSize).
void PT_FormatCurrency(double value, char* out, size_t outSize);

#endif // PRICETICKER_CARDDATA_H
