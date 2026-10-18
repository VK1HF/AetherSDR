#include "StripDelayPanel.h"
#include "ClientCompKnob.h"
#include "EditorFramelessTitleBar.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ThemeManager.h"
#include "core/ClientDelay.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr const char* kWindowStyle =
    "QWidget { background: {{color.background.0}}; color: {{color.text.primary}}; }"
    "QLabel  { background: transparent; color: {{color.text.secondary}}; font-size: 11px; }";

// Visualisation: the dry voice burst (cyan) and its single echo (magenta),
// offset right by Time (0..50 ms spans 0..50% of the width) and scaled by
// Level. The two overlap at short times, which is the doubling effect.
class EchoBox : public QWidget {
public:
    explicit EchoBox(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(138);
    }
    void setTimeMs(float v) { m_timeMs = v; update(); }
    void setMix(float v)    { m_mix = v;    update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect();
        p.fillRect(r, ThemeManager::instance().color(this, "color.background.0"));

        const QColor gridColor = ThemeManager::instance().color(this, "color.border.subtle");
        const QColor axisColor = ThemeManager::instance().color(this, "color.border.strong");
        p.setPen(QPen(gridColor, 1.0));
        for (float n : { 0.25f, 0.50f, 0.75f }) {
            const float x = r.left() + n * r.width();
            const float y = r.top()  + n * r.height();
            p.drawLine(QPointF(x, r.top()),  QPointF(x, r.bottom()));
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
        p.setPen(QPen(axisColor, 1.0));
        const float cy = r.top() + 0.5f * r.height();
        p.drawLine(QPointF(r.left(), cy), QPointF(r.right(), cy));

        const float w      = r.width();
        const float maxAmp = r.height() * 0.40f;
        const float xStart = w * 0.04f;
        const float burst  = w * 0.40f;   // length of one voice burst
        const float echoX  = xStart + (m_timeMs / ClientDelay::kMaxTimeMs) * w * 0.50f;

        // One windowed tone burst starting at x0.
        auto drawBurst = [&](float x0, float amp, const QPen& pen) {
            constexpr float kCycles = 7.0f;
            const float freq = kCycles * 2.0f * float(M_PI) / burst;
            QPainterPath path;
            path.moveTo(x0, cy);
            for (float x = x0; x <= x0 + burst && x <= w - 2.0f; x += 0.5f) {
                const float t   = (x - x0) / burst;
                const float env = std::sin(float(M_PI) * t);
                path.lineTo(x, cy + amp * env * std::sin(freq * (x - x0)));
            }
            p.setPen(pen);
            p.drawPath(path);
        };

        // Echo first so the dry burst reads on top.
        drawBurst(echoX, m_mix * maxAmp, QPen(QColor("#c060e0"), 1.8));
        drawBurst(xStart, (1.0f - 0.5f * m_mix) * maxAmp,
                  QPen(ThemeManager::instance().color(this, "color.accent"), 2.2));
    }

private:
    float m_timeMs{ClientDelay::kDefaultTimeMs};
    float m_mix{ClientDelay::kDefaultMix};
};

} // namespace

StripDelayPanel::StripDelayPanel(AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_audio(engine)
{
    const QString title = QString::fromUtf8("Aetherial Delay \xe2\x80\x94 TX");
    setWindowTitle(title);
    AetherSDR::ThemeManager::instance().applyStyleSheet(this, kWindowStyle);
    resize(480, 180);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 0, 16, 14);
    root->setSpacing(8);

    auto* titleBar = new EditorFramelessTitleBar;
    titleBar->setTitleText(title);
    root->addWidget(titleBar);

    auto* echoBox = new EchoBox;
    m_viz = echoBox;
    root->addWidget(echoBox, 1);

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    row->addStretch();

    auto makeKnob = [](const QString& label) {
        auto* k = new ClientCompKnob;
        k->setLabel(label);
        k->setCenterLabelMode(true);
        k->setFixedSize(76, 76);
        return k;
    };

    m_time = makeKnob("Time");
    m_time->setRange(0.0f, ClientDelay::kMaxTimeMs);
    m_time->setDefault(ClientDelay::kDefaultTimeMs);
    m_time->setValueFromNorm([](float n) { return n * ClientDelay::kMaxTimeMs; });
    m_time->setNormFromValue([](float v) { return v / ClientDelay::kMaxTimeMs; });
    m_time->setLabelFormat([](float v) {
        return QString::number(v, 'f', 0) + " ms";
    });
    m_time->setToolTip("How far the echo trails your voice. "
                       "Short times (10-30 ms) thicken; longer ones start to slap.");
    row->addWidget(m_time);

    m_level = makeKnob("Level");
    m_level->setRange(0.0f, 1.0f);
    m_level->setDefault(ClientDelay::kDefaultMix);
    m_level->setValueFromNorm([](float n) { return n; });
    m_level->setNormFromValue([](float v) { return v; });
    m_level->setLabelFormat([](float v) {
        return QString::number(static_cast<int>(v * 100.0f + 0.5f)) + " %";
    });
    m_level->setToolTip("How loud the echo is under your voice. "
                        "25 % sits about 11 dB below it.");
    row->addWidget(m_level);

    row->addStretch();
    root->addLayout(row);

    connect(m_time, &ClientCompKnob::valueChanged, this, [this, echoBox](float v) {
        if (m_audio && m_audio->clientDelayTx()) {
            m_audio->clientDelayTx()->setTimeMs(v);
            m_audio->saveClientDelaySettings();
        }
        echoBox->setTimeMs(v);
    });
    connect(m_level, &ClientCompKnob::valueChanged, this, [this, echoBox](float v) {
        if (m_audio && m_audio->clientDelayTx()) {
            m_audio->clientDelayTx()->setMix(v);
            m_audio->saveClientDelaySettings();
        }
        echoBox->setMix(v);
    });

    syncControlsFromEngine();

    if (m_audio) {
        connect(m_audio, &AudioEngine::clientDelayStateChanged,
                this, &StripDelayPanel::syncControlsFromEngine);
    }
}

StripDelayPanel::~StripDelayPanel() = default;

void StripDelayPanel::showForTx()
{
    syncControlsFromEngine();
    restoreGeometryFromSettings();
    show();
    raise();
    activateWindow();
}

void StripDelayPanel::syncControlsFromEngine()
{
    if (!m_audio || !m_audio->clientDelayTx()) return;
    ClientDelay* d = m_audio->clientDelayTx();
    m_restoring = true;
    if (m_time)  { QSignalBlocker b(m_time);  m_time->setValue(d->timeMs()); }
    if (m_level) { QSignalBlocker b(m_level); m_level->setValue(d->mix()); }
    if (auto* viz = dynamic_cast<EchoBox*>(m_viz)) {
        viz->setTimeMs(d->timeMs());
        viz->setMix(d->mix());
    }
    m_restoring = false;
}

void StripDelayPanel::saveGeometryToSettings()
{
    auto& s = AppSettings::instance();
    s.setValue("StripDelayPanelGeometry", saveGeometry().toBase64());
}

void StripDelayPanel::restoreGeometryFromSettings()
{
    auto& s = AppSettings::instance();
    const QByteArray geom = QByteArray::fromBase64(
        s.value("StripDelayPanelGeometry", "").toByteArray());
    if (!geom.isEmpty()) {
        m_restoring = true;
        restoreGeometry(geom);
        m_restoring = false;
    }
}

void StripDelayPanel::closeEvent(QCloseEvent* ev)
{
    saveGeometryToSettings();
    QWidget::closeEvent(ev);
}

void StripDelayPanel::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoring) saveGeometryToSettings();
}

void StripDelayPanel::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoring) saveGeometryToSettings();
}

} // namespace AetherSDR
