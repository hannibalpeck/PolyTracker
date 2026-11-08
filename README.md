# ProTracker for Polyend Tracker+

A ProTracker (Amiga) mode for the **Polyend Tracker+**, built from the open-source Polyend Tracker CE firmware. It boots straight into a four-channel, 64-row, 31-sample ProTracker 2.3D style editor and player, and loads and saves genuine Amiga `.mod` files. The replayer is ported from [pt2-clone](https://github.com/8bitbubsy/pt2-clone) by Olav Sorensen (BSD 3-Clause, see [`NOTICE.md`](NOTICE.md)).

> **Status: not tested on hardware.** The ProTracker core has host-side unit tests (`tests/protracker`) and the firmware builds and links, but it has not been run on a real Tracker+ yet. Flash it with the recovery steps below in mind.

This firmware is Tracker+ only. Support for the Original Tracker and Tracker Mini was removed, along with the stock pattern, song, sample editing, instrument, performance and project screens. What is kept: the ProTracker screen, Config, Master, the sample recorder and the sample loader.

## What it does

* Boots directly into ProTracker mode. Songs live in `/mods` on the SD card; saving never overwrites an existing file (`name-N.mod`).
* Plays with ProTracker 2.3D semantics: period tables and finetune, vibrato/tremolo tables, funk repeat, CIA tempo quirk, the E0x LED filter, and 20% default stereo separation. Known original quirks are kept on purpose.
* Reads `M.K.`, `M!K!`, `FLT4`, `4CHN` and `N.T.` modules (6 and 8 channel modules are refused). Writes `M.K.` (or `M!K!` above 64 patterns).
* ProTracker is the main screen: the firmware starts in it, and the **Pattern** button returns to it from every other screen.
* Module memory is reserved from external RAM, which makes the stock sample memory about 2.1 MB smaller.

Not done yet: importing WAV files or recording into the 31 ProTracker sample slots. The recorder and loader screens still work on the stock sample memory and are not connected to the `.mod` samples.

## Repository structure

```text
.
├── Makefile                # Top-level build (Tracker+ only)
├── version.mk              # Version and build number
├── core/                   # Application, audio engine, UI
│   ├── protracker/         # Pure C++ .mod model and replayer (no hardware dependencies)
│   └── interface/protrackerMode/   # The ProTracker screen
├── platforms/imx/          # Tracker+ (i.MX RT1050) HAL, drivers and linker script
├── synth/                  # PolySynth headers and precompiled libraries (see License)
├── util/                   # Shared utility headers
├── tests/protracker/       # Host unit tests
└── docs/
```

---

## Notice: Official Support, Warranty & Hardware Disclaimer

> **A Quick Note Before Flashing or Modifying Firmware**
>
> Thanks to the Tracker's isolated bootloader and hardware emergency recovery button (see [Flashing on Hardware & Emergency Recovery](#4-flashing-on-hardware--emergency-recovery)), experimenting with Community Edition builds is very safe. Even if a custom build fails to boot, you can easily re-flash a stable `.ptf` image from your microSD card. That said, we still need to include the standard legal disclaimer just in case:
>
> * **Provided "AS IS" Without Support Obligation**: While the foundation of this repository was originally developed and released by **Polyend**, the **Polyend Tracker Community Edition (CE)** firmware, source code, and any community-developed modifications or plugins are provided **"AS IS"**, without warranty of any kind, express or implied. **Polyend has no obligation to provide official technical support, maintenance, bug fixes, or customer service** for Community Edition firmware or custom builds.
> * **Use at Your Own Risk**: Flashing, modifying, or running custom firmware is done at your own discretion and risk. Polyend is not liable for any data loss, project corruption, malfunction, or hardware damage (including "bricking" a device or issues caused by code modifications) arising from the use of this software.
> * **Out-of-Warranty Repairs Available**: In the unlikely event that something ever goes wrong with your hardware while using custom firmware, it is **not covered under standard warranty**. However, **Polyend is always happy to help get your instrument back up and running through its official out-of-warranty repair service** (standard service, parts, and shipping fees apply. See [How to Contact Polyend Customer Support](https://backstage.polyend.com/t/how-to-contact-the-polyend-customer-support/15510)).

---

## Building Firmware

### 1. Prerequisites
* **[GNU Arm Embedded Toolchain `10.3-2021.10`](https://developer.arm.com/downloads/-/gnu-rm)**:
  * Direct official downloads from Arm:
    * **Linux (`x86_64`)**: [`gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2)
    * **Linux (`aarch64` / ARM64)**: [`gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2)
    * **macOS (`x86_64` / Apple Silicon via Rosetta)**: [`gcc-arm-none-eabi-10.3-2021.10-mac.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-mac.tar.bz2)
    * **Windows (ZIP)**: [`gcc-arm-none-eabi-10.3-2021.10-win32.zip`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-win32.zip)
  * Ensure `arm-none-eabi-gcc` is on your `PATH`, extract the toolchain into `./agt/10.3/`, or pass `TOOLCHAIN=/path/to/arm-none-eabi-` to `make`.
* `make` (for building firmware) and `python3` (used by the `make bump-*` version helpers; standard library only).

### 2. Build Commands
From the repository root:

```bash
make plus       # -> build/TrackerPlus_CE_1.0.0.ptf
make clean

# Host tests for the ProTracker core (needs only g++):
cd tests/protracker && g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  -I../../core/protracker test_protracker.cpp ../../core/protracker/ptModule.cpp \
  ../../core/protracker/ptReplayer.cpp ../../core/protracker/ptTables.cpp -o /tmp/pt_test && /tmp/pt_test
```

### 3. Version Management (`version.mk`)
All firmware version numbers are defined in [`version.mk`](version.mk). The **Build** identifier displayed on the hardware under **Config > Firmware > Build** is automatically populated from the short Git commit hash (`git rev-parse --short=7 HEAD`):

```bash
# Show current version, Git build hash & output filenames
make version

# Increment patch version (e.g. 1.0.0 -> 1.0.1)
make bump-patch

# Increment minor version (e.g. 1.0.x -> 1.1.0)
make bump-minor
```

### 4. Flashing on Hardware & Emergency Recovery

1. Copy the generated `.ptf` file from `build/` (`TrackerPlus_*.ptf`) to the `/Firmware/` folder on your Tracker's microSD card.
   * Keep the `.ptf` filename at 31 characters or fewer.
2. On the device, open **Config > Firmware > Firmware update**, select the `.ptf` file, and confirm.

> **Bootloader Resilience & Emergency Firmware Recovery**:
> * The Polyend Tracker bootloader is isolated from the application firmware:
>   * On the **Tracker+ (`IMX`)**, the bootloader resides in a protected region at the bottom of QSPI Flash (`0x60000000`–`0x6002FFFF`), while `.ptf` application builds are linked starting at `0x60030000` (do not modify the flash `ORIGIN` address in the linker scripts).
> * **Emergency Recovery**: If a custom firmware build ever hangs or fails to boot into the UI, you can always recover your device:
>   1. Place a known-good, stable `.ptf` firmware file for your device inside the `/Firmware/` folder on your microSD card (ideally as the only `.ptf` file in `/Firmware/`).
>   2. Insert the microSD card into your Tracker and power it on.
>   3. Use a paperclip or SIM-eject pin to press the **recessed hardware Reset / Emergency Bootloader pinhole button** (on the bottom of the Tracker+). The hardware bootloader will automatically pick up the `.ptf` file from `/Firmware/` and flash it.

---

## Documentation

* [ProTracker mode](docs/protracker.md)
* [Firmware architecture and developer gotchas](docs/developer_guide.md) (written for the full upstream firmware; parts no longer apply)
* [Custom Themes & Fonts](docs/custom_themes_and_fonts.md)

## Upstream and license

Based on [polyend/tracker-ce](https://github.com/polyend/tracker-ce). See the upstream project for the stock firmware, other devices and its community. The license is unchanged: MIT with a Wholesale Commercial Clone Restriction (see [`LICENSE`](LICENSE)), the precompiled PolySynth/PolyDSP libraries have their own terms ([`synth/lib/LICENSE-PROPRIETARY.txt`](synth/lib/LICENSE-PROPRIETARY.txt)), and third-party components are listed in [`NOTICE.md`](NOTICE.md).
