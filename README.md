# Card Price Ticker — Premiere Pro C++ effect plugin

A drag-and-drop video effect for Adobe Premiere Pro that overlays a **cumulative
running total** of card prices onto a clip. You feed it two files:

1. a **price CSV** of `CardName,Price`, and
2. a **markers file** exported from your timeline — either Premiere's own
   **Export Markers** CSV (used as-is) or a simple `CardName,Seconds` file.

**Markers drive everything — there are no keyframes.** You drop a timeline marker
at each reveal point and name it after the card; the running total is computed
straight from the marker times, so you never have to figure out counts or
positions. Each reveal adds its card's price to the on-screen total and
(optionally) flashes that card's own value for a few frames.

The effect matches markers to prices **by name** (case-insensitive), so the CSV's
row order is irrelevant. A marker whose name matches no card is drawn with a `(?)`
and adds `$0` — handy for spotting a spelling mismatch between the two files.

> **Why a markers *file* and not the live timeline?** Premiere doesn't let an
> effect read the sequence's markers or create keyframes (both are After Effects
> AEGP capabilities, unavailable to Premiere-hosted effects). So you export the
> markers once — via Premiere's **Export Markers**, or the bundled
> [`export_markers.jsx`](export_markers.jsx) — and point the effect at the file.

Built on the AE-style effect API that Premiere hosts (`PF_*` / `EffectMain`),
modelled on the SDK's `GPUVideoFilter\SDK_ProcAmp` and `Vignette` examples.
**Windows-only**, CPU render path, text drawn with **DirectWrite/Direct2D**.

---

## 1. Prerequisites

| Requirement | Notes |
|---|---|
| **Premiere Pro C++ SDK** | You have it at `D:\source\SDK\Adobe\Premiere Pro`. |
| **After Effects SDK** | **Required and not yet installed.** The effect API headers (`AE_Effect.h`, `Param_Utils.h`, `AE_Macros.h`) and the `PiPLTool.exe` used to compile the plugin's resource live here. Free download from Adobe (search "After Effects Plug-in SDK"). |
| **Visual Studio 2022** | Desktop development with C++ workload. The project uses toolset `v143`. |
| Premiere Pro | Any recent version that loads the 26.0-era SDK effects. |

## 2. One-time environment setup

Set these environment variables (System, or per-user), then **restart Visual
Studio** so it picks them up:

| Variable | Value | Purpose |
|---|---|---|
| `AE_SDK_BASE_PATH` | root of the After Effects SDK (the folder whose `Examples\Headers` contains `AE_Effect.h`) | **Required.** Effect headers + PiPLTool. |
| `PREMIERE_SDK_PATH` | `D:\source\SDK\Adobe\Premiere Pro` | Premiere headers. *(Optional — the project already defaults to this path.)* |
| `PREMSDKBUILDPATH` | e.g. `D:\source\repos\PricePlugin\_build` | Where the built `.aex` lands. *(Optional — defaults to `_build\` beside the project.)* |

## 3. Build

```
Win\PriceTicker.sln  →  configuration x64, Debug or Release  →  Build
```

Output: `PriceTicker.aex` in `PREMSDKBUILDPATH` (default `_build\`).

If the build stops early complaining that `AE_SDK_BASE_PATH is not defined`, the
After Effects SDK step in **Prerequisites/Setup** hasn't been completed.

## 4. Install

Copy `PriceTicker.aex` to Premiere's plug-in folder (needs admin):

```
C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\
```

Restart Premiere Pro. The effect appears under **Video Effects → SDK → Card
Price Ticker**.

> Tip: set `PREMSDKBUILDPATH` directly to the MediaCore path to build straight
> into place (run VS as admin), skipping the copy.

## 5. Use it

1. **Mark the reveals.** Scrub the timeline and press **M** at each point a card
   is revealed. Double-click each marker and set its **Name** to the card's name
   exactly as it appears in your price CSV.
2. **Export the markers**, either way:
   - **Premiere's Export Markers** → save the CSV. The effect reads it directly
     (name from the *Marker Name* column, time from the *In* timecode). Times are
     in **sequence** time, so if your clip doesn't start at 00:00:00:00 you'll set
     **Marker Time Offset** in step 6.
   - **`export_markers.jsx`** (File → Scripts → Run Script File…). Select the clip
     that will carry the effect first; it writes `Name,Seconds` **relative to the
     clip start** (see `sample_markers.csv`), so no offset is needed.
3. **Apply the effect.** Drag **Card Price Ticker** onto that clip.
4. **Effect Controls → Card List → Choose CSV…** — pick your price file
   (see `sample_cards.csv`).
5. **Reveal Markers → Choose Markers…** — pick the file from step 2. The total
   now appears and advances by itself at each marker time — no keyframing.
6. If the total is time-shifted (Premiere export + a clip that starts later in the
   sequence), set **Marker Time Offset (s)** to the clip's start time in seconds.
   `0` means markers already start at the clip's first frame.
7. Adjust look with **Position X/Y**, **Font**, **Font Size**, **Text Color**.
   **Show Card Value** flashes `+$value  Name` for **Card Display (frames)** after
   each reveal.

> Change a marker later? Re-export, then re-pick the file (or just reopen the
> project — the effect reloads it from the saved path). A marker whose name
> matches no card still reveals, shows `(?)`, and adds `$0` — the quickest way to
> catch a name typo between the two files.

### Parameters

| Parameter | Meaning |
|---|---|
| **Card List → Choose CSV…** | Opens a file dialog; loads `CardName,Price` rows. |
| **Reveal Markers → Choose Markers…** | Opens a file dialog; loads the exported markers (Premiere's Export Markers CSV or `Name,Seconds`). Marker times drive the running total. |
| **Marker Time Offset (s)** | Seconds subtracted from every marker time. Set to the clip's sequence-start when using Premiere-exported (sequence-time) markers. Default `0`. |
| **Card Display (frames)** | How long a freshly revealed card's own value is shown. Default 20. |
| **Position X / Y (%)** | **Top-left corner** of the text block, as a percent of frame width/height. Text is left/top-aligned, so lines grow right and down and the first line never shifts when the `+value` line appears. |
| **Font** | Font family used to render the overlay (choose from common Windows fonts). |
| **Font Size (px)** | Font size at full resolution (auto-scaled in reduced-res previews). |
| **Text Color** | Fill color of the **Total: $X** line (drop shadow added for legibility). |
| **Card Value Color** | Fill color of the **+$value  Name** reveal line when **Price Thresholds** is off. |
| **Show Card Value** | When on, shows the `+$value…` reveal line for *Card Display* frames after each reveal. |
| **Show Card Name** | When on, the reveal line includes the card name (`+$value  Name`); when off, just `+$value`. |
| **Price Thresholds** | When on, the reveal line is colored by the revealed card's price band instead of the single *Card Value Color*. |
| **Low if price ≤ ($)** | Upper bound of the **Low** band. A card priced at/below this uses **Low Color**. |
| **Medium if price ≤ ($)** | Upper bound of the **Medium** band. At/below → **Medium Color**; above → **High Color**. |
| **Low / Medium / High Color** | Reveal-line fill for each price band (used when *Price Thresholds* is on). |
| **Subtract Color** | Reveal-line fill for a `[subtract]` marker's `-$amount` line. |
| **Preview Sound** | When on, plays the band's WAV as an **editing-only** cue as playback crosses a card reveal. **Not in the export — turn this off before exporting.** |
| **Low / Medium / High Sound → Choose WAV…** | Optional WAV played when a card in that band reveals. Empty = silent. |

### File formats

**Price CSV** — name in the first column, price in the second; optional header.
Names containing commas must be quoted (`"Yorion, Sky Nomad",0.34`). Currency
symbols, thousands separators, and quotes are tolerated in the price:

```
CardName,Price
Black Lotus,25000.00
Mox Sapphire,"$4,200.50"
```

**Markers file** — the name is always the first column; the time is taken from
the first column that parses as a time. Two shapes are accepted:

*Premiere Export Markers* (used as-is — time from the `In` timecode):

```
Marker Name,Description,In,Out,Duration,Marker Type
Black Lotus,,00:00:05:13,00:00:05:13,00:00:00:00,Comment
```

*Simple* (`export_markers.jsx`, see `sample_markers.csv`):

```
Name,Comment,Seconds
Black Lotus,,5.5
```

(The older two-column `Name,Seconds` form still loads.)

Time values may be plain seconds (`5`, `5.5`), `H:M:S(.f)` (`00:00:05.5`), or an
`HH:MM:SS:FF` timecode (`00:00:05:13`); timecode frames are converted using the
sequence frame rate at render time.

Limits: up to 512 cards and 512 markers, names up to 63 bytes (UTF-8).

### Subtract markers

To **subtract** a fixed amount from the running total at a point in the timeline,
add a marker named **`[subtract]`** (case-insensitive) and put the amount in the
marker's **comment**. At that marker's time the effect subtracts the amount and
flashes `-$amount` in the **Subtract Color** (no sound is played for a subtract).

```
Name,Comment,Seconds
[subtract],1.00,42.0        # at 42s, subtract $1.00 from the total
```

In a Premiere *Export Markers* CSV the same marker appears as
`[subtract],1.00,00:00:42:00,...` — the amount is the `Description` column and the
time is the `In` timecode.

### Price thresholds & preview sounds

Turn on **Price Thresholds** to color the reveal line by the revealed card's
price: at/below **Low if price ≤** uses **Low Color**, at/below **Medium if
price ≤** uses **Medium Color**, and anything higher uses **High Color**. With
thresholds off, the reveal line uses the single **Card Value Color** as before.

Each band can also have an optional **preview sound**: pick a WAV for Low/Medium/
High and, with **Preview Sound** on, the matching clip plays as playback crosses a
card in that band.

> **Audio is a preview cue only.** A Premiere *video* effect has no audio output
> path — it can't write sound into the mixed/exported audio (the same limitation
> that forces the markers-*file* design). The WAV plays through the editing
> machine's speakers while you scrub/preview; it will **not** be in your exported
> video, and it may make noise during an on-machine export, so **turn Preview
> Sound off before exporting** (or use a proper audio track for the final mix).

---

## Project layout

| File | Role |
|---|---|
| `PriceTicker.h` | Effect identity, parameter indices/IDs, out-flags. |
| `PriceTicker.cpp` | Setup, params, sequence-data persistence, button/file-dialogs, render + composite. |
| `CardData.h/.cpp` | CSV + markers parsing, name→price join, currency formatting, the flat sequence-data store (SDK-independent). |
| `TextRenderer.h/.cpp` | DirectWrite/Direct2D/WIC → premultiplied BGRA text bitmap. |
| `PriceTicker.r` | PiPL resource (name, category, entry, out-flags). |
| `PriceTicker.rc` | Includes the `PiPLTool`-generated `PriceTicker.rcp`. |
| `export_markers.jsx` | ExtendScript: dump the active sequence's markers to the `Name,Seconds` file. |
| `Win/` | Visual Studio solution/project/filters. |
| `sample_cards.csv` / `sample_markers.csv` | Example data. |

## Design notes & known risks

- **Pixel format.** The effect registers `PrPixelFormat_BGRA_4444_32f` so
  DirectWrite's premultiplied BGRA output composites over the frame with no
  color-space conversion.
- **Markers drive the total (no keyframes).** An effect hosted in Premiere can
  neither read the sequence's markers nor create keyframes — both need the After
  Effects AEGP API, which Premiere doesn't expose to effects. So the reveal
  schedule is imported as a file (`export_markers.jsx` → *Choose Markers…*),
  stored in sequence data next to the cards, and joined by name. At render time
  the effect converts `current_time` to seconds and sums every reveal at/before
  it — an `O(markers)` scan, no per-frame parameter sampling.
- **Marker time base.** Marker seconds are **relative to the effect's clip start**
  (the script subtracts the selected clip's start). If the running total looks
  time-shifted, the clip used as the origin in the export didn't match the clip
  the effect is on — re-export with the correct clip selected.
- **Button events.** Premiere's delivery of button-click events
  (`PF_Cmd_USER_CHANGED_PARAM`) to effects has historically been less reliable
  than in After Effects. If **Choose CSV…** does nothing, that's the first thing
  to check — as a fallback the store also reloads from the last saved path on
  project load, and you can point `PT_LoadCardsFromCsv` at a fixed path for
  testing.
- **Out-flags must match.** The numeric `AE_Effect_Global_OutFlags` in
  `PriceTicker.r` (`0x50`) must match what `GlobalSetup` sets. If the host warns
  about a mismatch on first load, reconcile the two.
- **Not yet compiled.** These sources were written against the SDK headers but
  have **not been built** here, because the After Effects SDK isn't installed on
  this machine. Expect to iron out a few small header/signature details on the
  first compile — see the design notes above for the likely spots.
