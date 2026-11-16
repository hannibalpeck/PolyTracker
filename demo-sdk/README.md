# Tracker CE demos (`.ptdemo`)

Full-screen demoscene effects that live on the SD card, not in the firmware.

## Running a demo

1. Copy `*.ptdemo` files to `/Demos` on the SD card (the folder is created the first time you open the menu).
2. On the Tracker: **Config → Info → Demos**, pick a file with the jog wheel or Up/Down, press **Run** (Cancel goes back).
3. **Shift** leaves the demo and returns to Config. Nothing else responds while a demo runs (the power button still works).

The firmware stops the current song before a demo starts.

## Writing a demo

One C file, no libc. Everything comes through the host API in `tracker_demo.h`:

| Entry point | When |
|---|---|
| `int32_t demo_init(const PtDemoHostApi* host, void* scratch, uint32_t scratch_size)` | once; upload textures, return 0 |
| `void demo_frame(uint32_t time_ms)` | every panel frame; emit FT81x display-list words with `host->dl()` |
| `void demo_exit(void)` | optional, on Shift |

The firmware opens each frame with `VERTEX_FORMAT(0)` and a black clear, and closes and shows it after `demo_frame` returns. Use the `PTD_*` macros for the words. Keep within about 2000 words per frame. The included demo uses about 325.

Metadata goes in comments at the top of the file:

```c
// PTDEMO_NAME: Oldskool
// PTDEMO_AUTHOR: you
// PTDEMO_SCRATCH: 8192        // working RAM, max 384 KB
// PTDEMO_FLAGS: IMX_ONLY      // only if code+bss > 32 KB
```

Build (needs `arm-none-eabi-gcc`):

```sh
python3 ptdemo_builder.py demos/oldskool.c -o build/Demos/oldskool.ptdemo
```

### What you get

| | |
|---|---|
| Graphics RAM (RAM_G) | `host->ramg_base` .. `+ramg_size` (700000..1040000, 340 KB, on this firmware). Writes outside are ignored. |
| Bitmap handles | `host->first_bitmap_handle` .. `+bitmap_handle_count-1` (8..14) |
| ROM fonts | read metrics from `PTD_ROM_FONT_TABLE_POINTER`; see `oldskool.c` |
| Code + bss | up to 128 KB (the builder asks for `IMX_ONLY` above 32 KB, so a demo says it needs a Tracker+/Mini) |
| Files | `host->read_file("tex.bin", …)`: relative paths resolve next to the `.ptdemo` (call from `demo_init` only) |
| Pads | `host->pad_light(pad, level)` |

### Rules

The code runs wherever the loader puts it, and nothing patches addresses. The builder refuses anything that would need that:

- keep every global `static` (the builder also hides symbols)
- no pointers in initialized data (`static const char* names[] = {...}` fails); build tables of offsets or compute pointers at run time
- no libc calls: use `host->sinf`, `host->memcpy`, and so on

A demo runs as native code, so a crashing demo freezes the Tracker until you power-cycle it. Test new demos without unsaved work open.
