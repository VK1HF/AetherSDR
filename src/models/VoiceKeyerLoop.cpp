#include "models/VoiceKeyerLoop.h"

#include "models/VoiceKeyer.h"

#include <QtGlobal>

namespace AetherSDR {

namespace {

// A play that ends this much short of the recording was cut off, not
// finished. Keyers report the end a little early or late, so it is generous,
// but never more than half the recording.
int earlyEndMarginMs(int durationMs)
{
    return qMin(750, durationMs / 2);
}

}  // namespace

VoiceKeyerLoop::VoiceKeyerLoop(VoiceKeyer* keyer, QObject* parent)
    : VoiceKeyerLoop(keyer, Timing{}, parent)
{
}

VoiceKeyerLoop::VoiceKeyerLoop(VoiceKeyer* keyer, Timing timing, QObject* parent)
    : QObject(parent), m_keyer(keyer), m_timing(timing)
{
    m_gapTimer.setSingleShot(true);
    m_graceTimer.setSingleShot(true);
    m_startTimer.setSingleShot(true);
    connect(&m_gapTimer, &QTimer::timeout, this, &VoiceKeyerLoop::playNow);
    connect(&m_graceTimer, &QTimer::timeout, this, [this] {
        stop(QStringLiteral("Loop stopped: the transmitter stayed keyed after the recording."));
    });
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        stop(QStringLiteral("Loop stopped: the voice keyer did not start playing."));
    });
    connectKeyer();
}

void VoiceKeyerLoop::connectKeyer()
{
    if (!m_keyer)
        return;
    connect(m_keyer, &VoiceKeyer::statusChanged, this,
            [this](VoiceKeyer::Status status, int id) { onKeyerStatus(status, id); });
    connect(m_keyer, &VoiceKeyer::commandFailed, this, &VoiceKeyerLoop::onCommandFailed);
}

void VoiceKeyerLoop::setKeyer(VoiceKeyer* keyer)
{
    if (keyer == m_keyer)
        return;
    if (isActive())
        stop(QStringLiteral("Loop stopped: the voice keyer changed."));
    if (m_keyer)
        disconnect(m_keyer, nullptr, this, nullptr);
    m_keyer = keyer;
    connectKeyer();
}

void VoiceKeyerLoop::setGapSeconds(int seconds)
{
    seconds = qBound(kMinGapSeconds, seconds, kMaxGapSeconds);
    if (seconds == m_gapSeconds)
        return;
    m_gapSeconds = seconds;
    // A pause already running keeps its length; the next one uses the new.
    emit gapSecondsChanged(seconds);
}

int VoiceKeyerLoop::msUntilNextPlay() const
{
    return m_phase == Phase::Waiting ? qMax(0, m_gapTimer.remainingTime()) : 0;
}

int VoiceKeyerLoop::slotDurationMs() const
{
    if (!m_keyer)
        return 0;
    for (const auto& r : m_keyer->recordings())
        if (r.id == m_slot)
            return r.durationMs;
    return 0;
}

bool VoiceKeyerLoop::start(int slot)
{
    if (!m_keyer)
        return false;
    if (isActive())
        stop();
    m_slot = slot;
    if (slotDurationMs() <= 0) {
        m_slot = -1;
        return false;
    }
    // Already on air from a plain play: carry on from it.
    if (m_keyer->status() == VoiceKeyer::Playback && m_keyer->activeId() == slot) {
        m_playClock.invalidate();  // its start time is unknown, so no early-end test
        setPhase(Phase::Playing);
        emit playStarted(m_slot);
        return true;
    }
    if (!m_keyer->canStartOperation()) {
        m_slot = -1;
        return false;
    }
    playNow();
    return isActive();
}

void VoiceKeyerLoop::playNow()
{
    if (!m_keyer || !m_keyer->canStartOperation() || slotDurationMs() <= 0) {
        stop(QStringLiteral("Loop stopped: the voice keyer is busy or the slot is empty."));
        return;
    }
    // Starting before the call: the keyer may report Playback synchronously,
    // and the key-down it causes is the loop's own.
    setPhase(Phase::Starting);
    m_startTimer.start(m_timing.startTimeoutMs);
    m_keyer->playbackStart(m_slot);
    if (isActive())
        emit playStarted(m_slot);
}

void VoiceKeyerLoop::stop(const QString& reason)
{
    if (!isActive())
        return;
    const Phase was = m_phase;
    m_gapTimer.stop();
    m_graceTimer.stop();
    m_startTimer.stop();
    // Phase goes Off first, so the status this stop causes is not taken as a
    // play ending and restarting the loop.
    setPhase(Phase::Off);
    // A start not yet echoed is stopped too: a stop must never be weaker
    // than the start it follows.
    if ((was == Phase::Starting || was == Phase::Playing) && m_keyer)
        m_keyer->playbackStop(m_slot);
    emit playStopped();
    m_slot = -1;
    if (!reason.isEmpty())
        emit interrupted(reason);
}

void VoiceKeyerLoop::onKeyerStatus(int status, int id)
{
    const auto s = static_cast<VoiceKeyer::Status>(status);
    switch (m_phase) {
    case Phase::Starting:
        if (s == VoiceKeyer::Playback && id == m_slot) {
            m_startTimer.stop();
            m_playClock.start();
            setPhase(Phase::Playing);
        } else if (s == VoiceKeyer::Recording || s == VoiceKeyer::Preview
                   || s == VoiceKeyer::Disabled) {
            stop(QStringLiteral("Loop stopped: the voice keyer started something else."));
        }
        break;
    case Phase::Playing:
        if (s == VoiceKeyer::Playback && id == m_slot)
            break;
        if (s == VoiceKeyer::Idle)
            onPlayEnded();
        else
            stop(QStringLiteral("Loop stopped: the voice keyer started something else."));
        break;
    case Phase::AwaitUnkey:
    case Phase::Waiting:
        // The keyer is ours to drive; anything starting now is the operator.
        if (s == VoiceKeyer::Recording || s == VoiceKeyer::Preview
            || s == VoiceKeyer::Playback || s == VoiceKeyer::Disabled) {
            stop(QStringLiteral("Loop stopped."));
        }
        break;
    case Phase::Off:
        break;
    }
}

void VoiceKeyerLoop::onPlayEnded()
{
    const int duration = slotDurationMs();
    if (m_playClock.isValid() && duration > 0
        && m_playClock.elapsed() < duration - earlyEndMarginMs(duration)) {
        // Cut off: an unkey, TUNE, or an abort ended the play, not its end.
        setPhase(Phase::Off);
        m_slot = -1;
        emit interrupted(QStringLiteral("Loop stopped: the recording was cut off."));
        return;
    }
    if (m_transmitting) {
        // The radio's TX tail, or a PTT held over the end. The tail drops
        // quickly; a key still down after the grace is the operator.
        setPhase(Phase::AwaitUnkey);
        m_graceTimer.start(m_timing.unkeyGraceMs);
        return;
    }
    beginWaiting();
}

void VoiceKeyerLoop::beginWaiting()
{
    m_graceTimer.stop();
    setPhase(Phase::Waiting);
    m_gapTimer.start(m_gapSeconds * m_timing.secondMs);
}

void VoiceKeyerLoop::onTransmittingChanged(bool tx)
{
    m_transmitting = tx;
    if (m_phase == Phase::AwaitUnkey && !tx) {
        beginWaiting();
    } else if (m_phase == Phase::Waiting && tx) {
        stop(QStringLiteral("Loop stopped: the transmitter was keyed."));
    }
}

void VoiceKeyerLoop::onRemoteTransmitting(bool tx)
{
    if (m_phase == Phase::Waiting && tx)
        stop(QStringLiteral("Loop stopped: another linked radio was keyed."));
}

void VoiceKeyerLoop::onRemoteFault(const QString& reason)
{
    if (isActive())
        stop(reason);
}

void VoiceKeyerLoop::onTuneChanged(bool tuning)
{
    if (tuning && isActive())
        stop(QStringLiteral("Loop stopped: TUNE."));
}

void VoiceKeyerLoop::onCommandFailed()
{
    if (isActive())
        stop(QStringLiteral("Loop stopped: the voice keyer refused a command."));
}

void VoiceKeyerLoop::setPhase(Phase phase)
{
    if (phase == m_phase)
        return;
    m_phase = phase;
    emit phaseChanged(phase, m_slot);
}

}  // namespace AetherSDR
