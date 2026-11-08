# ProTracker mode

The firmware boots into a ProTracker 2.3D style editor. The code is split in two parts:

* `core/protracker/` is a self-contained C++ library with no hardware dependencies. `ptModule` holds the song and reads/writes `.mod` files, `ptReplayer` is the Paula-style replayer ported from pt2-clone, `ptAudioStream` feeds it into the Teensy-style audio graph (mixer input `FinalMixInputs::kProTracker`).
* `core/interface/protrackerMode/` is the screen (`cProTrackerMode`), including the file list.

## Controls

* **Pads** play the current sample with the stock pad layout (looping samples sustain while held). By default they only play; nothing is written to the song.
* **Rec** toggles recording. With Rec on, pads write notes at the cursor (on the sample and effect columns the first 16 pads enter hex digits). The info line shows PLAY, REC or NOTE.
* **Note** jumps to the note column and turns on note entry until pressed again: pads write notes, and the encoder changes the note under the cursor (Shift: octave).
* **Instr** opens the instrument selector, a list of the 31 samples. Pads (pad 1 is sample 1), the encoder and Up/Down choose the sample; Instr, Enter or Done closes it.
* **Pattern** returns to the main ProTracker screen from any other screen, and closes the selector or file list. **Game** opens ProTracker as well.

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
