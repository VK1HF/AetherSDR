#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

namespace AetherSDR {

class VoiceKeyer;

// Repeats one voice-keyer slot on air with a pause between plays: a CQ that
// calls itself until someone answers. It drives whichever keyer is bound (the
// radio DVK or the local one) through the VoiceKeyer interface only.
//
// The loop must give way to the operator at once, so it ends on:
//  * TUNE starting, at any point (playback in progress is stopped);
//  * any transmit during the pause (MOX, PTT, VOX, CAT...), since the keyer
//    is idle then and a key-down can only be someone else's;
//  * a play that ends well short of the recording's length, which is how an
//    unkey or abort mid-play shows up whatever the keyer;
//  * the transmitter still keyed shortly after a play ends (a PTT held over
//    the end of the recording);
//  * a refused command, a keyer swap, or stop().
// A key-down while the recording itself is on air is the one case nothing can
// see: the radio is already transmitting. Its unkey then ends the play early.
class VoiceKeyerLoop : public QObject {
    Q_OBJECT
public:
    // Pause between plays, from the end of one transmission to the next.
    static constexpr int kMinGapSeconds = 3;
    static constexpr int kMaxGapSeconds = 20;
    static constexpr int kDefaultGapSeconds = 6;

    // Real-time constants, overridable so a test can run in milliseconds.
    struct Timing {
        int secondMs{1000};       // one "second" of the gap setting
        int unkeyGraceMs{2000};   // how long a play's TX tail may last
        int startTimeoutMs{5000}; // how long a start may go unanswered
    };

    enum class Phase {
        Off,
        Starting,    // a play was asked for; the keyer has not reported it yet
        Playing,     // the slot is on air
        AwaitUnkey,  // the play ended; waiting for the radio to drop TX
        Waiting,     // the pause between plays
    };
    Q_ENUM(Phase)

    explicit VoiceKeyerLoop(VoiceKeyer* keyer, QObject* parent = nullptr);
    VoiceKeyerLoop(VoiceKeyer* keyer, Timing timing, QObject* parent = nullptr);

    // Bind a different keyer. A running loop ends first.
    void setKeyer(VoiceKeyer* keyer);

    int gapSeconds() const { return m_gapSeconds; }
    void setGapSeconds(int seconds);  // clamped to the range above

    bool isActive() const { return m_phase != Phase::Off; }
    Phase phase() const { return m_phase; }
    int slot() const { return m_slot; }
    // Time left in the pause, 0 outside it.
    int msUntilNextPlay() const;

    // Loop a slot. If the keyer is already playing that slot, the loop takes
    // that play over; otherwise it starts one now. Returns false when nothing
    // could start (an empty slot, or the keyer is busy with something else).
    bool start(int slot);
    // End the loop, stopping a play it owns mid-air. reason is shown to the
    // operator; empty for an operator's own stop.
    void stop(const QString& reason = {});

public slots:
    // TransmitModel::transmittingChanged and tuneChanged.
    void onTransmittingChanged(bool tx);
    void onTuneChanged(bool tuning);
    // Linked windows: another window keyed (any means). Ends a loop that is
    // waiting for its next play; a key during a play is that window's own
    // transmission of the same recording.
    void onRemoteTransmitting(bool tx);
    // Linked windows: another window could not play the slot. Ends the loop.
    void onRemoteFault(const QString& reason);

signals:
    // Each play this loop starts, and the stop that ends it, so the linked
    // windows can play the same slot in step.
    void playStarted(int slot);
    void playStopped();
    void phaseChanged(AetherSDR::VoiceKeyerLoop::Phase phase, int slot);
    void gapSecondsChanged(int seconds);
    // The loop ended on its own (not by stop() with an empty reason).
    void interrupted(const QString& reason);

private:
    void onKeyerStatus(int status, int id);
    void onCommandFailed();
    void playNow();
    void onPlayEnded();
    void beginWaiting();
    void setPhase(Phase phase);
    int slotDurationMs() const;
    void connectKeyer();

    QPointer<VoiceKeyer> m_keyer;
    Timing m_timing;
    Phase m_phase{Phase::Off};
    int m_slot{-1};
    int m_gapSeconds{kDefaultGapSeconds};
    bool m_transmitting{false};
    QElapsedTimer m_playClock;
    QTimer m_gapTimer;
    QTimer m_graceTimer;
    QTimer m_startTimer;
};

}  // namespace AetherSDR
