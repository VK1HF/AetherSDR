#include "ClientDelay.h"

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// Time-knob glide: ~30 ms time constant, fast enough to feel immediate and
// slow enough that the read pointer never jumps.
constexpr double kTimeGlideSeconds = 0.030;

float linToDb(float lin) noexcept
{
    return (lin > 1e-9f) ? 20.0f * std::log10(lin) : -180.0f;
}

} // namespace

ClientDelay::ClientDelay() = default;

void ClientDelay::prepare(double sampleRate)
{
    m_sampleRate = sampleRate;
    // +2 frames: one for the current write, one for the interpolation partner.
    m_ringCap = static_cast<int>(
        std::ceil(kMaxTimeMs * 0.001 * sampleRate)) + 2;
    m_ring.assign(static_cast<size_t>(m_ringCap) * 2, 0.0f);
    m_ringWrite = 0;
    m_smoothCoef = static_cast<float>(
        1.0 - std::exp(-1.0 / (kTimeGlideSeconds * sampleRate)));
    m_primed = false;

    m_atomics.version.fetch_add(1, std::memory_order_release);
    recacheIfDirty();
}

void ClientDelay::setEnabled(bool on) noexcept
{
    m_atomics.enabled.store(on, std::memory_order_release);
}
bool ClientDelay::isEnabled() const noexcept
{
    return m_atomics.enabled.load(std::memory_order_acquire);
}

void ClientDelay::setTimeMs(float ms) noexcept
{
    m_atomics.timeMs.store(std::clamp(ms, 0.0f, kMaxTimeMs),
                           std::memory_order_relaxed);
    m_atomics.version.fetch_add(1, std::memory_order_release);
}
float ClientDelay::timeMs() const noexcept
{ return m_atomics.timeMs.load(std::memory_order_relaxed); }

void ClientDelay::setMix(float m) noexcept
{
    m_atomics.mix.store(std::clamp(m, 0.0f, 1.0f), std::memory_order_relaxed);
    m_atomics.version.fetch_add(1, std::memory_order_release);
}
float ClientDelay::mix() const noexcept
{ return m_atomics.mix.load(std::memory_order_relaxed); }

void ClientDelay::reset() noexcept
{
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    m_ringWrite = 0;
    m_primed = false;
}

float ClientDelay::inputPeakDb() const noexcept
{ return m_inputPeakDb.load(std::memory_order_relaxed); }
float ClientDelay::outputPeakDb() const noexcept
{ return m_outputPeakDb.load(std::memory_order_relaxed); }

void ClientDelay::recacheIfDirty() noexcept
{
    const uint64_t v = m_atomics.version.load(std::memory_order_acquire);
    if (v == m_lastVersion) return;
    m_lastVersion = v;

    const float ms  = m_atomics.timeMs.load(std::memory_order_relaxed);
    const float mix = m_atomics.mix.load(std::memory_order_relaxed);

    m_targetDelaySamples = std::clamp(
        static_cast<float>(ms * 0.001 * m_sampleRate),
        0.0f, static_cast<float>(m_ringCap - 2));
    // Same gentle blend as ClientReverb: the echo adds level, so trim the
    // dry path a little to keep peaks close to where they were.
    m_dryGain = 1.0f - 0.5f * mix;
    m_wetGain = mix;
}

void ClientDelay::process(float* interleaved, int frames, int channels) noexcept
{
    if (frames <= 0) return;
    if (channels != 1 && channels != 2) return;
    if (m_ringCap <= 2) return;  // not prepared

    recacheIfDirty();
    const bool enabled = m_atomics.enabled.load(std::memory_order_acquire);

    float inPeakLin  = 0.0f;
    float outPeakLin = 0.0f;

    if (!m_primed) {
        m_delaySamples = m_targetDelaySamples;
        m_primed = true;
    }

    const float dryGain = m_dryGain;
    const float wetGain = m_wetGain;
    const float target  = m_targetDelaySamples;
    const float coef    = m_smoothCoef;

    for (int f = 0; f < frames; ++f) {
        const float inL = interleaved[f * channels];
        const float inR = (channels == 2) ? interleaved[f * channels + 1] : inL;

        const float inAbs = std::max(std::fabs(inL), std::fabs(inR));
        if (inAbs > inPeakLin) inPeakLin = inAbs;

        if (!enabled) {
            if (inAbs > outPeakLin) outPeakLin = inAbs;
            continue;
        }

        m_ring[m_ringWrite * 2]     = inL;
        m_ring[m_ringWrite * 2 + 1] = inR;

        m_delaySamples += (target - m_delaySamples) * coef;

        // Fractional read d samples behind the write head, interpolating
        // between the two neighbouring frames.
        const float d    = m_delaySamples;
        const int   di   = static_cast<int>(d);
        const float frac = d - static_cast<float>(di);
        int a = m_ringWrite - di;
        if (a < 0) a += m_ringCap;
        int b = a - 1;
        if (b < 0) b += m_ringCap;
        const float echoL = m_ring[a * 2]     + (m_ring[b * 2]     - m_ring[a * 2])     * frac;
        const float echoR = m_ring[a * 2 + 1] + (m_ring[b * 2 + 1] - m_ring[a * 2 + 1]) * frac;

        m_ringWrite = (m_ringWrite + 1) % m_ringCap;

        const float outL = inL * dryGain + echoL * wetGain;
        const float outR = inR * dryGain + echoR * wetGain;

        interleaved[f * channels] = outL;
        if (channels == 2) interleaved[f * channels + 1] = outR;

        const float oAbs = std::max(std::fabs(outL), std::fabs(outR));
        if (oAbs > outPeakLin) outPeakLin = oAbs;
    }

    m_inputPeakDb.store(linToDb(std::max(inPeakLin, 1e-6f)),
                        std::memory_order_relaxed);
    m_outputPeakDb.store(linToDb(std::max(outPeakLin, 1e-6f)),
                         std::memory_order_relaxed);
}

} // namespace AetherSDR
