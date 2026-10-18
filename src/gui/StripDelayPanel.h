#pragma once

#include <QWidget>

namespace AetherSDR {

class AudioEngine;
class ClientCompKnob;

// Editor for the client-side TX delay (single slapback/doubling echo).
// Two 76×76 knobs — Time, Level — under a picture of the dry voice and
// its echo. Geometry persists via AppSettings (`StripDelayPanelGeometry`).
class StripDelayPanel : public QWidget {
    Q_OBJECT

public:
    explicit StripDelayPanel(AudioEngine* engine, QWidget* parent = nullptr);
    ~StripDelayPanel() override;

    void showForTx();

    // Pull every knob state from the bound engine.
    // Called after preset load when the engine is mutated externally.
    void syncControlsFromEngine();

signals:
    // Present for API symmetry with the other editors; bypass lives on
    // the CHAIN widget.
    void bypassToggled(bool bypassed);

protected:
    void closeEvent(QCloseEvent* ev) override;
    void moveEvent(QMoveEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    void saveGeometryToSettings();
    void restoreGeometryFromSettings();

    AudioEngine*    m_audio{nullptr};
    ClientCompKnob* m_time{nullptr};
    ClientCompKnob* m_level{nullptr};
    QWidget*        m_viz{nullptr};   // EchoBox (defined in .cpp)
    bool            m_restoring{false};
};

} // namespace AetherSDR
