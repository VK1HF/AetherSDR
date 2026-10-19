#pragma once

#include <QObject>
#include <QTimer>

namespace AetherSDR {

// "TX Mute": silences the computer's default audio output while this radio
// transmits, so a second AetherSDR instance (or anything else) is not heard
// over the transmission. Only undoes what it did: if the output was already
// muted at key-down it is left muted at key-up, and a device without a mute
// control is turned down to zero and back instead. macOS only (CoreAudio);
// elsewhere isSupported() is false and the controller does nothing.
//
// The output comes back a short tail after TX ends, not at once, so the
// radio's own unkey (or a WebSDR such as a KiwiSDR running a second or two
// behind) is not heard. A new key-down within the tail keeps it muted.
class SystemOutputMute : public QObject {
    Q_OBJECT

public:
    explicit SystemOutputMute(QObject* parent = nullptr);
    ~SystemOutputMute() override;   // restores the output if still muted

    static bool isSupported();

    // Delay between the end of TX and unmuting.
    static constexpr int kMinTailMs = 100;
    static constexpr int kMaxTailMs = 2500;
    static constexpr int kDefaultTailMs = 200;
    int tailMs() const { return m_tailMs; }

    bool isEnabled() const { return m_enabled; }
    // True while this controller is holding the output muted.
    bool isEngaged() const { return m_engaged; }

public slots:
    void setEnabled(bool on);
    // Feed the radio's actual TX state (MOX, VOX, CW break-in, TUNE...).
    void setTransmitting(bool tx);
    void setTailMs(int ms);   // clamped to the range above

signals:
    void engagedChanged(bool engaged);

private:
    void engage();
    void release();

    bool  m_enabled{false};
    bool  m_transmitting{false};
    bool  m_engaged{false};
    int   m_tailMs{kDefaultTailMs};
    QTimer m_tailTimer;
    // How engage() silenced the device, so release() can undo exactly that.
    enum class Method { None, Mute, Volume };
    Method   m_method{Method::None};
    unsigned m_deviceId{0};
    float    m_savedVolume{0.0f};
};

} // namespace AetherSDR
