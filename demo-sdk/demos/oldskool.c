// PTDEMO_NAME: Oldskool
// PTDEMO_AUTHOR: Tracker CE
// PTDEMO_SCRATCH: 8192
// PTDEMO_MOD: oldskool.mod
//
// Rotozoomer, copper bars, vector-ball cube and a sine scroller, synced to
// the music: kick drums flash the copper bars, snares pump the cube, the
// arpeggio channel swings the scroller.
// Build:  python3 ptdemo_builder.py demos/oldskool.c -o build/Demos/oldskool.ptdemo
// (oldskool.mod is made by tools/make_oldskool_mod.py)

#include "tracker_demo.h"

static const PtDemoHostApi* H;

#define DL(word) H->dl(word)

static const float kPi = 3.14159265f;

/* ---------------------------------------------------------------- resources */

#define TEX_SIZE 64
#define TEX_BYTES (TEX_SIZE * TEX_SIZE * 2)

static uint32_t texAddr;
static uint8_t texHandle;
static uint8_t fontHandle;

static uint8_t fontWidths[128];
static uint32_t fontFormat, fontStride, fontWidth, fontHeight, fontSource;
static uint32_t scrollTextWidth;
static int fontOk;

#define FONT_SCALE 2

static const char kScrollText[] =
  "        *** TRACKER CE DEMO LOADED FROM THE SD CARD ***    "
  "ROTOZOOMER  -  COPPER BARS  -  VECTOR BALLS  -  SINE SCROLLER    "
  "ALL DRAWN BY THE FT812 FROM ONE DISPLAY LIST PER FRAME    "
  "PRESS SHIFT TO EXIT    GREETINGS TO ALL TRACKER CE HACKERS ...    ";

static uint8_t padState[48];

/* music sync: decaying pulses, set when a channel starts a note */
static PtDemoModState mod;
static uint8_t lastHits[4];
static float kickPulse, snarePulse, arpPulse;
static uint32_t lastTimeMs;

static uint32_t read32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t demo_init(const PtDemoHostApi* host, void* scratch, uint32_t scratch_size)
{
    H = host;
    if (host->api_version < PTDEMO_API_VERSION || host->bitmap_handle_count < 2 ||
        scratch_size < TEX_BYTES || host->ramg_size < TEX_BYTES)
    {
        return 1;
    }

    texHandle = host->first_bitmap_handle;
    fontHandle = host->first_bitmap_handle + 1;
    texAddr = host->ramg_base;

    /* XOR pattern on an 8x8 checker, built in scratch RAM, then uploaded */
    uint16_t* tex = (uint16_t*)scratch;
    for (int y = 0; y < TEX_SIZE; y++)
    {
        for (int x = 0; x < TEX_SIZE; x++)
        {
            int v = (x ^ y) & 63;
            int checker = ((x >> 3) ^ (y >> 3)) & 1;
            int r = checker ? 40 + v * 3 : 16 + v;
            int g = 8 + v;
            int b = checker ? 120 + v * 2 : 60 + v * 2;
            tex[y * TEX_SIZE + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    host->ramg_write(texAddr, tex, TEX_BYTES);

    /* ROM font 31 metrics: 128 widths, then format, stride, width, height, data */
    uint8_t ptr[4];
    host->ramg_read(PTD_ROM_FONT_TABLE_POINTER, ptr, 4);
    uint8_t block[PTD_ROM_FONT_BLOCK_SIZE];
    host->ramg_read(read32(ptr) + (31 - 16) * PTD_ROM_FONT_BLOCK_SIZE, block, sizeof(block));

    for (int i = 0; i < 128; i++)
        fontWidths[i] = block[i];
    fontFormat = read32(block + 128);
    fontStride = read32(block + 132);
    fontWidth = read32(block + 136);
    fontHeight = read32(block + 140);
    fontSource = read32(block + 144);

    scrollTextWidth = 0;
    for (const char* p = kScrollText; *p; p++)
        scrollTextWidth += fontWidths[(uint8_t)*p & 127] * FONT_SCALE;
    fontOk = scrollTextWidth > 0 && fontHeight > 0 && fontHeight < 200;

    for (int i = 0; i < 48; i++)
        padState[i] = 0;
    for (int c = 0; c < 4; c++)
        lastHits[c] = 0;
    kickPulse = snarePulse = arpPulse = 0.0f;
    lastTimeMs = 0;
    return 0;
}

/* ----------------------------------------------------------------- effects */

static void rotozoomer(float phase)
{
    const int W = H->screen_width, Hh = H->screen_height;
    float angle = phase * 0.7f;
    float zoom = 0.45f + 0.30f * H->sinf(phase * 0.5f); /* texels per pixel */
    float c = H->cosf(angle) * zoom;
    float s = H->sinf(angle) * zoom;
    float ox = H->fmodf(phase * 40.0f, (float)TEX_SIZE);
    float oy = H->fmodf(phase * 23.0f, (float)TEX_SIZE);

    /* u = A*x + B*y + C, v = D*x + E*y + F (8.8 fixed), centred on screen */
    int32_t A = (int32_t)(c * 256.0f);
    int32_t B = (int32_t)(s * 256.0f);
    int32_t D = -B;
    int32_t E = A;
    int32_t C = (int32_t)(ox * 256.0f) - A * (W / 2) - B * (Hh / 2);
    int32_t F = (int32_t)(oy * 256.0f) - D * (W / 2) - E * (Hh / 2);

    DL(PTD_SAVE_CONTEXT());
    DL(PTD_BITMAP_HANDLE(texHandle));
    DL(PTD_BITMAP_SOURCE(texAddr));
    DL(PTD_BITMAP_LAYOUT_H(0, 0));
    DL(PTD_BITMAP_LAYOUT(PTD_RGB565, TEX_SIZE * 2, TEX_SIZE));
    DL(PTD_BITMAP_SIZE_H(W >> 9, Hh >> 9));
    DL(PTD_BITMAP_SIZE(PTD_NEAREST, PTD_REPEAT, PTD_REPEAT, W, Hh));
    DL(PTD_BITMAP_TRANSFORM_A(A));
    DL(PTD_BITMAP_TRANSFORM_B(B));
    DL(PTD_BITMAP_TRANSFORM_C(C));
    DL(PTD_BITMAP_TRANSFORM_D(D));
    DL(PTD_BITMAP_TRANSFORM_E(E));
    DL(PTD_BITMAP_TRANSFORM_F(F));
    DL(PTD_COLOR_RGB(150, 150, 190));
    DL(PTD_BEGIN(PTD_BITMAPS));
    DL(PTD_VERTEX2F(0, 0));
    DL(PTD_END());
    DL(PTD_RESTORE_CONTEXT());
}

static const uint8_t kBarColors[3][3] = { { 255, 60, 40 }, { 60, 255, 90 }, { 70, 110, 255 } };

static void copperBars(float t)
{
    const int rows = 16;
    const float boost = 1.0f + 0.9f * kickPulse;
    DL(PTD_SAVE_CONTEXT());
    DL(PTD_BLEND_FUNC(PTD_SRC_ALPHA, PTD_ONE));
    DL(PTD_LINE_WIDTH(16));
    DL(PTD_BEGIN(PTD_RECTS));
    for (int bar = 0; bar < 3; bar++)
    {
        int yc = H->screen_height / 2 + (int)(170.0f * H->sinf(t * 1.3f + bar * 0.9f));
        int y0 = yc - rows;
        for (int k = 0; k < rows; k++)
        {
            float i = H->sinf(kPi * (k + 0.5f) / rows) * boost;
            if (i > 1.0f)
                i = 1.0f;
            DL(PTD_COLOR_RGB((int)(kBarColors[bar][0] * i), (int)(kBarColors[bar][1] * i),
                             (int)(kBarColors[bar][2] * i)));
            DL(PTD_VERTEX2F(0, y0 + k * 2));
            DL(PTD_VERTEX2F(H->screen_width - 1, y0 + k * 2 + 1));
        }
    }
    DL(PTD_END());
    DL(PTD_RESTORE_CONTEXT());
}

static const int8_t kVerts[8][3] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
                                     { -1, -1, 1 },  { 1, -1, 1 },  { 1, 1, 1 },  { -1, 1, 1 } };
static const uint8_t kEdges[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
                                       { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

static void cube(float t)
{
    const float size = 70.0f * (1.0f + 0.25f * snarePulse), dist = 420.0f, focal = 420.0f,
                ball = 16.0f;
    float cx = H->screen_width / 2 + 150.0f * H->sinf(t * 0.6f);
    float cy = 180.0f + 20.0f * H->sinf(t * 1.7f);
    float sx = H->sinf(t * 0.9f), cxr = H->cosf(t * 0.9f);
    float sy = H->sinf(t * 1.3f), cyr = H->cosf(t * 1.3f);
    float sz = H->sinf(t * 0.4f), czr = H->cosf(t * 0.4f);

    int px[8], py[8];
    float pz[8];
    int order[8];

    for (int i = 0; i < 8; i++)
    {
        float x = kVerts[i][0] * size, y = kVerts[i][1] * size, z = kVerts[i][2] * size;
        float y1 = y * cxr - z * sx, z1 = y * sx + z * cxr;
        float x2 = x * cyr + z1 * sy, z2 = -x * sy + z1 * cyr;
        float x3 = x2 * czr - y1 * sz, y3 = x2 * sz + y1 * czr;
        float k = focal / (z2 + dist);
        px[i] = (int)(cx + x3 * k);
        py[i] = (int)(cy + y3 * k);
        pz[i] = z2;
        order[i] = i;
    }
    for (int i = 1; i < 8; i++) /* far to near */
        for (int j = i; j > 0 && pz[order[j]] > pz[order[j - 1]]; j--)
        {
            int tmp = order[j];
            order[j] = order[j - 1];
            order[j - 1] = tmp;
        }

    DL(PTD_SAVE_CONTEXT());
    DL(PTD_BLEND_FUNC(PTD_SRC_ALPHA, PTD_ONE));
    DL(PTD_COLOR_RGB(60, 200, 255));
    DL(PTD_LINE_WIDTH(24));
    DL(PTD_BEGIN(PTD_LINES));
    for (int e = 0; e < 12; e++)
    {
        DL(PTD_VERTEX2F(px[kEdges[e][0]], py[kEdges[e][0]]));
        DL(PTD_VERTEX2F(px[kEdges[e][1]], py[kEdges[e][1]]));
    }
    DL(PTD_END());

    DL(PTD_BLEND_FUNC(PTD_SRC_ALPHA, PTD_ONE_MINUS_SRC_ALPHA));
    DL(PTD_BEGIN(PTD_POINTS));
    for (int n = 0; n < 8; n++)
    {
        int i = order[n];
        float r = ball * focal / (pz[i] + dist);
        float depth = 0.55f + 0.45f * (size - pz[i]) / (2.0f * size); /* near = bright */
        if (depth > 1.0f)
            depth = 1.0f; /* corners reach +-size*sqrt(3); keep colours <= 255 */
        if (depth < 0.0f)
            depth = 0.0f;
        DL(PTD_COLOR_RGB((int)(230 * depth), (int)(40 * depth), (int)(90 * depth)));
        DL(PTD_POINT_SIZE((int)(r * 16.0f)));
        DL(PTD_VERTEX2F(px[i], py[i]));
        DL(PTD_COLOR_RGB(255, (int)(170 + 80 * depth), (int)(200 + 55 * depth)));
        DL(PTD_POINT_SIZE((int)(r * 16.0f * 0.33f)));
        DL(PTD_VERTEX2F(px[i] - (int)(r * 0.35f), py[i] - (int)(r * 0.35f)));
    }
    DL(PTD_END());
    DL(PTD_RESTORE_CONTEXT());
}

#define MAX_GLYPHS 64
static int16_t glyphX[MAX_GLYPHS], glyphY[MAX_GLYPHS];
static uint8_t glyphC[MAX_GLYPHS];

static void scroller(float t)
{
    if (!fontOk)
        return;

    const int textLen = (int)sizeof(kScrollText) - 1;
    const int W = H->screen_width;
    uint32_t pos = (uint32_t)(t * 220.0f) % scrollTextWidth;

    int idx = 0;
    int32_t x = -(int32_t)pos;
    while (x + fontWidths[(uint8_t)kScrollText[idx] & 127] * FONT_SCALE <= 0)
    {
        x += fontWidths[(uint8_t)kScrollText[idx] & 127] * FONT_SCALE;
        idx = (idx + 1) % textLen;
    }

    int glyphH = (int)fontHeight * FONT_SCALE;
    int baseY = H->screen_height - glyphH - 55;
    int count = 0;

    while (x < W && count < MAX_GLYPHS)
    {
        uint8_t c = (uint8_t)kScrollText[idx] & 127;
        if (c != ' ')
        {
            glyphX[count] = (int16_t)x;
            glyphY[count] =
              (int16_t)(baseY + (int)((34.0f + 20.0f * arpPulse) * H->sinf(x * 0.011f + t * 3.2f)));
            glyphC[count] = c;
            count++;
        }
        x += fontWidths[c] * FONT_SCALE;
        idx = (idx + 1) % textLen;
    }

    uint32_t w2 = fontWidth * FONT_SCALE, h2 = fontHeight * FONT_SCALE;
    DL(PTD_SAVE_CONTEXT());
    DL(PTD_BITMAP_HANDLE(fontHandle));
    DL(PTD_BITMAP_SOURCE(fontSource));
    DL(PTD_BITMAP_LAYOUT_H(fontStride >> 10, fontHeight >> 9));
    DL(PTD_BITMAP_LAYOUT(fontFormat, fontStride, fontHeight));
    DL(PTD_BITMAP_SIZE_H(w2 >> 9, h2 >> 9));
    DL(PTD_BITMAP_SIZE(PTD_NEAREST, PTD_BORDER, PTD_BORDER, w2, h2));
    DL(PTD_BITMAP_TRANSFORM_A(256 / FONT_SCALE));
    DL(PTD_BITMAP_TRANSFORM_E(256 / FONT_SCALE));
    DL(PTD_BEGIN(PTD_BITMAPS));

    DL(PTD_COLOR_RGB(0, 0, 0)); /* drop shadow */
    DL(PTD_COLOR_A(150));
    for (int i = 0; i < count; i++)
    {
        DL(PTD_CELL(glyphC[i]));
        DL(PTD_VERTEX2F(glyphX[i] + 6, glyphY[i] + 6));
    }

    DL(PTD_COLOR_A(255));
    for (int i = 0; i < count; i++)
    {
        float h = glyphX[i] * 0.006f + t * 2.0f;
        DL(PTD_COLOR_RGB((int)(128 + 127 * H->sinf(h)), (int)(128 + 127 * H->sinf(h + 2.094f)),
                         (int)(128 + 127 * H->sinf(h + 4.189f))));
        DL(PTD_CELL(glyphC[i]));
        DL(PTD_VERTEX2F(glyphX[i], glyphY[i]));
    }
    DL(PTD_END());
    DL(PTD_RESTORE_CONTEXT());
}

/* A sine wave rolling across the 12x4 pad grid; only changes are sent. */
static void pads(float t)
{
    for (int n = 0; n < 48 && n < H->pad_count; n++)
    {
        int col = n % 12, row = n / 12;
        float height = 1.5f + 1.5f * H->sinf(col * 0.55f + t * 5.0f);
        uint8_t lit = (3 - row) <= (int)(height + 0.5f) ? 255 : 0;
        if (lit != padState[n])
        {
            padState[n] = lit;
            H->pad_light((uint8_t)n, lit);
        }
    }
}

/* -------------------------------------------------------------- entry points */

static void syncToMusic(uint32_t time_ms)
{
    float dt = (time_ms - lastTimeMs) * 0.001f;
    lastTimeMs = time_ms;
    float decay = 1.0f - 6.0f * dt; /* pulses fade in ~1/6 s */
    if (decay < 0.0f)
        decay = 0.0f;
    kickPulse *= decay;
    snarePulse *= decay;
    arpPulse *= decay;

    if (H->api_version < 2)
        return; /* older firmware: no music, effects run unsynced */
    H->mod_state(&mod);
    if (!mod.playing)
        return;

    /* channel 0 carries kick and snare: tell them apart by the row in the bar */
    if (mod.hits[0] != lastHits[0])
    {
        int step = mod.row % 16;
        if (step == 4 || step == 12)
            snarePulse = 1.0f;
        else
            kickPulse = 1.0f;
    }
    if (mod.hits[2] != lastHits[2])
        arpPulse = 1.0f;
    for (int c = 0; c < 4; c++)
        lastHits[c] = mod.hits[c];
}

void demo_frame(uint32_t time_ms)
{
    float t = time_ms * 0.001f;
    syncToMusic(time_ms);
    rotozoomer(t);
    copperBars(t);
    cube(t);
    scroller(t);
    pads(t);
}

void demo_exit(void)
{
    /* the firmware clears the pads and restores the GUI; nothing to free */
}
