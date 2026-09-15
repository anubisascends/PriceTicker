/*
** export_markers.jsx  -  Card Price Ticker helper
**
** Exports the ACTIVE SEQUENCE's markers as a "Name,Seconds" CSV that the
** Card Price Ticker effect reads (its "Choose Markers..." button). Times are
** written RELATIVE TO THE START OF THE SELECTED CLIP, which is what the effect
** sees as time 0. Each marker's NAME must match a card name in your price CSV.
**
** Workflow:
**   1. Scrub the timeline and press M at each reveal point to drop a sequence
**      marker. Double-click the marker and set its Name to the card's name.
**   2. Select the clip that has the Card Price Ticker effect (so the script
**      knows where clip time 0 is). If nothing is selected, the clip is assumed
**      to start at the beginning of the sequence.
**   3. Run this script:  File > Scripts > Run Script File...  (or via ExtendScript
**      Toolkit / VS Code ExtendScript Debugger) and choose where to save the CSV.
**   4. In the effect, click "Reveal Markers > Choose Markers..." and pick the CSV.
**
** Re-run this whenever you move/rename markers; then re-pick the file in the
** effect (or just reopen the project - the effect reloads it from the saved path).
*/

(function () {
    var seq = app.project.activeSequence;
    if (!seq) { alert("Card Price Ticker: no active sequence."); return; }

    // Time origin = start of the first selected video clip (fallback: 0).
    var origin = 0.0;
    var found  = false;
    for (var t = 0; t < seq.videoTracks.numTracks && !found; t++) {
        var track = seq.videoTracks[t];
        for (var i = 0; i < track.clips.numItems; i++) {
            var clip = track.clips[i];
            if (clip.isSelected && clip.isSelected()) {
                origin = clip.start.seconds;
                found  = true;
                break;
            }
        }
    }

    var markers = seq.markers;
    if (!markers || markers.numMarkers === 0) {
        alert("Card Price Ticker: the active sequence has no markers.\n" +
              "Drop markers on the timeline (press M) and name them after cards.");
        return;
    }

    var rows = ["Name,Seconds"];
    var kept = 0;
    var m = markers.getFirstMarker();
    while (m) {
        var name = String(m.name);
        // Strip commas/newlines that would break the CSV; trim ends.
        name = name.replace(/[\r\n,]+/g, " ").replace(/^\s+|\s+$/g, "");
        var rel = m.start.seconds - origin;
        if (name.length > 0 && rel >= 0) {
            rows.push(name + "," + rel.toFixed(4));
            kept++;
        }
        m = markers.getNextMarker(m);
    }

    if (kept === 0) {
        alert("Card Price Ticker: no usable markers found at/after the clip start.\n" +
              "Check that markers are named and fall within the selected clip.");
        return;
    }

    var out = File.saveDialog("Save markers for Card Price Ticker", "CSV:*.csv");
    if (!out) return;
    if (!/\.csv$/i.test(out.fsName)) out = new File(out.fsName + ".csv");
    out.encoding = "UTF-8";
    if (!out.open("w")) { alert("Card Price Ticker: could not write " + out.fsName); return; }
    out.write(rows.join("\n"));
    out.close();

    alert("Card Price Ticker: wrote " + kept + " marker(s) to\n" + out.fsName);
})();
