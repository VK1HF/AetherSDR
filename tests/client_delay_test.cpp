// Standalone test harness for ClientDelay DSP (single slapback echo).
// Build: CMake target `client_delay_test`.  Exit 0 = pass.

#include "core/ClientDelay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using AetherSDR::ClientDelay;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 128;

int g_failed = 0;

void report(const char* name, bool ok, const std::string& detail = {})
{
    std::printf("%s %-54s %s\n",
                ok ? "[ OK ]" : "[FAIL]",
                name,
                detail.c_str());
    if (!ok) ++g_failed;
}

std::vector<float> makeImpulse(int frames, float amplitude = 0.8f)
{
    std::vector<float> buf(frames * 2, 0.0f);
    buf[0] = amplitude;
    buf[1] = amplitude;
    return buf;
}

void processBlocks(ClientDelay& d, std::vector<float>& buf)
{
    const int frames = static_cast<int>(buf.size() / 2);
    for (int off = 0; off < frames; off += kBlockSize) {
        const int n = std::min(kBlockSize, frames - off);
        d.process(buf.data() + off * 2, n, 2);
    }
}

// Index of the largest |sample| on the left channel at or after `from`.
int peakFrame(const std::vector<float>& buf, int from)
{
    int best = from;
    for (int i = from; i < static_cast<int>(buf.size() / 2); ++i) {
        if (std::fabs(buf[i * 2]) > std::fabs(buf[best * 2])) best = i;
    }
    return best;
}

void testDisabledIsBitExact()
{
    ClientDelay d;
    d.prepare(kSampleRate);
    auto buf = makeImpulse(4800);
    const auto ref = buf;
    processBlocks(d, buf);
    report("disabled stage leaves audio untouched", buf == ref);
}

void testDefaults()
{
    ClientDelay d;
    report("defaults: 25 ms, 25 % level, off",
           d.timeMs() == 25.0f && d.mix() == 0.25f && !d.isEnabled());
}

void testEchoArrivesAtTime()
{
    ClientDelay d;
    d.prepare(kSampleRate);
    d.setEnabled(true);
    d.setTimeMs(25.0f);
    d.setMix(0.25f);
    auto buf = makeImpulse(4800);
    processBlocks(d, buf);

    const int expected = static_cast<int>(std::lround(0.025 * kSampleRate));
    const int echoAt = peakFrame(buf, 10);
    const float dry  = buf[0];
    const float echo = buf[echoAt * 2];
    char detail[96];
    std::snprintf(detail, sizeof detail, "echo at %d (want %d), dry %.3f echo %.3f",
                  echoAt, expected, dry, echo);
    report("single echo lands at the set time and level",
           echoAt == expected
               && std::fabs(dry - 0.8f * 0.875f) < 1e-4f
               && std::fabs(echo - 0.8f * 0.25f) < 1e-4f,
           detail);

    // No second repeat: everything after the echo is silent.
    float tail = 0.0f;
    for (int i = echoAt + 2; i < 4800; ++i) tail = std::max(tail, std::fabs(buf[i * 2]));
    report("no feedback: exactly one repeat", tail < 1e-6f);
}

void testTimeClampsToMax()
{
    ClientDelay d;
    d.setTimeMs(80.0f);
    report("time clamps to 50 ms", d.timeMs() == ClientDelay::kMaxTimeMs);
    d.setMix(1.5f);
    report("level clamps to 100 %", d.mix() == 1.0f);
}

void testMaxTimeWorks()
{
    ClientDelay d;
    d.prepare(kSampleRate);
    d.setEnabled(true);
    d.setTimeMs(50.0f);
    auto buf = makeImpulse(4800);
    processBlocks(d, buf);
    const int expected = static_cast<int>(std::lround(0.050 * kSampleRate));
    report("50 ms echo fits the buffer", peakFrame(buf, 10) == expected);
}

void testTimeChangeGlides()
{
    // A steady tone with the time knob swept mid-stream must not jump: the
    // largest sample-to-sample step stays near what the tone itself makes.
    ClientDelay d;
    d.prepare(kSampleRate);
    d.setEnabled(true);
    d.setMix(0.5f);
    d.setTimeMs(5.0f);
    const int frames = 24000;
    std::vector<float> buf(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = 0.5f * static_cast<float>(
            std::sin(6.283185307179586 * 300.0 * i / kSampleRate));
        buf[i * 2] = buf[i * 2 + 1] = s;
    }
    for (int off = 0; off < frames; off += kBlockSize) {
        if (off == frames / 2) d.setTimeMs(45.0f);
        d.process(buf.data() + off * 2, std::min(kBlockSize, frames - off), 2);
    }
    float maxStep = 0.0f;
    for (int i = 1; i < frames; ++i) {
        maxStep = std::max(maxStep, std::fabs(buf[i * 2] - buf[(i - 1) * 2]));
    }
    // 300 Hz at peak 0.5*(0.75+0.5) moves ~0.025 per sample at 48 kHz.
    char detail[48];
    std::snprintf(detail, sizeof detail, "max step %.4f", maxStep);
    report("time change glides without a click", maxStep < 0.05f, detail);
}

void testMono()
{
    ClientDelay d;
    d.prepare(kSampleRate);
    d.setEnabled(true);
    d.setTimeMs(10.0f);
    std::vector<float> buf(960, 0.0f);
    buf[0] = 0.8f;
    d.process(buf.data(), static_cast<int>(buf.size()), 1);
    report("mono input gets its echo", std::fabs(buf[480] - 0.2f) < 1e-4f);
}

} // namespace

int main()
{
    testDefaults();
    testDisabledIsBitExact();
    testEchoArrivesAtTime();
    testTimeClampsToMax();
    testMaxTimeWorks();
    testTimeChangeGlides();
    testMono();
    std::printf("%s\n", g_failed ? "FAILED" : "PASSED");
    return g_failed ? 1 : 0;
}
