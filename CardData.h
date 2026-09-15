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
#define PT_CARDSTORE_VERSION  3   // v3: reveal stores a frame remainder for timecode

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
    double   price;             // resolved from the card CSV (0 if unmatched)
    char     name[PT_MAX_NAME]; // marker name (== card name)
    uint32_t matched;           // 1 if name matched a card, else 0
};

// Flat, POD, self-contained: safe to memcpy into/out of a sequence-data handle.
struct PT_CardStore {
    uint32_t  version;                       // == PT_CARDSTORE_VERSION
    uint32_t  count;                         // valid entries in cards[]
    uint32_t  markerCount;                   // valid entries in reveals[]
    wchar_t   csvPath[PT_MAX_PATH_LEN];      // card CSV, for reload after project load
    wchar_t   markersPath[PT_MAX_PATH_LEN];  // markers file, likewise
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
