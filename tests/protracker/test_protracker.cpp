// Host-side tests for the ProTracker module loader/saver and replayer.
//
// Build and run (from this directory):
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I../../core/protracker test_protracker.cpp ../../core/protracker/*.cpp -o test_protracker && ./test_protracker

#include "ptModule.h"
#include "ptReplayer.h"
#include "ptSamples.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace protracker;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        gChecks++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            gFailures++;                                                                           \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                               \
        }                                                                                          \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                      \
    do                                                                                             \
    {                                                                                              \
        gChecks++;                                                                                 \
        double va = (a), vb = (b);                                                                 \
        if (fabs(va - vb) > (tol))                                                                 \
        {                                                                                          \
            gFailures++;                                                                           \
            printf("  FAIL %s:%d  %s = %g, expected %g +-%g\n", __FILE__, __LINE__, #a, va, vb,    \
                   (double)(tol));                                                                 \
        }                                                                                          \
    } while (0)

// ------------------------------------------------------------------------ memory file I/O
struct MemFile
{
    std::vector<uint8_t> data;
    size_t pos = 0;
};

static uint32_t memRead(void* ctx, void* dst, uint32_t n)
{
    MemFile* f = (MemFile*)ctx;
    size_t avail = f->data.size() > f->pos ? f->data.size() - f->pos : 0;
    size_t count = n < avail ? n : avail;
    memcpy(dst, f->data.data() + f->pos, count);
    f->pos += count;
    return (uint32_t)count;
}

static bool memWrite(void* ctx, const void* src, uint32_t n)
{
    MemFile* f = (MemFile*)ctx;
    const uint8_t* p = (const uint8_t*)src;
    f->data.insert(f->data.end(), p, p + n);
    return true;
}

static bool memSeek(void* ctx, uint32_t offset)
{
    MemFile* f = (MemFile*)ctx;
    if (offset > f->data.size())
        return false;
    f->pos = offset;
    return true;
}

static FileIo ioFor(MemFile& f)
{
    FileIo io;
    io.ctx = &f;
    io.read = memRead;
    io.write = memWrite;
    io.seek = memSeek;
    return io;
}

// ------------------------------------------------------------------------ helpers
struct Fixture
{
    std::vector<uint8_t> pool;
    Module mod;
    Replayer rep;

    explicit Fixture(float rate = 44100.0f)
      : pool(kPoolBytes)
    {
        mod.attach(pool.data(), pool.size());
        rep.init(&mod, rate);
        rep.setStereoSeparation(100); // channel 0 and 3 left only, 1 and 2 right only
    }

    // a 64 byte looping square wave in sample slot `s` (1-based in patterns)
    void makeSquare(int s, int amplitude = 100)
    {
        Sample& smp = mod.samples[s];
        strcpy(smp.name, "square");
        smp.length = 64;
        smp.volume = 64;
        smp.loopStart = 0;
        smp.loopLength = 64;
        int8_t* d = mod.sampleData(s);
        for (int i = 0; i < 64; i++)
            d[i] = (int8_t)(i < 32 ? amplitude : -amplitude);
        mod.fixSample(s);
    }

    void note(int pattern, int row, int channel, int sample, int period, int cmd = 0, int param = 0)
    {
        mod.pattern(pattern)[row * 4 + channel].set((uint8_t)sample, (uint16_t)period,
                                                    (uint8_t)cmd, (uint8_t)param);
    }
};

static std::vector<int16_t> renderLeft(Replayer& r, uint32_t frames)
{
    std::vector<int16_t> l(frames), rr(frames);
    r.render(l.data(), rr.data(), frames);
    return l;
}

static double zeroCrossingFrequency(const std::vector<int16_t>& s, float rate)
{
    int crossings = 0;
    for (size_t i = 1; i < s.size(); i++)
    {
        if ((s[i - 1] < 0) != (s[i] < 0))
            crossings++;
    }
    return (crossings / 2.0) / ((double)s.size() / rate);
}

static int peak(const std::vector<int16_t>& s, size_t from, size_t to)
{
    int p = 0;
    for (size_t i = from; i < to && i < s.size(); i++)
    {
        int a = abs(s[i]);
        if (a > p)
            p = a;
    }
    return p;
}

// ------------------------------------------------------------------------ tests
static void testRoundTrip()
{
    printf("round trip\n");
    Fixture a;
    strcpy(a.mod.title, "TEST SONG");
    a.makeSquare(0, 90);
    a.mod.samples[0].fineTune = 3;
    a.mod.samples[0].volume = 48;
    a.mod.samples[0].length = 256;
    a.mod.samples[0].loopStart = 64;
    a.mod.samples[0].loopLength = 128;
    for (int i = 0; i < 256; i++)
        a.mod.sampleData(0)[i] = (int8_t)((i * 7) - 128);
    strcpy(a.mod.samples[0].name, "sample one");

    a.mod.samples[5].length = 100; // non looping
    for (int i = 0; i < 100; i++)
        a.mod.sampleData(5)[i] = (int8_t)(i + 1);
    a.mod.fixSample(5);

    a.mod.songLength = 3;
    a.mod.order[0] = 0;
    a.mod.order[1] = 2;
    a.mod.order[2] = 1;
    a.note(0, 0, 0, 1, 428, 0xC, 0x20);
    a.note(2, 63, 3, 6, 113, 0xE, 0x9A);
    a.note(1, 10, 1, 31, 856, 0xF, 0x7D);

    MemFile f1;
    CHECK(a.mod.save(ioFor(f1)) == Result::kOk);

    // header layout checks
    CHECK(f1.data.size() > 1084);
    CHECK(memcmp(&f1.data[1080], "M.K.", 4) == 0);
    CHECK(f1.data[950] == 3);
    CHECK(f1.data[951] == 0x7F);
    CHECK(f1.data[952 + 1] == 2);
    // 3 patterns used (highest order number is 2)
    CHECK(f1.data.size() == 1084 + 3 * 1024 + 256 + 100);

    Fixture b;
    f1.pos = 0;
    CHECK(b.mod.load(ioFor(f1)) == Result::kOk);
    CHECK(strcmp(b.mod.title, "TEST SONG") == 0);
    CHECK(strcmp(b.mod.samples[0].name, "sample one") == 0);
    CHECK(b.mod.samples[0].length == 256);
    CHECK(b.mod.samples[0].fineTune == 3);
    CHECK(b.mod.samples[0].volume == 48);
    CHECK(b.mod.samples[0].loopStart == 64);
    CHECK(b.mod.samples[0].loopLength == 128);
    CHECK(memcmp(b.mod.sampleData(0), a.mod.sampleData(0), 256) == 0);
    CHECK(b.mod.samples[5].length == 100);
    CHECK(b.mod.samples[5].loopLength == 2);
    // first two bytes of a non looping sample are cleared
    CHECK(b.mod.sampleData(5)[0] == 0 && b.mod.sampleData(5)[1] == 0);
    CHECK(b.mod.sampleData(5)[2] == 3);
    CHECK(b.mod.songLength == 3);
    CHECK(b.mod.order[1] == 2);
    CHECK(memcmp(b.mod.pattern(0), a.mod.pattern(0), 1024) == 0);
    CHECK(memcmp(b.mod.pattern(2), a.mod.pattern(2), 1024) == 0);
    CHECK(b.mod.pattern(2)[63 * 4 + 3].sample() == 6);
    CHECK(b.mod.pattern(2)[63 * 4 + 3].period() == 113);
    CHECK(b.mod.pattern(2)[63 * 4 + 3].command() == 0xE);
    CHECK(b.mod.pattern(2)[63 * 4 + 3].param() == 0x9A);
    CHECK(b.mod.pattern(1)[10 * 4 + 1].sample() == 31);

    // saving what we loaded gives the identical file
    MemFile f2;
    CHECK(b.mod.save(ioFor(f2)) == Result::kOk);
    CHECK(f2.data == f1.data);
}

static void testLoaderRejects()
{
    printf("loader rejects\n");
    Fixture a;
    a.makeSquare(0);
    MemFile f;
    a.mod.save(ioFor(f));

    // too short
    MemFile shortFile;
    shortFile.data.assign(f.data.begin(), f.data.begin() + 500);
    CHECK(a.mod.load(ioFor(shortFile)) == Result::kBadFormat);

    // 8 channel
    MemFile eight = f;
    memcpy(&eight.data[1080], "8CHN", 4);
    CHECK(a.mod.load(ioFor(eight)) == Result::kUnsupportedChannels);
    memcpy(&eight.data[1080], "16CH", 4);
    CHECK(a.mod.load(ioFor(eight)) == Result::kUnsupportedChannels);

    // not a module
    MemFile junk = f;
    memcpy(&junk.data[1080], "XXXX", 4);
    CHECK(a.mod.load(ioFor(junk)) == Result::kBadFormat);

    // accepted four channel tags
    for (const char* tag : { "M!K!", "FLT4", "4CHN", "N.T." })
    {
        MemFile ok = f;
        memcpy(&ok.data[1080], tag, 4);
        CHECK(a.mod.load(ioFor(ok)) == Result::kOk);
    }
}

static void testLoaderSanitising()
{
    printf("loader sanitising\n");
    Fixture a;
    a.makeSquare(0);
    a.mod.samples[0].length = 200;
    MemFile f;
    a.mod.save(ioFor(f));

    // sample 0: loop runs past the end of the sample -> the sample grows to contain the loop
    uint8_t* h = &f.data[20];
    h[25] = 200; // volume > 64 -> 64
    h[26] = 0;
    h[27] = 90; // loop start 180 bytes
    h[28] = 0;
    h[29] = 40; // loop length 80 bytes -> loop ends at 260 > 200
    // append 60 bytes of extra file data so the grown sample can be read
    f.data.resize(f.data.size() + 200, 0x11);

    Fixture b;
    CHECK(b.mod.load(ioFor(f)) == Result::kOk);
    CHECK(b.mod.samples[0].volume == 64);
    CHECK(b.mod.samples[0].loopStart == 180);
    CHECK(b.mod.samples[0].loopLength == 80);
    CHECK(b.mod.samples[0].length == 260);

    // truncated file: samples that could not be read end up empty/silent, no crash
    MemFile t = f;
    t.data.resize(1084 + 1024 + 50);
    CHECK(b.mod.load(ioFor(t)) == Result::kOk);
}

static void testOversizedSample()
{
    printf("oversized sample\n");
    // sample 1 declares 70000 bytes (more than we can hold), sample 2 follows it in the file
    Fixture a;
    a.makeSquare(1, 77);
    MemFile f;
    a.mod.save(ioFor(f));

    std::vector<uint8_t> file(1084 + 1024);
    memcpy(file.data(), f.data.data(), 1084 + 1024);
    uint8_t* s0 = &file[20];
    s0[22] = (35000 >> 8); s0[23] = (35000 & 0xFF); // 70000 bytes
    s0[25] = 64;
    s0[28] = 0; s0[29] = 1;
    uint8_t* s1 = &file[20 + 30];
    s1[22] = 0; s1[23] = 32; // 64 bytes
    s1[25] = 64;
    s1[28] = 0; s1[29] = 32; // looping
    for (int i = 0; i < 70000; i++)
        file.push_back((uint8_t)(i & 0x7F));
    for (int i = 0; i < 64; i++)
        file.push_back((uint8_t)(i < 32 ? 77 : (uint8_t)-77));

    MemFile in;
    in.data = file;
    Fixture b;
    CHECK(b.mod.load(ioFor(in)) == Result::kOk);
    CHECK(b.mod.samples[0].length == 65534);
    CHECK(b.mod.samples[1].length == 64);
    CHECK(b.mod.sampleData(1)[0] == 77);
    CHECK(b.mod.sampleData(1)[40] == -77);
    CHECK(b.mod.sampleData(0)[1000] == (1000 & 0x7F));
}

static void testPitch()
{
    printf("pitch\n");
    const float rate = 44100.0f;
    Fixture a(rate);
    a.makeSquare(0);
    a.note(0, 0, 0, 1, 428);
    a.rep.play(0, 0);

    // 3546895 / 428 = 8287.2 Hz per sample, 64 byte period -> 129.5 Hz
    auto s = renderLeft(a.rep, (uint32_t)rate);
    CHECK_NEAR(zeroCrossingFrequency(s, rate), 3546895.0 / 428.0 / 64.0, 1.5);
    CHECK(peak(s, 1000, s.size()) > 1000);

    // one octave up: period 214
    Fixture b(rate);
    b.makeSquare(0);
    b.note(0, 0, 0, 1, 214);
    b.rep.play(0, 0);
    auto s2 = renderLeft(b.rep, (uint32_t)rate);
    CHECK_NEAR(zeroCrossingFrequency(s2, rate), 3546895.0 / 214.0 / 64.0, 3.0);
}

static void testStereoRouting()
{
    printf("stereo routing\n");
    Fixture a;
    a.makeSquare(0);
    a.note(0, 0, 0, 1, 428); // channel 0 -> left
    a.note(0, 0, 1, 1, 428); // channel 1 -> right (silenced below to check separation)
    a.rep.setMuted(1, true);
    a.rep.play(0, 0);
    std::vector<int16_t> l(4000), r(4000);
    a.rep.render(l.data(), r.data(), 4000);
    CHECK(peak(l, 100, 4000) > 1000);
    CHECK(peak(r, 100, 4000) == 0);

    // mono: both sides identical
    Fixture b;
    b.makeSquare(0);
    b.note(0, 0, 0, 1, 428);
    b.rep.setStereoSeparation(0);
    b.rep.play(0, 0);
    b.rep.render(l.data(), r.data(), 4000);
    CHECK(memcmp(l.data(), r.data(), 4000 * 2) == 0);
    CHECK(peak(l, 100, 4000) > 1000);
}

static void testTiming()
{
    printf("timing\n");
    const float rate = 44100.0f;
    Fixture a(rate);
    a.makeSquare(0);
    a.note(0, 0, 0, 1, 428);
    a.rep.play(0, 0);

    // speed 6 @ 125 BPM = 6 * 20 ms = 120 ms per row = 5292 samples
    std::vector<int16_t> l(1), r(1);
    int rowChangeAt[3] = { -1, -1, -1 };
    int lastRow = -1;
    int found = 0;
    for (int f = 0; f < 5292 * 3 + 10 && found < 3; f++)
    {
        a.rep.render(l.data(), r.data(), 1);
        if (a.rep.row() != lastRow)
        {
            lastRow = a.rep.row();
            rowChangeAt[found++] = f;
        }
    }
    CHECK(rowChangeAt[0] == 0);
    CHECK_NEAR(rowChangeAt[1], 5292, 2);
    CHECK_NEAR(rowChangeAt[2], 5292 * 2, 3);

    // F-command: set speed 3
    Fixture b(rate);
    b.makeSquare(0);
    b.note(0, 0, 0, 1, 428, 0xF, 3);
    b.rep.play(0, 0);
    std::vector<int16_t> big(44100), big2(44100);
    b.rep.render(big.data(), big2.data(), 2646 + 100);
    CHECK(b.rep.speed() == 3);
    CHECK(b.rep.row() == 1);

    // F-command: tempo 250 halves the tick time (applied one tick late like the CIA)
    Fixture c(rate);
    c.makeSquare(0);
    c.note(0, 0, 0, 1, 428, 0xF, 250);
    c.rep.play(0, 0);
    c.rep.render(big.data(), big2.data(), 100);
    CHECK(c.rep.bpm() == 125);
    c.rep.render(big.data(), big2.data(), 1000);
    CHECK(c.rep.bpm() == 250);
}

static void testFlowControl()
{
    printf("flow control\n");
    std::vector<int16_t> l(1), r(1);

    // pattern break: D10 on row 2 of pattern 0 continues at row 10 of the next order
    {
        Fixture a;
        a.makeSquare(0);
        a.mod.songLength = 2;
        a.mod.order[0] = 0;
        a.mod.order[1] = 1;
        a.note(0, 2, 0, 0, 0, 0xD, 0x10);
        a.rep.play(0, 0);
        int lastPos = 0, lastRow = 0;
        bool seen = false;
        for (int f = 0; f < 44100 * 2 && !seen; f++)
        {
            a.rep.render(l.data(), r.data(), 1);
            if (a.rep.position() == 1)
            {
                lastPos = a.rep.position();
                lastRow = a.rep.row();
                seen = true;
            }
        }
        CHECK(seen);
        CHECK(lastPos == 1);
        CHECK(lastRow == 10);
    }

    // position jump: B01 on row 0 of pattern 0 jumps to order 1, skipping the rest of pattern 0
    {
        Fixture a;
        a.makeSquare(0);
        a.mod.songLength = 3;
        a.mod.order[0] = 0;
        a.mod.order[1] = 1;
        a.mod.order[2] = 2;
        a.note(0, 0, 0, 0, 0, 0xB, 0x02);
        a.rep.play(0, 0);
        bool seen = false;
        for (int f = 0; f < 44100 && !seen; f++)
        {
            a.rep.render(l.data(), r.data(), 1);
            if (a.rep.position() == 2)
                seen = true;
        }
        CHECK(seen);
        CHECK(a.rep.row() == 0);
    }

    // song end wraps to the start
    {
        Fixture a;
        a.makeSquare(0);
        a.mod.songLength = 1;
        a.note(0, 1, 0, 0, 0, 0xD, 0x00); // break to row 0 of next position (wraps)
        a.rep.play(0, 0);
        std::vector<int16_t> big(44100), big2(44100);
        a.rep.render(big.data(), big2.data(), 44100);
        CHECK(a.rep.songLoops() >= 3);
        CHECK(a.rep.position() == 0);
    }

    // E6x pattern loop: loop row 1 .. row 3 twice
    {
        Fixture a;
        a.makeSquare(0);
        a.mod.songLength = 1;
        a.note(0, 1, 0, 0, 0, 0xE, 0x60);
        a.note(0, 3, 0, 0, 0, 0xE, 0x62);
        a.rep.play(0, 0);
        // rows played: 0 1 2 3 | 1 2 3 | 1 2 3 | 4  -> row 4 is reached after 11 row steps
        int steps = 0;
        int lastRow = -1;
        int rowsBefore4 = 0;
        for (int f = 0; f < 44100 * 3; f++)
        {
            a.rep.render(l.data(), r.data(), 1);
            if (a.rep.row() != lastRow)
            {
                lastRow = a.rep.row();
                steps++;
                if (lastRow == 4)
                    break;
                rowsBefore4 = steps;
            }
        }
        CHECK(lastRow == 4);
        CHECK(rowsBefore4 == 10); // rows 0,1,2,3,1,2,3,1,2,3 played before row 4
    }

    // pattern mode loops one pattern and ignores the order list
    {
        Fixture a;
        a.makeSquare(0);
        a.mod.songLength = 2;
        a.mod.order[0] = 0;
        a.mod.order[1] = 1;
        a.rep.playPattern(1, 0);
        std::vector<int16_t> big(44100 * 10), big2(44100 * 10);
        a.rep.render(big.data(), big2.data(), 44100 * 10);
        CHECK(a.rep.pattern() == 1);
        CHECK(a.rep.position() == 0);
    }
}

static void testVolumeEffects()
{
    printf("volume effects\n");
    const float rate = 44100.0f;

    // C20 = half volume
    Fixture a(rate);
    a.makeSquare(0);
    a.note(0, 0, 0, 1, 428);
    a.rep.play(0, 0);
    auto full = renderLeft(a.rep, 4000);

    Fixture b(rate);
    b.makeSquare(0);
    b.note(0, 0, 0, 1, 428, 0xC, 0x20);
    b.rep.play(0, 0);
    auto half = renderLeft(b.rep, 4000);
    CHECK_NEAR(peak(half, 100, 4000), peak(full, 100, 4000) / 2.0, peak(full, 100, 4000) * 0.03);

    // volume slide down A0x makes it quieter over the row
    Fixture c(rate);
    c.makeSquare(0);
    c.note(0, 0, 0, 1, 428, 0xA, 0x08);
    c.rep.play(0, 0);
    auto slide = renderLeft(c.rep, 5292);
    CHECK(peak(slide, 4000, 5292) < peak(slide, 100, 1000));

    // note cut ECx: silence from tick 2 on (tick = 441 samples at 125 BPM / 44100 Hz... 882 per tick)
    Fixture d(rate);
    d.makeSquare(0);
    d.note(0, 0, 0, 1, 428, 0xE, 0xC2);
    d.rep.play(0, 0);
    auto cut = renderLeft(d.rep, 5292);
    CHECK(peak(cut, 100, 1500) > 1000);
    CHECK(peak(cut, 2000, 5200) == 0);
}

static void testPeriodEffects()
{
    printf("period effects\n");
    const float rate = 44100.0f;

    // arpeggio 0xy: periods cycle base, +x, +y semitones
    Fixture a(rate);
    a.makeSquare(0);
    a.note(0, 0, 0, 1, 428, 0x0, 0x47);
    a.rep.play(0, 0);
    // tick 0
    std::vector<int16_t> l(8192), r(8192);
    a.rep.render(l.data(), r.data(), 100);
    CHECK(a.rep.channelPeriod(0) == 428);
    a.rep.render(l.data(), r.data(), 882); // into tick 1
    CHECK(a.rep.channelPeriod(0) == 339); // C-2 +4 semitones = E-2
    a.rep.render(l.data(), r.data(), 882); // into tick 2
    CHECK(a.rep.channelPeriod(0) == 285); // +7 = G-2
    a.rep.render(l.data(), r.data(), 882); // tick 3 = base again
    CHECK(a.rep.channelPeriod(0) == 428);

    // portamento up 1xx: moves the period every tick except the first
    Fixture b(rate);
    b.makeSquare(0);
    b.note(0, 0, 0, 1, 428, 0x1, 0x04);
    b.rep.play(0, 0);
    b.rep.render(l.data(), r.data(), 5000); // ticks 0..5 (5 slides)
    CHECK(b.rep.channelPeriod(0) == 428 - 5 * 4);

    // portamento is clamped to 113 (120 - 16 = 104 -> 113)
    Fixture c(rate);
    c.makeSquare(0);
    c.note(0, 0, 0, 1, 124, 0x1, 0x10);
    c.rep.play(0, 0);
    c.rep.render(l.data(), r.data(), 3000);
    CHECK(c.rep.channelPeriod(0) == 113);

    // original ProTracker bug, kept on purpose: a slide that underflows below zero is not clamped
    // (the sign is removed before the comparison), 120 - 255 = -135 -> 0xF79 = 3961
    Fixture c2(rate);
    c2.makeSquare(0);
    c2.note(0, 0, 0, 1, 124, 0x1, 0xFF);
    c2.rep.play(0, 0);
    c2.rep.render(l.data(), r.data(), 1500);
    CHECK(c2.rep.channelPeriod(0) == 3961);

    // tone portamento 3xx slides to the target and stops there
    Fixture d(rate);
    d.makeSquare(0);
    d.note(0, 0, 0, 1, 428);
    d.note(0, 1, 0, 0, 380, 0x3, 0x10);
    d.rep.play(0, 0);
    std::vector<int16_t> big(44100), big2(44100);
    d.rep.render(big.data(), big2.data(), 44100);
    // the target is the table value at or below the note: 380 -> 360
    CHECK(d.rep.channelPeriod(0) == 360);

    // fine portamento E1x only acts on the first tick
    Fixture e(rate);
    e.makeSquare(0);
    e.note(0, 0, 0, 1, 428, 0xE, 0x13);
    e.rep.play(0, 0);
    e.rep.render(l.data(), r.data(), 5000);
    CHECK(e.rep.channelPeriod(0) == 425);

    // finetune: sample finetune +7 plays 428 as 407... (table lookup)
    Fixture f(rate);
    f.makeSquare(0);
    f.mod.samples[0].fineTune = 7;
    f.note(0, 0, 0, 1, 428);
    f.rep.play(0, 0);
    f.rep.render(l.data(), r.data(), 10);
    CHECK(f.rep.channelPeriod(0) == 407 + 0 || f.rep.channelPeriod(0) == 407);
}

static void testSampleOffsetAndRetrigger()
{
    printf("sample offset / retrigger\n");
    const float rate = 44100.0f;

    // sample offset 9xx starts playback inside the sample: use a non looping ramp
    Fixture a(rate);
    Sample& s = a.mod.samples[0];
    s.length = 1024;
    s.volume = 64;
    for (int i = 0; i < 1024; i++)
        a.mod.sampleData(0)[i] = (int8_t)(i < 512 ? 100 : -100);
    a.mod.fixSample(0);
    a.note(0, 0, 0, 1, 428, 0x9, 0x02); // offset 0x200 bytes -> second half only
    a.rep.play(0, 0);
    auto o = renderLeft(a.rep, 500);
    // after the offset only negative samples remain
    int positives = 0;
    for (int i = 200; i < 500; i++)
    {
        if (o[i] > 100)
            positives++;
    }
    CHECK(positives == 0);

    // retrigger E9x: the sample restarts every x ticks -> more zero crossings than without
    Fixture b(rate);
    b.makeSquare(0);
    b.note(0, 0, 0, 1, 428, 0xE, 0x91);
    b.rep.play(0, 0);
    // just make sure it doesn't crash and keeps sounding
    auto rt = renderLeft(b.rep, 5292);
    CHECK(peak(rt, 4000, 5292) > 1000);
}

static void testFunkRepeatAndEdgeCases()
{
    printf("funk repeat / edge cases\n");
    std::vector<int16_t> big(44100), big2(44100);

    // EFx modifies the loop data like the original replayer
    Fixture a;
    a.makeSquare(0);
    int8_t before[64];
    memcpy(before, a.mod.sampleData(0), 64);
    a.note(0, 0, 0, 1, 428, 0xE, 0xFF);
    a.rep.play(0, 0);
    a.rep.render(big.data(), big2.data(), 44100);
    CHECK(memcmp(before, a.mod.sampleData(0), 64) != 0);

    // an empty sample and a note with no sample at all must be harmless
    Fixture b;
    b.note(0, 0, 0, 1, 428); // sample 1 is empty
    b.note(0, 1, 1, 0, 428); // no sample selected on that channel
    b.rep.play(0, 0);
    b.rep.render(big.data(), big2.data(), 44100);
    CHECK(peak(std::vector<int16_t>(big.begin(), big.end()), 0, 44100) == 0);

    // F00 stops the song
    Fixture c;
    c.makeSquare(0);
    c.note(0, 0, 0, 1, 428);
    c.note(0, 2, 0, 0, 0, 0xF, 0x00);
    c.rep.play(0, 0);
    c.rep.render(big.data(), big2.data(), 44100);
    CHECK(!c.rep.isPlaying());
    CHECK(peak(std::vector<int16_t>(big.begin() + 40000, big.end()), 0, 4000) == 0);

    // garbage in the pattern data never crashes the replayer (fuzz with ASAN/UBSAN)
    Fixture d;
    d.makeSquare(0);
    d.makeSquare(1, 60);
    d.mod.songLength = 4;
    srand(1234);
    for (int p = 0; p < 4; p++)
    {
        d.mod.order[p] = (uint8_t)p;
        for (int i = 0; i < 256; i++)
        {
            Note& n = d.mod.pattern(p)[i];
            for (int k = 0; k < 4; k++)
                n.b[k] = (uint8_t)rand();
        }
    }
    d.rep.play(0, 0);
    for (int i = 0; i < 20; i++)
        d.rep.render(big.data(), big2.data(), 44100);
    CHECK(true);

    // garbage order list
    Fixture e;
    e.makeSquare(0);
    e.mod.songLength = 128;
    for (int i = 0; i < 128; i++)
        e.mod.order[i] = (uint8_t)(i * 3);
    e.rep.play(0, 0);
    for (int i = 0; i < 10; i++)
        e.rep.render(big.data(), big2.data(), 44100);
    CHECK(true);
}

static void testLedFilter()
{
    printf("LED filter\n");
    const float rate = 44100.0f;

    // a high pitched square wave loses energy with E00 (filter on) compared to E01 (off)
    Fixture a(rate);
    a.makeSquare(0);
    a.note(0, 0, 0, 1, 124, 0xE, 0x01);
    a.rep.play(0, 0);
    auto off = renderLeft(a.rep, 4000);

    Fixture b(rate);
    b.makeSquare(0);
    b.note(0, 0, 0, 1, 124, 0xE, 0x00);
    b.rep.play(0, 0);
    auto on = renderLeft(b.rep, 4000);
    CHECK(b.rep.ledFilter());
    CHECK(!a.rep.ledFilter());

    double energyOff = 0, energyOn = 0;
    for (int i = 1000; i < 4000; i++)
    {
        energyOff += (double)off[i] * off[i];
        energyOn += (double)on[i] * on[i];
    }
    CHECK(energyOn < energyOff);
}

static void testPreview()
{
    printf("Sample preview\n");
    const float rate = 44100.0f;
    std::vector<int16_t> l(512), r(512);

    // a looping sample sustains until it is stopped
    Fixture a(rate);
    a.makeSquare(0);
    a.rep.previewStart(0, 428);
    CHECK(a.rep.isPreviewing());
    for (int block = 0; block < 20; block++)
        a.rep.render(l.data(), r.data(), 512);
    CHECK(a.rep.isPreviewing());
    CHECK(l[100] != 0 || l[300] != 0);
    CHECK(l[10] == r[10]);
    a.rep.previewStop();
    a.rep.render(l.data(), r.data(), 512);
    CHECK(l[0] == 0 && l[511] == 0);

    // a one-shot sample ends by itself
    Fixture b(rate);
    b.mod.samples[2].length = 64;
    b.mod.samples[2].volume = 64;
    for (int i = 0; i < 64; i++)
        b.mod.sampleData(2)[i] = 50;
    b.mod.fixSample(2);
    b.rep.previewStart(2, 428);
    for (int block = 0; block < 20; block++)
        b.rep.render(l.data(), r.data(), 512);
    CHECK(!b.rep.isPreviewing());

    // an empty sample makes no sound, and playing the song cancels a preview
    Fixture c(rate);
    c.rep.previewStart(4, 428);
    CHECK(!c.rep.isPreviewing());
    c.makeSquare(0);
    c.rep.previewStart(0, 428);
    c.rep.play(0, 0);
    CHECK(!c.rep.isPreviewing());

    // a preview started while the song runs is heard on top of it
    Fixture d(rate);
    d.makeSquare(0);
    d.rep.play(0, 0);
    d.rep.render(l.data(), r.data(), 512);
    CHECK(l[100] == 0 && l[300] == 0); // the empty pattern is silent
    d.rep.previewStart(0, 428);
    CHECK(d.rep.isPreviewing());
    d.rep.render(l.data(), r.data(), 512);
    CHECK(d.rep.isPlaying() && d.rep.isPreviewing());
    CHECK(l[100] != 0 || l[300] != 0);
}


static void testWavSlots()
{
    printf("WAV to sample slot\n");
    std::vector<uint8_t> pool(kPoolBytes);
    Module m;
    m.attach(pool.data(), pool.size());
    m.clear();

    // 16-bit stereo: left +16384, right 0 averages to +8192 -> +32 in 8 bits
    MemFile f;
    for (int i = 0; i < 100; i++)
    {
        int16_t l = 16384, r = 0;
        f.data.push_back((uint8_t)(l & 0xFF));
        f.data.push_back((uint8_t)(l >> 8));
        f.data.push_back((uint8_t)(r & 0xFF));
        f.data.push_back((uint8_t)(r >> 8));
    }
    FileIo io;
    io.ctx = &f;
    io.read = memRead;

    WavInfo info;
    info.format = 1;
    info.channels = 2;
    info.bits = 16;
    info.rate = 44100;
    info.dataBytes = (uint32_t)f.data.size();
    CHECK(wavToSlot(m, 3, info, io) == WavResult::kOk);
    CHECK(m.samples[3].length == 100);
    CHECK(m.sampleData(3)[2] == 32 && m.sampleData(3)[99] == 32);
    CHECK(m.samples[3].volume == 64 && m.samples[3].loopLength == 2);
    CHECK(slotActive(m, 3) && !slotActive(m, 4));

    // 8-bit unsigned, odd length is cut to an even one
    MemFile g;
    for (int i = 0; i < 51; i++)
        g.data.push_back(192); // +64
    io.ctx = &g;
    info.channels = 1;
    info.bits = 8;
    info.dataBytes = 51;
    CHECK(wavToSlot(m, 4, info, io) == WavResult::kOk);
    CHECK(m.samples[4].length == 50);
    CHECK(m.sampleData(4)[2] == 64);

    // float
    MemFile h;
    float fl = -0.5f;
    for (int i = 0; i < 10; i++)
    {
        uint8_t b[4];
        memcpy(b, &fl, 4);
        h.data.insert(h.data.end(), b, b + 4);
    }
    io.ctx = &h;
    info.format = 3;
    info.bits = 32;
    info.dataBytes = 40;
    CHECK(wavToSlot(m, 5, info, io) == WavResult::kOk);
    CHECK(m.sampleData(5)[2] == -64);

    // longer than a slot: truncated
    MemFile big;
    big.data.assign(70000 * 2, 0);
    io.ctx = &big;
    info.format = 1;
    info.bits = 16;
    info.dataBytes = (uint32_t)big.data.size();
    CHECK(wavToSlot(m, 6, info, io) == WavResult::kTruncated);
    CHECK(m.samples[6].length == kMaxSampleBytes);

    // unsupported and short files
    info.bits = 12;
    CHECK(wavToSlot(m, 7, info, io) == WavResult::kUnsupported);
    MemFile shortFile;
    shortFile.data.assign(10, 0);
    io.ctx = &shortFile;
    info.bits = 16;
    info.dataBytes = 100;
    CHECK(wavToSlot(m, 7, info, io) == WavResult::kIoError);

    // copy, clear, usage
    copySlot(m, 3, 9);
    CHECK(m.samples[9].length == 100 && m.sampleData(9)[50] == 32);
    clearSlot(m, 9);
    CHECK(!slotActive(m, 9) && m.sampleData(9)[50] == 0);

    m.pattern(0)[0].set(4, 428, 0, 0); // sample number 4 = slot 3
    bool used[kSampleCount];
    usedSamples(m, used);
    CHECK(used[3] && !used[0] && !used[4]);
}

int main()
{
    testRoundTrip();
    testLoaderRejects();
    testLoaderSanitising();
    testOversizedSample();
    testPitch();
    testStereoRouting();
    testTiming();
    testFlowControl();
    testVolumeEffects();
    testPeriodEffects();
    testSampleOffsetAndRetrigger();
    testFunkRepeatAndEdgeCases();
    testLedFilter();
    testPreview();
    testWavSlots();

    printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
