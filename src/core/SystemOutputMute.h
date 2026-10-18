#pragma once

#include <QObject>

namespace AetherSDR {

// "TX Mute": silences the computer's default audio output while this radio
// transmits, so a second AetherSDR instance (or anything else) is not heard
// over the transmission. Only undoes what it did: if the output was already
// muted at key-down it is left muted at key-up, and a device without a mute
// control is turned down to zero and back instead. macOS only (CoreAudio);
// elsewhere isSupported() is false and the controller does nothing.
class SystemOutputMute : public QObject {
    Q_OBJECT

public:
    explicit SystemOutputMute(QObject* parent = nullptr);
    ~SystemOutputMute() override;   // restores the output if still muted

    static bool isSupported();

    bool isEnabled() const { return m_enabled; }
    // True while this controller is holding the output muted.
    bool isEngaged() const { return m_engaged; }

public slots:
    void setEnabled(bool on);
    // Feed the radio's actual TX state (MOX, VOX, CW break-in, TUNE...).
    void setTransmitting(bool tx);

signals:
    void engagedChanged(bool engaged);

private:
    void engage();
    void release();

    bool  m_enabled{false};
    bool  m_transmitting{false};
    bool  m_engaged{false};
    // How engage() silenced the device, so release() can undo exactly that.
    enum class Method { None, Mute, Volume };
    Method   m_method{Method::None};
    unsigned m_deviceId{0};
    float    m_savedVolume{0.0f};
};

} // namespace AetherSDR
