#include "CardData.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cwchar>

/* ------------------------------------------------------------------------- */

void PT_InitStore(PT_CardStore* store)
{
    if (!store) return;
    std::memset(store, 0, sizeof(PT_CardStore));
    store->version = PT_CARDSTORE_VERSION;
    store->count = 0;
}

/* --- small parsing helpers ----------------------------------------------- */

static char* pt_trim(char* s)
{
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' ||
           *s == '"' || *s == '\'')
        ++s;
    size_t len = std::strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == '"' || c == '\'')
            s[--len] = '\0';
        else
            break;
    }
    return s;
}

// Split a CSV line into fields in-place, honoring double-quoted fields (which
// may contain commas) and "" as an escaped quote. Returns the field count;
// fills 'fields' with pointers to null-terminated, un-quoted values (each still
// needing pt_trim for surrounding whitespace). Stops at 'maxFields'.
static int pt_split_csv(char* line, char* fields[], int maxFields)
{
    int n = 0;
    char* p = line;
    while (n < maxFields) {
        // Skip leading spaces/tabs so a quote after them still counts as quoted.
        while (*p == ' ' || *p == '\t') ++p;

        char* out = p;         // write cursor (we unescape in place)
        fields[n++] = out;

        if (*p == '"') {
            ++p;               // consume opening quote
            while (*p) {
                if (*p == '"') {
                    if (p[1] == '"') { *out++ = '"'; p += 2; }   // escaped quote
                    else { ++p; break; }                          // closing quote
                } else {
                    *out++ = *p++;
                }
            }
            // Skip to the next comma (past any trailing spaces / stray chars).
            while (*p && *p != ',') ++p;
        } else {
            while (*p && *p != ',' && *p != '\r' && *p != '\n') *out++ = *p++;
        }

        bool atComma = (*p == ',');
        *out = '\0';
        if (!atComma) break;   // end of line
        ++p;                   // consume the comma, on to the next field
    }
    return n;
}

// Parse a money-ish token ("$4,200.50", "4200.5", "(1,234)") into a double.
// Returns true only if at least one digit was present.
static bool pt_parse_price(const char* token, double* out)
{
    char clean[64];
    size_t j = 0;
    bool sawDigit = false;
    bool negative = false;

    for (size_t i = 0; token[i] != '\0' && j + 1 < sizeof(clean); ++i) {
        char c = token[i];
        if (c == '-' || c == '(') {
            negative = true;
        } else if ((c >= '0' && c <= '9') || c == '.') {
            if (c >= '0' && c <= '9') sawDigit = true;
            clean[j++] = c;
        }
        // commas, currency symbols, spaces, ')' etc. are ignored
    }
    clean[j] = '\0';

    if (!sawDigit) return false;

    double v = std::strtod(clean, nullptr);
    if (negative) v = -v;
    *out = v;
    return true;
}

/* ------------------------------------------------------------------------- */

bool PT_LoadCardsFromCsv(const wchar_t* path, PT_CardStore* store)
{
    if (!store || !path || path[0] == L'\0') return false;

    FILE* fp = _wfopen(path, L"rb");
    if (!fp) return false;

    // Reset entries but remember the path we loaded from.
    store->version = PT_CARDSTORE_VERSION;
    store->count = 0;
    std::memset(store->cards, 0, sizeof(store->cards));

    char line[1024];
    bool first = true;

    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        char* p = line;

        // Skip a UTF-8 BOM on the very first line.
        if (first) {
            first = false;
            if ((unsigned char)p[0] == 0xEF &&
                (unsigned char)p[1] == 0xBB &&
                (unsigned char)p[2] == 0xBF)
                p += 3;
        }

        // Split into fields (quote-aware, so "Yorion, Sky Nomad" stays intact).
        char* fields[8];
        int nf = pt_split_csv(p, fields, 8);
        if (nf < 2) continue;                 // need at least name + price

        char* name = pt_trim(fields[0]);
        char* priceTok = pt_trim(fields[1]);

        double price = 0.0;
        if (!pt_parse_price(priceTok, &price))
            continue;                         // header row or malformed -> skip

        if (name[0] == '\0')
            continue;                         // no name -> skip

        PT_Card* card = &store->cards[store->count];
        std::strncpy(card->name, name, PT_MAX_NAME - 1);
        card->name[PT_MAX_NAME - 1] = '\0';
        card->price = price;

        if (++store->count >= PT_MAX_CARDS)
            break;                            // capacity reached
    }

    std::fclose(fp);

    // Record the path regardless, so a later reload can find the file.
    std::wcsncpy(store->csvPath, path, PT_MAX_PATH_LEN - 1);
    store->csvPath[PT_MAX_PATH_LEN - 1] = L'\0';

    // Card prices changed: re-join any already-loaded markers to them.
    PT_ResolveReveals(store);

    return store->count > 0;
}

bool PT_ReloadStore(PT_CardStore* store)
{
    if (!store || store->csvPath[0] == L'\0') return false;
    bool ok = PT_LoadCardsFromCsv(store->csvPath, store);
    // PT_LoadCardsFromCsv clears reveals-independent state but keeps the marker
    // path; refresh the schedule from disk too, then re-join.
    if (store->markersPath[0] != L'\0')
        PT_LoadMarkersFromCsv(store->markersPath, store);
    else
        PT_ResolveReveals(store);
    return ok;
}

/* --- markers + join ------------------------------------------------------- */

// Case-insensitive ASCII compare of two UTF-8 names (byte-wise; fine for the
// card names in use). Returns true if equal.
static bool pt_ieq(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

// True if 'name' is the subtract directive "[subtract]" (case-insensitive).
// 'name' is expected already trimmed of surrounding whitespace/quotes.
static bool pt_is_subtract(const char* name)
{
    return pt_ieq(name, "[subtract]");
}

// Parse a time token into (whole seconds, extra frames). Accepts:
//   plain seconds     "5", "5.5"              -> sec=value,          frames=0
//   M:S               "1:05"                  -> sec=65,             frames=0
//   H:M:S(.f)         "00:01:05", "0:1:5.5"   -> sec=65(.5),         frames=0
//   H:M:S:F  timecode "00:02:23:13"           -> sec=143,            frames=13
// ':' and ';' (drop-frame) are both accepted as separators. The frame field is
// kept separate because converting it to seconds needs the sequence frame rate,
// which isn't known until render. Returns true if a value was parsed.
static bool pt_parse_time(const char* token, double* outSec, double* outFrames)
{
    *outSec = 0.0;
    *outFrames = 0.0;

    // Split on ':' or ';' into up to 4 numeric parts.
    double parts[4] = { 0, 0, 0, 0 };
    int  n = 0;
    bool any = false;
    char buf[64];
    size_t bi = 0;
    for (const char* p = token; ; ++p) {
        if (*p == ':' || *p == ';' || *p == '\0') {
            buf[bi] = '\0';
            if (bi > 0) any = true;
            if (n < 4) {
                char* end = nullptr;
                double v = std::strtod(buf, &end);
                if (end == buf && bi > 0) return false;   // non-numeric field
                parts[n++] = v;
            }
            bi = 0;
            if (*p == '\0') break;
        } else if (bi + 1 < sizeof(buf)) {
            buf[bi++] = *p;
        }
    }
    if (!any) return false;

    switch (n) {
        case 1: *outSec = parts[0]; break;                                   // seconds
        case 2: *outSec = parts[0] * 60.0 + parts[1]; break;                 // M:S
        case 3: *outSec = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];    // H:M:S
                break;
        case 4: *outSec = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];    // H:M:S:F
                *outFrames = parts[3];
                break;
        default: return false;
    }
    return true;
}

bool PT_LoadMarkersFromCsv(const wchar_t* path, PT_CardStore* store)
{
    if (!store || !path || path[0] == L'\0') return false;

    FILE* fp = _wfopen(path, L"rb");
    if (!fp) return false;

    store->markerCount = 0;
    std::memset(store->reveals, 0, sizeof(store->reveals));

    char line[1024];
    bool first = true;

    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        char* p = line;
        if (first) {
            first = false;
            if ((unsigned char)p[0] == 0xEF &&
                (unsigned char)p[1] == 0xBB &&
                (unsigned char)p[2] == 0xBF)
                p += 3;
        }

        // Quote-aware split. Column 0 is always the name. The time may be in
        // column 1 ("Name,Seconds") or column 2 (Premiere's In, after an empty
        // Description); take the first column that parses as a time.
        char* fields[8];
        int nf = pt_split_csv(p, fields, 8);
        if (nf < 2) continue;

        char* name = pt_trim(fields[0]);
        if (name[0] == '\0') continue;

        bool   isSub     = pt_is_subtract(name);
        double timeSec   = 0.0, frames = 0.0;
        double subAmount = 0.0;
        bool   haveTime  = false;

        if (isSub) {
            // Subtract marker: amount lives in the comment (field 1); the time
            // is the first *later* field that parses as a time. Starting the
            // time scan at field 2 keeps a timecode from being read as the
            // amount, and the amount (e.g. "1.00") from being read as a time.
            if (nf >= 2)
                pt_parse_price(pt_trim(fields[1]), &subAmount);
            for (int c = 2; c < nf && !haveTime; ++c) {
                char* tok = pt_trim(fields[c]);
                if (tok[0] != '\0' && pt_parse_time(tok, &timeSec, &frames))
                    haveTime = true;
            }
        } else {
            // Normal marker: time is the first field after the name that parses.
            for (int c = 1; c < nf && !haveTime; ++c) {
                char* tok = pt_trim(fields[c]);
                if (tok[0] != '\0' && pt_parse_time(tok, &timeSec, &frames))
                    haveTime = true;
            }
        }
        if (!haveTime) continue;              // header row or malformed -> skip

        PT_Reveal* rv = &store->reveals[store->markerCount];
        std::strncpy(rv->name, name, PT_MAX_NAME - 1);
        rv->name[PT_MAX_NAME - 1] = '\0';
        rv->timeSec = timeSec;
        rv->frames  = frames;
        if (isSub) {
            // Store the amount as a negative price so PT_TotalAtSeconds subtracts
            // it with no special case. matched=1 so it never renders "(?)".
            rv->kind    = PT_REVEAL_SUBTRACT;
            rv->price   = -std::fabs(subAmount);
            rv->matched = 1;
        } else {
            rv->kind    = PT_REVEAL_CARD;
            rv->price   = 0.0;
            rv->matched = 0;
        }

        if (++store->markerCount >= PT_MAX_MARKERS)
            break;
    }

    std::fclose(fp);

    std::wcsncpy(store->markersPath, path, PT_MAX_PATH_LEN - 1);
    store->markersPath[PT_MAX_PATH_LEN - 1] = L'\0';

    PT_ResolveReveals(store);
    return store->markerCount > 0;
}

double PT_RevealSeconds(const PT_Reveal* rv, double fps, double offsetSec)
{
    if (!rv) return 0.0;
    double frac = (fps > 0.0) ? rv->frames / fps : 0.0;
    return rv->timeSec + frac - offsetSec;
}

void PT_ResolveReveals(PT_CardStore* store)
{
    if (!store) return;

    // Join each reveal to a card by name (case-insensitive, first match wins).
    // Subtract reveals carry a fixed amount and are never card-joined.
    for (uint32_t i = 0; i < store->markerCount; ++i) {
        PT_Reveal* rv = &store->reveals[i];
        if (rv->kind == PT_REVEAL_SUBTRACT)
            continue;
        rv->price = 0.0;
        rv->matched = 0;
        for (uint32_t c = 0; c < store->count; ++c) {
            if (pt_ieq(rv->name, store->cards[c].name)) {
                rv->price = store->cards[c].price;
                rv->matched = 1;
                break;
            }
        }
    }

    // Insertion sort ascending by time (whole seconds, then frame remainder).
    // Small N, one-time on load. Frames are sub-second, so (timeSec, frames)
    // lexicographic order is the true time order regardless of frame rate.
    for (uint32_t i = 1; i < store->markerCount; ++i) {
        PT_Reveal key = store->reveals[i];
        int j = (int)i - 1;
        while (j >= 0 &&
               (store->reveals[j].timeSec > key.timeSec ||
                (store->reveals[j].timeSec == key.timeSec &&
                 store->reveals[j].frames > key.frames))) {
            store->reveals[j + 1] = store->reveals[j];
            --j;
        }
        store->reveals[j + 1] = key;
    }
}

double PT_TotalAtSeconds(const PT_CardStore* store, double nowSec,
                         double fps, double offsetSec, int* outCurrentIdx)
{
    if (outCurrentIdx) *outCurrentIdx = -1;
    if (!store) return 0.0;

    double total = 0.0;
    for (uint32_t i = 0; i < store->markerCount; ++i) {
        if (PT_RevealSeconds(&store->reveals[i], fps, offsetSec) <= nowSec) {
            total += store->reveals[i].price;
            if (outCurrentIdx) *outCurrentIdx = (int)i;   // reveals[] is time-sorted
        } else {
            break;                                        // rest are in the future
        }
    }
    return total;
}

/* --- currency formatting -------------------------------------------------- */

void PT_FormatCurrency(double value, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;

    bool negative = value < 0.0;
    double av = negative ? -value : value;

    // Round to cents.
    double rounded = std::floor(av * 100.0 + 0.5);
    long long cents = (long long)rounded;
    long long whole = cents / 100;
    int frac = (int)(cents % 100);

    // Build the integer part with thousands separators, right-to-left.
    char grouped[32];
    int gi = (int)sizeof(grouped);
    grouped[--gi] = '\0';

    if (whole == 0) {
        grouped[--gi] = '0';
    } else {
        int digitCount = 0;
        long long w = whole;
        while (w > 0 && gi > 0) {
            if (digitCount > 0 && digitCount % 3 == 0)
                grouped[--gi] = ',';
            grouped[--gi] = (char)('0' + (int)(w % 10));
            w /= 10;
            ++digitCount;
        }
    }

    std::snprintf(out, outSize, "%s$%s.%02d",
                  negative ? "-" : "", &grouped[gi], frac);
    out[outSize - 1] = '\0';
}
