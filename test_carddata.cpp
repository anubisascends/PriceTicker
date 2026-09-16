// Standalone verification of the SDK-free CardData logic (no Adobe SDK needed).
// Build & run from the repo dir (so the sample CSVs resolve against CWD):
//   cmd /c "call \"<vcvars64.bat>\" >nul && cl /nologo /EHsc /I . test_carddata.cpp CardData.cpp && test_carddata.exe"
//
// Focuses on the v4 additions: [subtract] markers (name=[subtract], amount in the
// comment column, time from position), the card-join staying intact, and the
// running total reflecting the subtraction. Exit code is the number of failures.

#include "CardData.h"
#include <cstdio>
#include <cmath>
#include <cstring>

static int g_fail = 0;

static void check(const char* what, double got, double want)
{
    bool ok = std::fabs(got - want) < 0.005;
    std::printf("%-40s got=%.2f want=%.2f  %s\n", what, got, want, ok ? "OK" : "FAIL");
    if (!ok) ++g_fail;
}

static void checkInt(const char* what, long got, long want)
{
    bool ok = (got == want);
    std::printf("%-40s got=%ld want=%ld  %s\n", what, got, want, ok ? "OK" : "FAIL");
    if (!ok) ++g_fail;
}

int main()
{
    static PT_CardStore store;
    PT_InitStore(&store);

    if (!PT_LoadCardsFromCsv(L"sample_cards.csv", &store)) {
        std::printf("FAILED to load sample_cards.csv (run from the repo dir)\n");
        return 1;
    }
    if (!PT_LoadMarkersFromCsv(L"sample_markers.csv", &store)) {
        std::printf("FAILED to load sample_markers.csv (run from the repo dir)\n");
        return 1;
    }

    std::printf("cards=%u markers=%u\n\n", store.count, store.markerCount);

    // Locate the subtract reveal and a couple of card reveals.
    int subIdx = -1, blackLotusIdx = -1;
    for (uint32_t i = 0; i < store.markerCount; ++i) {
        if (store.reveals[i].kind == PT_REVEAL_SUBTRACT) subIdx = (int)i;
        if (std::strcmp(store.reveals[i].name, "Black Lotus") == 0) blackLotusIdx = (int)i;
    }

    checkInt("subtract reveal exists", subIdx >= 0 ? 1 : 0, 1);
    if (subIdx >= 0) {
        check("subtract stored as negative price", store.reveals[subIdx].price, -1.00);
        checkInt("subtract marked matched", (long)store.reveals[subIdx].matched, 1);
        check("subtract time (from position)", store.reveals[subIdx].timeSec, 11.0);
    }

    // A normal card still joins to its price.
    if (blackLotusIdx >= 0) {
        check("Black Lotus joined price", store.reveals[blackLotusIdx].price, 25000.00);
        checkInt("Black Lotus matched", (long)store.reveals[blackLotusIdx].matched, 1);
    }

    // Running total: fps only matters for timecode frames (none here); offset 0.
    const double fps = 30.0;
    // At 3s: only Mox Jet (2.0s) revealed.
    check("total @3s (Mox Jet)", PT_TotalAtSeconds(&store, 3.0, fps, 0.0, nullptr), 2400.00);
    // At 6s: + Black Lotus (5.5s).
    check("total @6s (+Black Lotus)", PT_TotalAtSeconds(&store, 6.0, fps, 0.0, nullptr),
          2400.00 + 25000.00);
    // At 10s: + Time Walk (9.0s), still BEFORE the subtract at 11s.
    check("total @10s (before subtract)", PT_TotalAtSeconds(&store, 10.0, fps, 0.0, nullptr),
          2400.00 + 25000.00 + 3100.75);
    // At 11.5s: subtract $1.00 has now applied.
    check("total @11.5s (after subtract)", PT_TotalAtSeconds(&store, 11.5, fps, 0.0, nullptr),
          2400.00 + 25000.00 + 3100.75 - 1.00);
    // At 12.6s: + Ancestral Recall (12.5s).
    check("total @12.6s (+Ancestral)", PT_TotalAtSeconds(&store, 12.6, fps, 0.0, nullptr),
          2400.00 + 25000.00 + 3100.75 - 1.00 + 3800.00);

    // Currency formatting of the subtract amount as rendered ("-$1.00").
    char buf[64];
    PT_FormatCurrency(-store.reveals[subIdx >= 0 ? subIdx : 0].price, buf, sizeof(buf));
    std::printf("\nsubtract amount formatted: %s (expect $1.00)\n", buf);

    std::printf("\n%s (%d failure(s))\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail;
}
