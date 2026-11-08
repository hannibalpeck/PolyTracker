# ProTracker mode

The firmware boots into a ProTracker 2.3D style editor. The code is split in two parts:

* `core/protracker/` is a self-contained C++ library with no hardware dependencies. `ptModule` holds the song and reads/writes `.mod` files, `ptReplayer` is the Paula-style replayer ported from pt2-clone, `ptAudioStream` feeds it into the Teensy-style audio graph (mixer input `FinalMixInputs::kProTracker`).
* `core/interface/protrackerMode/` is the screen (`cProTrackerMode`), including the file list.

## Controls (Pattern screen)

The screen is the stock Polyend pattern editor: four tracks of note, instrument and effect, 15 visible rows with the cursor in the middle, eight bottom buttons with two pages ("More" switches).

* **Rec** switches between green (pads only play, cursor follows playback) and red (pads write). **Play + Rec** (hold one, press the other) plays the pattern and records: pads write the note on the row that is playing, in the track the cursor is in, and the cursor runs with the song. The whole take is one Undo step. Rec while playing punches in and out the same way.
* **Note / Instr / Fx1 / Fx2** tap to choose the column. Holding Note, Instr or Fx opens the stock popup (pads or encoder choose, release applies). The Fx popup lists the 29 ProTracker effects (0xy Arpeggio … Fxx Speed/Tempo, with the E-commands separately) and puts the chosen one in the effect lane with a starting value. A step has one effect, so Fx1 and Fx2 do the same.
* **Effect values:** with the cursor on the effect lane press **Rec** (red) and turn the jogwheel. Ranges are the usual ProTracker ones: Cxx 00-40, Bxx 00-7F, Dxx 00-63, Exy changes y only, Fxx 01-1F speed, 20-FF tempo, the rest 00-FF.
* **Pads** (48): when the note column is active, pad 13 is C-3. A .mod has three octaves, so pads 1-12 are C-2..B-2, pads 13-24 C-3..B-3, and the pads above fold down: 25-36 are C-1..B-1, 37-48 C-2..B-2. Red: note or instrument (pad 1 = sample 1; a new instrument on a step without a note also writes C-3) is written and the cursor moves by the step jump. Green: preview only. Pads sound over a running song.
* **Bottom buttons, page 1:** Pattern, Position, Step jump: each is active while its button is held; turn the jogwheel (or use the arrows or pads) to change the value. Copy Track, Paste Track, Copy Pattern, Paste Pattern (pastes need red mode). **Page 2 (More):** Preview, Undo, Redo.
* **Pattern** (held) + arrows or encoder picks the pattern of the current position; pressing it on another screen returns here.
* **Play** loops the current pattern (on the song screen it plays the song from the selected position, and stops it again). A song keeps playing when you switch between the Pattern and Song screens, and the pattern screen shows the pattern it is in, Shift+Play starts at the cursor. **Shift** + bottom buttons 1-4 mutes tracks. Shift+Left/Right extend a selection, Shift+Up selects the whole track and Shift+Up again the whole pattern (Shift+Down goes back). The selection stays after Shift is released until the cursor is moved. Copy copies the selection, Shift+Copy pastes at its top left, Delete clears only the lane under the cursor, note, instrument or effect (Shift+Delete is backspace); Insert inserts a row, Shift+Insert goes to the first row.
* **File** opens the file screen.

Limits follow the .mod format: 4 tracks, 64 rows, 31 samples, patterns 0-99, ProTracker effects 0-F.

## Controls (Song screen)

The **Song** button opens the arrangement view, built from the stock Polyend song screen: the playlist on the right (slot number and pattern, play icon at the playing slot), a block matrix with the four tracks of every pattern (filled when the track has notes) and the tempo bar. The playlist is the order list of the .mod file: up to 128 slots, patterns 0-99.

* **Encoder / Up / Down** move through the slots. **Shift + encoder**, **Left / Right** and the **Pattern -/+** buttons (Shift: 10 steps) change the pattern of the slot.
* **Add Slot** inserts a copy of the slot after it, **Shift + Add Slot** inserts a new empty pattern. **Delete Slot** removes the slot (at least one stays). **Undo** steps back through the last 16 changes.
* **Play Pattern** loops the pattern of the slot, **Play Song** plays from the slot (while playing: Loop Pattern / Continue Song / Stop).
* **Tempo** held + encoder or Up/Down (Shift: 10) edits the first tempo command (Fxx, 32 and up) of the song, or writes one on the first free effect of row 1 when there is none. A .mod has no separate tempo field.
* **Pattern** goes back to the pattern screen on the selected slot.

Playback stops when you change screens.

## File screen

The **File** button opens a screen with the layout of the stock Polyend file screen: New Song, Open, Clear Song (patterns, order list and title are cleared, the samples stay), Save and Save As. Open and Save As show the SD card browser (folders on the left, starting in `/Mod`) with Enter, Delete, New Folder, Cancel and Open/Save; Save As names the file with the on-screen keyboard. Only `.mod` files can be opened. **Export WAV** renders the song (from position 0 until it ends or loops, at most 15 minutes) to a 16-bit 44.1 kHz stereo WAV in `/Mod/Export`, named after the song; "Please wait while exporting..." and the progress in percent are shown while it renders. New and Open ask to save unsaved changes first.

## Sample loader

The **Sample Load** screen (stock layout: file browser on the left, instrument list on the right) loads WAV files straight into the 31 sample slots of the song. Add and Add Next load the selected files from the selected slot on; Import loads the selected file into the selected slot. Samples are converted to 8-bit mono (channels averaged, not resampled) and cut at 65534 bytes. Delete, Delete Unused (samples no pattern uses), Copy/Paste (Shift+Copy) and Rename work on the slots.

## Sample editor

The **Sample Edit** button opens the stock Polyend sample editor on the current sample (the one selected on the pattern screen). Start, End and Zoom select the part to work on, the list on the right holds the effects: Normalizer, Crop, Reverse, Amplifier, Overdrive, Delay, Bitcrusher, Chorus, Flanger, Equalizer, Limiter, Compressor, Fade In and Fade Out. (WT Smoother, Timestretch and Modular were left out.) Effects with parameters open a parameter screen with Preview (auditions the result at C-3) and Apply; Crop, Reverse and the fades apply straight away. Undo takes back the last Apply (one level). The pads play the selection at their pitch.

The effects work on a 16-bit copy of the sample; Apply converts the result back to 8 bit and writes it into the song's sample slot, so every Apply or Undo requantises. Effect time constants (delay time, chorus rate and so on) assume 44.1 kHz, not the 16.5 kHz a sample plays at for C-3. A result longer than 65534 bytes is refused.

## Sample parameters

The **Sample Play** button opens the sample header: Sample (which of the 31), Volume (0-64), Finetune (-8..7), Loop (1-Shot or Forward, the only two modes ProTracker has), Loop start, Loop end and Length, all in bytes. The waveform shows the loop markers. Select a field with its button or Left/Right, change it with the encoder or Up/Down (Shift: coarse steps), Delete resets Volume, Finetune or the loop. Positions move one word (2 bytes) at a time. The pads play the sample with its loop at their pitch. Shortening the length keeps the loop inside the sample. Loop start and Loop end are greyed out while the sample is 1-Shot. A line on the waveform shows the playing position of a pad, and loop points, length and volume changed while a pad is held are heard immediately.

In both the sample editor and the sample parameters screen, hold **Instr** and turn the encoder to pick another sample from the list; releasing Instr loads it. While a pattern is playing in green mode, Left/Right still move the cursor between the tracks.

## Files

Songs are stored in `/Mod` on the SD card. Saving never overwrites: an existing name becomes `name-N.mod`. Samples are 8-bit, at most 65534 bytes each, 31 per song; a module holds up to 128 orders of 64 rows.

## Memory

`PROTRACKER_POOL_SIZE` (`core/source/mtStructs.h`) is carved out of the stock sample memory, so the other screens have about 2.1 MB less sample space.

## Tests

`tests/protracker/test_protracker.cpp` covers load/save round trips, the sanitising rules and replayer behaviour (portamento, vibrato, arpeggio, tempo quirks). Build and run it with the command in the README.

## Known limits

* Not tested on hardware yet.
* Some `#ifdef` code for other devices is still in the tree.

## Settings, restart and credits

The **Config** screen has no MIDI section. **Info → About** lists the sources this firmware is built on: Polyend Tracker CE and the ProTracker 2.3D replayer as implemented in pt2-clone. The song is loaded again after a restart. When the unit is shut down with the power button, the current song, unsaved changes included, is written to `/Mod/.lastsong` (with `/Mod/.lastinfo` holding its file name and whether it has unsaved changes) and loaded again at the next start. After a power cut without the shutdown sequence, the last song that was opened or saved is loaded instead. The **File** button works from the Song screen too.
