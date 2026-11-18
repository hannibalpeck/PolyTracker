#ifndef TRACKER_EXTRA_H_
#define TRACKER_EXTRA_H_

/*
 * Polyend Tracker CE - loadable extras (.ptextra)
 *
 * A .ptextra is a position-independent ARM Thumb-2 binary with a 128-byte
 * header, placed on the SD card under /Extras. Config > Info > Extras opens a
 * file browser on that folder; the picked extra is loaded into executable RAM
 * (external RAM, executable) and runs full screen until Shift is
 * pressed. Shift is handled by the firmware, so an extra can never lock you in.
 *
 * An extra exports three C functions:
 *
 *   int32_t extra_init(const PtExtraHostApi* host, void* scratch, uint32_t scratch_size);
 *       Called once after loading. Upload textures to RAM_G here. Return 0
 *       to start, anything else to abort (the firmware shows an error).
 *
 *   void extra_frame(uint32_t time_ms);
 *       Called once per panel frame (~60 Hz). The firmware has already
 *       opened a display list with VERTEX_FORMAT(0) and a black CLEAR, and
 *       closes and shows it after you return. Emit FT81x display-list words
 *       with host->dl() (the PTD_* macros below build them). Budget: about
 *       2000 words per frame. time_ms counts from extra start.
 *
 *   void extra_exit(void);
 *       Optional. Called when the user leaves (Shift, or Shift+Pattern for
 *       input extras) and when the Tracker is switched off while the extra
 *       runs, so it can save its work with write_file().
 *
 * Music: an extra can carry a ProTracker .mod (`// PTEXTRA_MOD: tune.mod` in
 * the source; the builder appends it to the .ptextra). The firmware plays it
 * with its own ProTracker 2.3D replayer, starting right after extra_init
 * returns, and stops it when the extra ends. The tracker's own song is
 * stopped before an extra starts and is never touched. Use host->mod_state()
 * to sync effects to the music (API version 2).
 *
 * Input (API version 3): an extra built with `// PTEXTRA_FLAGS: INPUT` gets
 * every button, pad and the jog wheel through host->poll_input(), and can
 * write files. Shift is then an ordinary button for the extra; such an extra
 * is left with Shift+Pattern (hold Shift, press Pattern).
 *
 * Rules for the code (the builder enforces what it can):
 *   - no libc: use the host functions for math, memory and files
 *   - keep globals `static` (or build with the builder, which hides them),
 *     and don't put pointers in initialized data - there is no relocation
 *   - code + bss: max 128 KB on Tracker+/Mini, 32 KB on the original Tracker
 *   - bitmap handles: only use host->first_bitmap_handle ..
 *     first_bitmap_handle + bitmap_handle_count - 1; the others belong to
 *     the GUI fonts and ROM fonts
 *   - RAM_G: write only inside [host->ramg_base, host->ramg_base + host->ramg_size)
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PTEXTRA_MAGIC 0x52545850u /* "PXTR" little endian */
#define PTEXTRA_FORMAT_VERSION 1
#define PTEXTRA_API_VERSION 3
#define PTEXTRA_HEADER_SIZE 128

#define PTEXTRA_FLAG_IMX_ONLY 0x0001 /* needs Tracker+/Mini (code > 32 KB, or M7 speed) */
#define PTEXTRA_FLAG_INPUT 0x0002    /* wants all input (API 3); left with Shift+Pattern */

/* host->poll_input() event types (API version 3) */
#define PTD_INPUT_BUTTON_PRESS 1   /* index = button number, see below */
#define PTD_INPUT_BUTTON_RELEASE 2
#define PTD_INPUT_PAD_PRESS 3      /* index = pad 0-47, row by row from the top left */
#define PTD_INPUT_PAD_RELEASE 4
#define PTD_INPUT_JOG 5            /* value = steps, + clockwise */

/* Button numbers (same as the firmware's interfaceButtonN) */
#define PTD_BUTTON_F1 0            /* F1..F8 = 0..7, the keys under the screen */
#define PTD_BUTTON_PARAMS 8
#define PTD_BUTTON_PERFORMANCE 9
#define PTD_BUTTON_FILE 10
#define PTD_BUTTON_CONFIG 11
#define PTD_BUTTON_MASTER 12
#define PTD_BUTTON_SAMPLE_LOAD 13
#define PTD_BUTTON_SAMPLE_PLAY 14
#define PTD_BUTTON_SAMPLE_EDIT 15
#define PTD_BUTTON_SAMPLE_REC 16
#define PTD_BUTTON_SONG 17
#define PTD_BUTTON_NOTE 18
#define PTD_BUTTON_INSTR 19
#define PTD_BUTTON_FX2 20
#define PTD_BUTTON_FX1 21
#define PTD_BUTTON_PATTERN 22
#define PTD_BUTTON_PLAY 23
#define PTD_BUTTON_COPY 24
#define PTD_BUTTON_INSERT 25
#define PTD_BUTTON_UP 26
#define PTD_BUTTON_DELETE 27
#define PTD_BUTTON_REC 28
#define PTD_BUTTON_SHIFT 29
#define PTD_BUTTON_LEFT 30
#define PTD_BUTTON_DOWN 31
#define PTD_BUTTON_RIGHT 32

typedef struct __attribute__((packed, aligned(4)))
{
    uint32_t magic;          /* +0x00 PTEXTRA_MAGIC */
    uint16_t format_version; /* +0x04 PTEXTRA_FORMAT_VERSION */
    uint16_t flags;          /* +0x06 PTEXTRA_FLAG_* */
    char name[32];           /* +0x08 */
    char author[16];         /* +0x28 */
    uint32_t scratch_size;   /* +0x38 working RAM requested, bytes */
    uint32_t code_offset;    /* +0x3C file offset of the code (128) */
    uint32_t code_size;      /* +0x40 .text + .rodata + .data */
    uint32_t bss_size;       /* +0x44 zeroed after the code */
    uint32_t init_offset;    /* +0x48 extra_init, relative to code base */
    uint32_t frame_offset;   /* +0x4C extra_frame */
    uint32_t exit_offset;    /* +0x50 extra_exit, 0xFFFFFFFF if absent */
    uint32_t mod_offset;     /* +0x54 file offset of an embedded .mod, 0 if none */
    uint32_t mod_size;       /* +0x58 its size in bytes */
    uint32_t required_api;   /* +0x5C lowest host API version the extra needs (0 = 1) */
    uint8_t reserved[32];    /* +0x60 */
} PtExtraHeader;

/* One input event (API version 3) */
typedef struct
{
    uint8_t type;  /* PTD_INPUT_* */
    uint8_t index; /* button or pad number */
    int16_t value; /* jog steps */
} PtExtraInput;

/* What the music is doing, for syncing effects. */
typedef struct
{
    uint8_t playing;   /* 1 while the extra's module plays */
    uint8_t position;  /* order list position */
    uint8_t pattern;
    uint8_t row;       /* 0-63 */
    uint8_t tick;      /* 0 .. speed-1 */
    uint8_t speed;     /* ticks per row */
    uint16_t bpm;
    uint8_t vu[4];     /* per channel: jumps to the note's volume (0-64), falls back */
    uint8_t hits[4];   /* per channel: +1 for every note started (wraps at 255) */
    uint32_t loops;    /* times the song has wrapped to its restart */
} PtExtraModState;

typedef struct
{
    uint32_t api_version;   /* PTEXTRA_API_VERSION */
    uint16_t screen_width;  /* 800 */
    uint16_t screen_height; /* 480 */
    uint8_t platform;       /* 0 = original Tracker (MK66), 1 = Tracker+ / Mini (IMX) */
    uint8_t first_bitmap_handle;
    uint8_t bitmap_handle_count;
    uint8_t pad_count; /* 48 */

    /* FT812 graphics memory the extra owns while it runs */
    uint32_t ramg_base;
    uint32_t ramg_size;

    /* Display list (only valid inside extra_frame) */
    void (*dl)(uint32_t word);
    /* Text in a GUI font (1-5) or a ROM font (16-31), like CMD_TEXT */
    void (*text)(int16_t x, int16_t y, uint8_t font, uint16_t options, const char* s);

    /* Graphics memory. Writes outside the extra's region are ignored.
       Reads may go anywhere, e.g. the ROM font table pointer at 0x2FFFFC. */
    void (*ramg_write)(uint32_t addr, const void* data, uint32_t size);
    void (*ramg_read)(uint32_t addr, void* data, uint32_t size);

    uint32_t (*millis)(void);
    uint32_t (*micros)(void);

    float (*sinf)(float);
    float (*cosf)(float);
    float (*sqrtf)(float);
    float (*fabsf)(float);
    float (*fmodf)(float, float);
    float (*atan2f)(float, float);
    float (*powf)(float, float);
    float (*floorf)(float);

    void* (*memcpy)(void*, const void*, size_t);
    void* (*memset)(void*, int, size_t);
    int (*rand)(void);

    /* Read a file. A path without a leading '/' is relative to the folder
       the .ptextra was loaded from, so assets can sit next to the extra.
       Returns bytes read or -1. Only call from extra_init: SD access stalls
       the frame. */
    int32_t (*read_file)(const char* path, void* buffer, uint32_t max_bytes);

    /* Pad grid backlight: pad 0-47, level 0 = off, 1-255 = brightness
       (scaled to the user's pad brightness setting). */
    void (*pad_light)(uint8_t pad, uint8_t level);

    /* API 1 meaning: the tracker song. The firmware stops it before an extra
       starts, so this is 0 from API version 2 on; use mod_state(). */
    uint8_t (*is_song_playing)(void);

    /* ---- API version 2: the extra's music ---------------------------------- */

    /* Fills `out`. Fields are zero when the extra has no module. */
    void (*mod_state)(PtExtraModState* out);
    /* Start the module at an order position (0 = from the top), or stop it.
       mod_play returns 0 if there is no module. */
    uint8_t (*mod_play)(uint8_t position);
    void (*mod_stop)(void);
    /* Load another .mod (path as for read_file) in place of the current one;
       it is not started. Returns 0 on success. Stalls a moment: call it
       between parts, not every frame. */
    int32_t (*mod_load)(const char* path);

    /* ---- API version 3: input and saving --------------------------------- */

    /* Next input event: returns 1 and fills `out`, or 0 when there is none.
       Only extras with PTEXTRA_FLAG_INPUT get events. */
    int32_t (*poll_input)(PtExtraInput* out);
    /* Files; paths as for read_file. These may be called from extra_frame and
       extra_exit too (the frame stalls while the SD card works). */
    /* Creates or replaces a file. Returns bytes written or -1. */
    int32_t (*write_file)(const char* path, const void* data, uint32_t size);
    /* Names of the files (not folders) in a folder, each ending in a 0 byte,
       packed into `names`. Returns how many, or -1 if the folder is missing. */
    int32_t (*list_dir)(const char* path, char* names, uint32_t names_size, uint32_t max_names);
    /* 1 if the file or folder exists */
    int32_t (*file_exists)(const char* path);
    /* Creates a folder; 0 on success or if it already exists */
    int32_t (*make_dir)(const char* path);
} PtExtraHostApi;

typedef int32_t (*PtExtraInitFunc)(const PtExtraHostApi* host, void* scratch, uint32_t scratch_size);
typedef void (*PtExtraFrameFunc)(uint32_t time_ms);
typedef void (*PtExtraExitFunc)(void);

/* --------------------------------------------------------------------------
 * FT81x display-list words (same encoding as the FT81x programmer's guide).
 * -------------------------------------------------------------------------- */

/* primitives for PTD_BEGIN */
#define PTD_BITMAPS 1u
#define PTD_POINTS 2u
#define PTD_LINES 3u
#define PTD_LINE_STRIP 4u
#define PTD_EDGE_STRIP_R 5u
#define PTD_EDGE_STRIP_L 6u
#define PTD_EDGE_STRIP_A 7u
#define PTD_EDGE_STRIP_B 8u
#define PTD_RECTS 9u

/* bitmap formats */
#define PTD_ARGB1555 0u
#define PTD_L1 1u
#define PTD_L4 2u
#define PTD_L8 3u
#define PTD_RGB332 4u
#define PTD_ARGB2 5u
#define PTD_ARGB4 6u
#define PTD_RGB565 7u
#define PTD_L2 17u

/* filter / wrap */
#define PTD_NEAREST 0u
#define PTD_BILINEAR 1u
#define PTD_BORDER 0u
#define PTD_REPEAT 1u

/* blend factors */
#define PTD_ZERO 0u
#define PTD_ONE 1u
#define PTD_SRC_ALPHA 2u
#define PTD_DST_ALPHA 3u
#define PTD_ONE_MINUS_SRC_ALPHA 4u
#define PTD_ONE_MINUS_DST_ALPHA 5u

#define PTD_VERTEX2F(x, y) ((1UL << 30) | (((uint32_t)(x) & 32767UL) << 15) | ((uint32_t)(y) & 32767UL))
#define PTD_VERTEX2II(x, y, handle, cell)                                                          \
    ((2UL << 30) | (((uint32_t)(x) & 511UL) << 21) | (((uint32_t)(y) & 511UL) << 12) |             \
     (((uint32_t)(handle) & 31UL) << 7) | ((uint32_t)(cell) & 127UL))
#define PTD_BITMAP_SOURCE(addr) ((1UL << 24) | ((uint32_t)(addr) & 4194303UL))
#define PTD_CLEAR_COLOR_RGB(r, g, b)                                                               \
    ((2UL << 24) | (((uint32_t)(r) & 255UL) << 16) | (((uint32_t)(g) & 255UL) << 8) | ((uint32_t)(b) & 255UL))
#define PTD_TAG(s) ((3UL << 24) | ((uint32_t)(s) & 255UL))
#define PTD_COLOR_RGB(r, g, b)                                                                     \
    ((4UL << 24) | (((uint32_t)(r) & 255UL) << 16) | (((uint32_t)(g) & 255UL) << 8) | ((uint32_t)(b) & 255UL))
#define PTD_BITMAP_HANDLE(h) ((5UL << 24) | ((uint32_t)(h) & 31UL))
#define PTD_CELL(c) ((6UL << 24) | ((uint32_t)(c) & 127UL))
#define PTD_BITMAP_LAYOUT(format, stride, height)                                                  \
    ((7UL << 24) | (((uint32_t)(format) & 31UL) << 19) | (((uint32_t)(stride) & 1023UL) << 9) |    \
     ((uint32_t)(height) & 511UL))
#define PTD_BITMAP_SIZE(filter, wrapx, wrapy, w, h)                                                \
    ((8UL << 24) | (((uint32_t)(filter) & 1UL) << 20) | (((uint32_t)(wrapx) & 1UL) << 19) |        \
     (((uint32_t)(wrapy) & 1UL) << 18) | (((uint32_t)(w) & 511UL) << 9) | ((uint32_t)(h) & 511UL))
#define PTD_ALPHA_FUNC(func, ref) ((9UL << 24) | (((uint32_t)(func) & 7UL) << 8) | ((uint32_t)(ref) & 255UL))
#define PTD_STENCIL_FUNC(func, ref, mask)                                                          \
    ((10UL << 24) | (((uint32_t)(func) & 7UL) << 16) | (((uint32_t)(ref) & 255UL) << 8) |         \
     ((uint32_t)(mask) & 255UL))
#define PTD_BLEND_FUNC(src, dst) ((11UL << 24) | (((uint32_t)(src) & 7UL) << 3) | ((uint32_t)(dst) & 7UL))
#define PTD_STENCIL_OP(sfail, spass) ((12UL << 24) | (((uint32_t)(sfail) & 7UL) << 3) | ((uint32_t)(spass) & 7UL))
#define PTD_POINT_SIZE(size16) ((13UL << 24) | ((uint32_t)(size16) & 8191UL))
#define PTD_LINE_WIDTH(width16) ((14UL << 24) | ((uint32_t)(width16) & 4095UL))
#define PTD_CLEAR_COLOR_A(a) ((15UL << 24) | ((uint32_t)(a) & 255UL))
#define PTD_COLOR_A(a) ((16UL << 24) | ((uint32_t)(a) & 255UL))
#define PTD_CLEAR_STENCIL(s) ((17UL << 24) | ((uint32_t)(s) & 255UL))
#define PTD_STENCIL_MASK(m) ((19UL << 24) | ((uint32_t)(m) & 255UL))
/* A..F: A,B,D,E are signed 8.8 fixed point, C,F signed 15.8 */
#define PTD_BITMAP_TRANSFORM_A(a) ((21UL << 24) | ((uint32_t)(a) & 131071UL))
#define PTD_BITMAP_TRANSFORM_B(b) ((22UL << 24) | ((uint32_t)(b) & 131071UL))
#define PTD_BITMAP_TRANSFORM_C(c) ((23UL << 24) | ((uint32_t)(c) & 16777215UL))
#define PTD_BITMAP_TRANSFORM_D(d) ((24UL << 24) | ((uint32_t)(d) & 131071UL))
#define PTD_BITMAP_TRANSFORM_E(e) ((25UL << 24) | ((uint32_t)(e) & 131071UL))
#define PTD_BITMAP_TRANSFORM_F(f) ((26UL << 24) | ((uint32_t)(f) & 16777215UL))
#define PTD_SCISSOR_XY(x, y) ((27UL << 24) | (((uint32_t)(x) & 2047UL) << 11) | ((uint32_t)(y) & 2047UL))
#define PTD_SCISSOR_SIZE(w, h) ((28UL << 24) | (((uint32_t)(w) & 4095UL) << 12) | ((uint32_t)(h) & 4095UL))
#define PTD_BEGIN(prim) ((31UL << 24) | ((uint32_t)(prim) & 15UL))
#define PTD_COLOR_MASK(r, g, b, a)                                                                 \
    ((32UL << 24) | (((uint32_t)(r) & 1UL) << 3) | (((uint32_t)(g) & 1UL) << 2) |                 \
     (((uint32_t)(b) & 1UL) << 1) | ((uint32_t)(a) & 1UL))
#define PTD_END() (33UL << 24)
#define PTD_SAVE_CONTEXT() (34UL << 24)
#define PTD_RESTORE_CONTEXT() (35UL << 24)
#define PTD_CLEAR(c, s, t)                                                                         \
    ((38UL << 24) | (((uint32_t)(c) & 1UL) << 2) | (((uint32_t)(s) & 1UL) << 1) | ((uint32_t)(t) & 1UL))
#define PTD_VERTEX_FORMAT(frac) ((39UL << 24) | ((uint32_t)(frac) & 7UL))
#define PTD_BITMAP_LAYOUT_H(stride, height) ((40UL << 24) | (((uint32_t)(stride) & 3UL) << 2) | ((uint32_t)(height) & 3UL))
#define PTD_BITMAP_SIZE_H(w, h) ((41UL << 24) | (((uint32_t)(w) & 3UL) << 2) | ((uint32_t)(h) & 3UL))
#define PTD_PALETTE_SOURCE(addr) ((42UL << 24) | ((uint32_t)(addr) & 4194303UL))
#define PTD_VERTEX_TRANSLATE_X(x) ((43UL << 24) | ((uint32_t)(x) & 131071UL))
#define PTD_VERTEX_TRANSLATE_Y(y) ((44UL << 24) | ((uint32_t)(y) & 131071UL))

/* FT81x ROM font metric table */
#define PTD_ROM_FONT_TABLE_POINTER 0x2FFFFCu
#define PTD_ROM_FONT_BLOCK_SIZE 148u

#ifdef __cplusplus
}
#endif

#endif /* TRACKER_EXTRA_H_ */
