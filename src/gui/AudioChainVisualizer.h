#pragma once

#include "CanonWindow.h"
#include "MeterSmoother.h"

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <functional>
#include <limits>

class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QTimer;
class QToolButton;

namespace AetherSDR {

class ChainCard;
class ChainCanvas;

// Everything the visualizer draws, read from the models in one pass. The
// window owns no model pointers: MainWindow builds this on the window's
// refresh tick, so the view stays free of engine includes.
struct AudioChainSnapshot {
    static constexpr float kNoReading = std::numeric_limits<float>::quiet_NaN();

    bool        connected{false};
    QString     micSource;          // the radio's mic_selection, e.g. "PC", "MIC"
    QStringList micSources;         // what the radio offers
    QString     pcInputDevice;      // the computer's capture device, for PC
    bool        daxTx{false};       // DAX/TCI audio replaces the mic capture
    bool        transmitting{false};

    struct Stage {
        QString label;
        bool    enabled{false};
    };
    QVector<Stage> aetherTxStages;  // processing order, RN2 first
    bool        aetherTxBypass{false};
    int         pcLevelPercent{100};
    bool        quindarOn{false};
    bool        limiterOn{false};

    int         radioMicLevel{0};   // the radio's mic gain, for a radio-side input
    bool        txEqOn{false};
    std::array<int, 8> txEqBands{}; // dB, 63 Hz .. 8 kHz
    bool        procOn{false};
    int         procLevel{0};
    int         procMaximum{2};     // 2 = NOR / DX / DX+
    int         txFilterLowHz{0};
    int         txFilterHighHz{0};
    int         rfPowerPercent{0};

    float       radioMicDbfs{kNoReading}; // the radio's MIC meter
    float       compressionDb{kNoReading};
    float       alcValue{kNoReading};
    QString     alcUnit;
    float       forwardWatts{kNoReading};
};

// Tools → Audio Chain Visualizer: the whole TX audio path as blocks, from the
// mic source through AetherTX on the computer to the radio's EQ, processor,
// filter/ALC and transmitter, with live levels. The mic source is chosen
// here; Pin on top keeps the window above the main window.
class AudioChainVisualizer : public CanonWindow {
    Q_OBJECT

public:
    using SnapshotProvider = std::function<AudioChainSnapshot()>;

    explicit AudioChainVisualizer(QWidget* parent = nullptr);
    ~AudioChainVisualizer() override;

    // Called on every refresh tick while the window is visible.
    void setSnapshotProvider(SnapshotProvider provider);
    void refresh();

    // AetherSDR's TX output level (post-limiter), from the PC mic meter.
    void setPcOutputLevel(float peakDbfs);

signals:
    void micSourceRequested(const QString& source);
    void aetherTxRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void applySnapshot(const AudioChainSnapshot& s);
    void rebuildStageChips(const AudioChainSnapshot& s);
    void rebuildMicMenu(const AudioChainSnapshot& s);
    void setPinned(bool on);
    void setMeter(int index, float fraction);
    void tickMeters();

    SnapshotProvider m_provider;
    QTimer*      m_refreshTimer{nullptr};
    QTimer*      m_meterTimer{nullptr};
    QElapsedTimer m_meterClock;

    QToolButton* m_micButton{nullptr};
    QMenu*       m_micMenu{nullptr};
    QLabel*      m_txStatus{nullptr};
    QPushButton* m_pinButton{nullptr};

    ChainCanvas* m_canvas{nullptr};
    QLabel*      m_macNote{nullptr};
    QWidget*     m_stageRow{nullptr};
    QLabel*      m_linkLabel{nullptr};

    ChainCard*   m_pcInput{nullptr};
    ChainCard*   m_aetherTx{nullptr};
    QPushButton* m_openAetherTx{nullptr};
    ChainCard*   m_pcLevel{nullptr};
    ChainCard*   m_quindar{nullptr};
    ChainCard*   m_limiter{nullptr};

    ChainCard*   m_radioInput{nullptr};
    QLabel*      m_radioInputArrow{nullptr};
    ChainCard*   m_radioLevel{nullptr};
    QLabel*      m_radioLevelArrow{nullptr};
    ChainCard*   m_txEq{nullptr};
    std::array<QProgressBar*, 8> m_eqBars{};
    ChainCard*   m_processor{nullptr};
    ChainCard*   m_filterAlc{nullptr};
    ChainCard*   m_transmitter{nullptr};

    QVector<QProgressBar*>  m_meters;
    QVector<MeterSmoother>  m_smoothers;
    QStringList  m_lastStageKey;
    QStringList  m_lastMicSources;
    QString      m_lastMicSource;
    float        m_pcOutDbfs{AudioChainSnapshot::kNoReading};
    qint64       m_pcOutAtMs{0};
};

} // namespace AetherSDR
