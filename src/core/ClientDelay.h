#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace AetherSDR {

// Client-side TX delay: a single, non-repeating echo (slapback / doubling)
// mixed under the dry voice. At 10-50 ms the echo is not heard as a repeat;
// it thickens the voice and adds presence. No feedback path, so it never
// rings. The dry signal is not delayed, so the stage adds no TX latency.
// UI thread writes atomics + bumps a version; the audio thread recaches per
// block; no locks or allocations in process(). Time changes glide (one-pole
// smoothed, linearly interpolated read) so turning the knob does not click.
class ClientDelay {
public:
    static constexpr float kMaxTimeMs     = 50.0f;
    static constexpr float kDefaultTimeMs = 25.0f;
    static constexpr float kDefaultMix    = 0.25f;  // echo ~11 dB under the voice

    ClientDelay();
    ~ClientDelay() = default;

    ClientDelay(const ClientDelay&)            = delete;
    ClientDelay& operator=(const ClientDelay&) = delete;

    // Main thread — call before first process() and on sample-rate change.
    void prepare(double sampleRate);

    void  setEnabled(bool on) noexcept;
    bool  isEnabled() const noexcept;

    void  setTimeMs(float ms) noexcept;   float timeMs() const noexcept;   // 0..50
    void  setMix(float m) noexcept;       float mix() const noexcept;      // 0..1

    // Audio thread — process in place.  channels must be 1 or 2.
    void process(float* interleaved, int frames, int channels) noexcept;
    void reset() noexcept;

    // UI-thread meter snapshots.
    float inputPeakDb() const noexcept;
    float outputPeakDb() const noexcept;

    double sampleRate() const noexcept { return m_sampleRate; }

private:
    struct Atomics {
        std::atomic<bool>     enabled{false};
        std::atomic<float>    timeMs{kDefaultTimeMs};
        std::atomic<float>    mix{kDefaultMix};
        std::atomic<uint64_t> version{0};
    };

    void recacheIfDirty() noexcept;

    double  m_sampleRate{48000.0};
    Atomics m_atomics;

    // Recached per block.
    float m_targetDelaySamples{0.0f};
    float m_dryGain{1.0f};
    float m_wetGain{0.0f};

    // Audio-thread state.
    float m_delaySamples{0.0f};   // smoothed toward m_targetDelaySamples
    float m_smoothCoef{0.0f};     // per-sample one-pole coefficient
    bool  m_primed{false};        // first block snaps instead of gliding

    // Stereo-interleaved ring buffer sized for kMaxTimeMs (+ interpolation).
    std::vector<float> m_ring;    // frames × 2
    int m_ringCap{0};             // capacity in frames
    int m_ringWrite{0};

    uint64_t m_lastVersion{0};

    std::atomic<float> m_inputPeakDb{-120.0f};
    std::atomic<float> m_outputPeakDb{-120.0f};
};

} // namespace AetherSDR
