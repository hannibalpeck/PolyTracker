// PTEXTRA_NAME: Divine Stylers
// PTEXTRA_AUTHOR: Divine Stylers
// PTEXTRA_SCRATCH: 4096
//
// The startup splash in motion: copper bars sliding up and down and the text
// "Divine Stylers presents" rolling in a sine wave, one letter at a time.
// Build:  python3 ptextra_builder.py examples/splash.c -o build/Extras/splash.ptextra

#include "tracker_extra.h"

static const PtExtraHostApi* H;

#define DL(word) H->dl(word)

static const float kPi = 3.14159265f;

static const char kText[] = "Divine Stylers presents";

#define TEXT_FONT 29

static uint8_t fontWidths[128];
static int textWidth;

static const uint8_t kCopper[6][3] = { { 0xE0, 0x30, 0x20 }, { 0xE0, 0x80, 0x20 },
                                       { 0xE0, 0xC8, 0x20 }, { 0x30, 0xC0, 0x40 },
                                       { 0x20, 0x90, 0xE0 }, { 0x70, 0x40, 0xE0 } };

static uint32_t read32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t extra_init(const PtExtraHostApi* host, void* scratch, uint32_t scratch_size)
{
    (void)scratch;
    (void)scratch_size;
    H = host;
    if (host->api_version < PTEXTRA_API_VERSION)
        return 1;

    /* ROM font metrics: the first 128 bytes are the character widths */
    uint8_t ptr[4];
    host->ramg_read(PTD_ROM_FONT_TABLE_POINTER, ptr, 4);
    uint8_t block[PTD_ROM_FONT_BLOCK_SIZE];
    host->ramg_read(read32(ptr) + (TEXT_FONT - 16) * PTD_ROM_FONT_BLOCK_SIZE, block, sizeof(block));
    for (int i = 0; i < 128; i++)
        fontWidths[i] = block[i];

    textWidth = 0;
    for (const char* p = kText; *p; p++)
        textWidth += fontWidths[(uint8_t)*p & 127];
    return 0;
}

/* six stripes, each one slightly behind the one above it */
static void bars(float t, float baseY, float direction)
{
    DL(PTD_BEGIN(PTD_RECTS));
    for (int i = 0; i < 6; i++)
    {
        float y = baseY + 9.0f * i + direction * 40.0f * H->sinf(t * 1.6f + i * 0.55f);
        DL(PTD_COLOR_RGB(kCopper[i][0], kCopper[i][1], kCopper[i][2]));
        DL(PTD_VERTEX2F(150, (int)y));
        DL(PTD_VERTEX2F(650, (int)y + 6));
    }
    DL(PTD_END());
}

void extra_frame(uint32_t time_ms)
{
    float t = time_ms * 0.001f;

    bars(t, 90.0f, 1.0f);
    bars(t, 340.0f, -1.0f);

    /* the text, one letter at a time on a sine wave */
    float x = (H->screen_width - textWidth) * 0.5f;
    for (int i = 0; kText[i]; i++)
    {
        char s[2] = { kText[i], 0 };
        float y = 240.0f + 45.0f * H->sinf(t * 3.0f + i * 0.5f);
        float shade = 0.5f + 0.5f * H->sinf(t * 3.0f + i * 0.5f);
        int c = 160 + (int)(95.0f * shade);
        DL(PTD_COLOR_RGB(c, c, 0xFF));
        H->text((int16_t)x, (int16_t)y, TEXT_FONT, 1024 /* OPT_CENTERY */, s);
        x += fontWidths[(uint8_t)kText[i] & 127];
    }

    (void)kPi;
}

void extra_exit(void)
{
}
