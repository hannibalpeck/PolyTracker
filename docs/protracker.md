# ProTracker mode

The firmware boots into a ProTracker 2.3D style editor. The code is split in two parts:

* `core/protracker/` is a self-contained C++ library with no hardware dependencies. `ptModule` holds the song and reads/writes `.mod` files, `ptReplayer` is the Paula-style replayer ported from pt2-clone, `ptAudioStream` feeds it into the Teensy-style audio graph (mixer input `FinalMixInputs::kProTracker`).
* `core/interface/protrackerMode/` is the screen (`cProTrackerMode`), including the file list.

## Controls (Pattern screen)

The screen is the stock Polyend pattern editor: four tracks of note, instrument and effect, 15 visible rows with the cursor in the middle, eight bottom buttons with two pages ("More" switches).

* **Rec** switches between green (pads only play, cursor follows playback) and red (pads write).
* **Note / Instr / Fx1 / Fx2** tap to choose the column. Holding Note or Instr opens the stock popup (pads or encoder choose, release applies in red mode). A step has one effect, so Fx1 and Fx2 both select it; Fx1/Fx2 held + encoder or pad changes the effect command, otherwise the value.
* **Pads** use the stock layout. Red: note, instrument (pad 1 = sample 1) or effect value is written and the cursor moves by the step jump. Green: preview only.
* **Bottom buttons, page 1:** Pattern, Position, Step jump (hold + arrows, encoder or pads), Preview, Undo, Redo. **Page 2:** Duplicate Pattern, Copy Pattern, Paste Pattern.
* **Pattern** (held) + arrows or encoder picks the pattern of the current position; pressing it on another screen returns here.
* **Play** loops the current pattern, Shift+Play starts at the cursor. **Shift** + bottom buttons 1-4 mutes tracks. Shift+arrows select a block; Copy copies, Shift+Copy pastes; Delete clears, Shift+Delete is backspace; Insert inserts a row, Shift+Insert goes to the first row.
* **File** opens the song list (Load, Save, New, Cancel).

Limits follow the .mod format: 4 tracks, 64 rows, 31 samples, patterns 0-99, ProTracker effects 0-F.

## Files

Songs are stored in `/ProTracker` on the SD card. Saving never overwrites: an existing name becomes `name-N.mod`. Samples are 8-bit, at most 65534 bytes each, 31 per song; a module holds up to 128 orders of 64 rows.

## Memory

`PROTRACKER_POOL_SIZE` (`core/source/mtStructs.h`) is carved out of the stock sample memory, so the other screens have about 2.1 MB less sample space.

## Tests

`tests/protracker/test_protracker.cpp` covers load/save round trips, the sanitising rules and replayer behaviour (portamento, vibrato, arpeggio, tempo quirks). Build and run it with the command in the README.

## Known limits

* Not tested on hardware yet.
* Sample import/recording into the 31 slots is not wired up.
* Some `#ifdef` code for other devices is still in the tree.
