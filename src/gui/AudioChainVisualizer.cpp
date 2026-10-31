#include "AudioChainVisualizer.h"

#include "CanonIndicators.h"
#include "FramelessResizer.h"
#include "core/AppSettings.h"
#include "core/ThemeManager.h"

#include <QAccessible>
#include <QAction>
#include <QActionGroup>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QHideEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr int kRefreshMs = 100;
constexpr int kMeterRange = 1000;
// The PC output meter arrives only while capture runs; older than this, the
// last reading no longer describes the audio.
constexpr qint64 kPcOutStaleMs = 500;
constexpr const char* kPinnedKey = "AudioChainVisualizerPinned";

constexpr const char* kStyle = R"(
QWidget {
    color: {{color.canon.inkSoft}};
    background: transparent;
    font-size: 13px;
}
QLabel#acvTitle {
    color: {{color.canon.ink}};
    font-size: 20px;
    font-weight: 700;
}
QFrame#acvRule {
    background: {{color.canon.line}};
    border: none;
    min-height: 1px;
    max-height: 1px;
}
QToolButton#acvMicSource, QPushButton#acvButton {
    background: {{color.canon.control}};
    border: 1px solid {{color.canon.lineHi}};
    border-radius: 8px;
    color: {{color.canon.cyan}};
    padding: 6px 14px;
    min-height: 22px;
}
QToolButton#acvMicSource:hover, QPushButton#acvButton:hover {
    background: {{color.canon.nested}};
    color: {{color.canon.aqua}};
}
QPushButton#acvButton:checked {
    background: {{color.canon.cyan}};
    color: {{color.canon.onAccent}};
}
QToolButton#acvMicSource:disabled, QPushButton#acvButton:disabled {
    color: {{color.canon.muted}};
}
QLabel#acvTxStatus {
    border: 1px solid {{color.canon.lineHi}};
    border-radius: 12px;
    padding: 4px 12px;
    color: {{color.canon.muted}};
    font-weight: 600;
}
QLabel#acvTxStatus[tx="true"] {
    border-color: {{color.slice.tx}};
    color: {{color.slice.tx}};
}
QFrame#acvSection {
    background: {{color.canon.raised}};
    border: 1px solid {{color.canon.line}};
    border-radius: 12px;
}
QFrame#acvSection[inPath="false"] {
    border: 1px dashed {{color.canon.lineHi}};
}
QLabel#acvSectionTitle {
    color: {{color.canon.ink}};
    font-size: 14px;
    font-weight: 600;
}
QLabel#acvSectionNote, QLabel#acvLegend {
    color: {{color.canon.muted}};
    font-size: 12px;
}
QFrame#acvSwatch[zone="mac"] { background: {{color.canon.category.blue}}; border-radius: 2px; }
QFrame#acvSwatch[zone="radio"] { background: {{color.canon.category.amber}}; border-radius: 2px; }
QLabel#acvLink {
    background: {{color.canon.ground}};
    border: 1px solid {{color.canon.lineHi}};
    border-radius: 11px;
    padding: 2px 12px;
    color: {{color.canon.inkSoft}};
    font-family: "JetBrains Mono", "Menlo", "Consolas", monospace;
    font-size: 12px;
}
QLabel#acvArrow {
    color: {{color.canon.muted}};
    font-size: 16px;
}
QFrame#acvCard {
    background: {{color.canon.nested}};
    border: 1px solid {{color.canon.line}};
    border-radius: 10px;
}
QFrame#acvCard[zone="mac"][state="on"] { border-color: {{color.canon.category.blue}}; }
QFrame#acvCard[zone="radio"][state="on"] { border-color: {{color.canon.category.amber}}; }
QFrame#acvCard[state="off"] {
    background: {{color.canon.raised}};
    border: 1px dashed {{color.canon.lineHi}};
}
QLabel#acvKicker {
    color: {{color.canon.muted}};
    font-size: 10px;
    font-weight: 700;
}
QLabel#acvName {
    color: {{color.canon.ink}};
    font-size: 15px;
    font-weight: 600;
}
QFrame#acvCard[state="off"] QLabel#acvName { color: {{color.canon.muted}}; }
QLabel#acvValue {
    color: {{color.canon.inkSoft}};
    font-family: "JetBrains Mono", "Menlo", "Consolas", monospace;
    font-size: 12px;
}
QFrame#acvCard[state="off"] QLabel#acvValue { color: {{color.canon.muted}}; }
QLabel#acvChip {
    background: {{color.canon.control}};
    color: {{color.canon.muted}};
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 10px;
    font-weight: 700;
}
QFrame#acvCard[zone="mac"][state="on"] QLabel#acvChip { color: {{color.canon.category.blue}}; }
QFrame#acvCard[zone="radio"][state="on"] QLabel#acvChip { color: {{color.canon.category.amber}}; }
QLabel#acvStage {
    background: {{color.canon.control}};
    color: {{color.canon.cyan}};
    border-radius: 4px;
    padding: 2px 6px;
    font-family: "JetBrains Mono", "Menlo", "Consolas", monospace;
    font-size: 11px;
}
QLabel#acvStage[on="false"] {
    color: {{color.canon.muted}};
    text-decoration: line-through;
}
QLabel#acvStageArrow {
    color: {{color.canon.muted}};
    font-size: 11px;
}
QProgressBar#acvMeter {
    background: {{color.canon.control}};
    border: none;
    border-radius: 3px;
    min-height: 6px;
    max-height: 6px;
}
QProgressBar#acvMeter::chunk { background: {{color.canon.category.blue}}; border-radius: 3px; }
QProgressBar#acvMeter[zone="radio"]::chunk { background: {{color.canon.category.amber}}; }
QProgressBar#acvEqBand {
    background: {{color.canon.control}};
    border: none;
    border-radius: 2px;
    min-width: 10px;
    max-width: 10px;
}
QProgressBar#acvEqBand::chunk { background: {{color.canon.category.amber}}; border-radius: 2px; }
)";

void restyle(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    for (QWidget* child : w->findChildren<QWidget*>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
    w->update();
}

bool setPropertyIfChanged(QWidget* w, const char* name, const QVariant& value)
{
    if (w->property(name) == value) {
        return false;
    }
    w->setProperty(name, value);
    return true;
}

QLabel* makeArrow(QWidget* parent)
{
    auto* arrow = new QLabel(QStringLiteral("→"), parent);
    arrow->setObjectName(QStringLiteral("acvArrow"));
    arrow->setAlignment(Qt::AlignCenter);
    arrow->setFixedWidth(20);
    arrow->setAccessibleName(QStringLiteral("then"));
    return arrow;
}

float dbfsFraction(float dbfs)
{
    if (!std::isfinite(dbfs)) {
        return 0.0f;
    }
    return std::clamp((dbfs + 60.0f) / 60.0f, 0.0f, 1.0f);
}

QString formatDbfs(float dbfs)
{
    if (!std::isfinite(dbfs)) {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1 dBFS").arg(dbfs, 0, 'f', 0);
}

QString micSourceDescription(const QString& source)
{
    if (source == QLatin1String("PC")) {
        return QStringLiteral("PC — the computer's audio input (AetherTX applies)");
    }
    if (source == QLatin1String("MIC")) {
        return QStringLiteral("MIC — the radio's microphone jack");
    }
    if (source == QLatin1String("BAL")) {
        return QStringLiteral("BAL — the radio's balanced input");
    }
    if (source == QLatin1String("LINE")) {
        return QStringLiteral("LINE — the radio's line input");
    }
    if (source == QLatin1String("ACC")) {
        return QStringLiteral("ACC — the radio's accessory connector");
    }
    return source;
}

} // namespace

// One block in the chain: a kicker, a name, a value line, an optional meter
// and a state chip. Its zone (mac / radio) and state (on / off) are style
// properties, so the canon sheet draws the colours.
class ChainCard : public QFrame {
public:
    ChainCard(const QString& zone, const QString& kicker, const QString& name,
              int width, QWidget* parent)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("acvCard"));
        setProperty("zone", zone);
        setProperty("state", QStringLiteral("on"));
        setFixedWidth(width);

        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(12, 12, 12, 10);
        m_layout->setSpacing(6);

        m_kicker = new QLabel(kicker.toUpper(), this);
        m_kicker->setObjectName(QStringLiteral("acvKicker"));
        m_name = new QLabel(name, this);
        m_name->setObjectName(QStringLiteral("acvName"));
        m_value = new QLabel(this);
        m_value->setObjectName(QStringLiteral("acvValue"));
        m_value->setWordWrap(true);
        m_layout->addWidget(m_kicker);
        m_layout->addWidget(m_name);
        m_layout->addWidget(m_value);
        m_extraIndex = m_layout->count();

        m_chip = new QLabel(this);
        m_chip->setObjectName(QStringLiteral("acvChip"));
        m_layout->addStretch(1);
        m_layout->addWidget(m_chip, 0, Qt::AlignLeft);
        setAccessibleName(name);
    }

    void addExtra(QWidget* w)
    {
        m_layout->insertWidget(m_extraIndex++, w);
    }

    QProgressBar* addMeter(const QString& accessibleName)
    {
        auto* bar = new QProgressBar(this);
        bar->setObjectName(QStringLiteral("acvMeter"));
        bar->setProperty("zone", property("zone"));
        bar->setRange(0, kMeterRange);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setAccessibleName(accessibleName);
        addExtra(bar);
        return bar;
    }

    void setName(const QString& name)
    {
        m_name->setText(name);
        setAccessibleName(name);
    }

    void setValue(const QString& text) { m_value->setText(text); }

    // Returns true when the state changed and the card needs a restyle.
    bool setState(bool on, const QString& chip)
    {
        m_chip->setText(chip);
        setAccessibleDescription(QStringLiteral("%1. %2").arg(chip, m_value->text()));
        return setPropertyIfChanged(this, "state", on ? QStringLiteral("on") : QStringLiteral("off"));
    }

private:
    QVBoxLayout* m_layout{nullptr};
    QLabel*      m_kicker{nullptr};
    QLabel*      m_name{nullptr};
    QLabel*      m_value{nullptr};
    QLabel*      m_chip{nullptr};
    int          m_extraIndex{0};
};

// Holds the two sections and draws the connector from the computer's last
// block to the radio's first, through the gap between them.
class ChainCanvas : public QWidget {
public:
    using QWidget::QWidget;

    QFrame*   macSection{nullptr};
    QFrame*   radioSection{nullptr};
    QWidget*  from{nullptr};
    QWidget*  to{nullptr};
    bool      linked{true};

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QWidget::paintEvent(event);
        if (!linked || !macSection || !radioSection || !from || !to
            || !to->isVisible()) {
            return;
        }
        const int top = macSection->geometry().bottom() + 1;
        const int bottom = radioSection->geometry().top();
        const int midY = (top + bottom) / 2;
        const int fromX = from->mapTo(this, from->rect().center()).x();
        const int toX = to->mapTo(this, to->rect().center()).x();

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor ink = ThemeManager::instance().color(this, QStringLiteral("color.canon.muted"));
        p.setPen(QPen(ink, 2, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        QPainterPath path;
        path.moveTo(fromX, top);
        path.lineTo(fromX, midY);
        path.lineTo(toX, midY);
        path.lineTo(toX, bottom - 7);
        p.drawPath(path);

        QPainterPath head;
        head.moveTo(toX - 6, bottom - 9);
        head.lineTo(toX + 6, bottom - 9);
        head.lineTo(toX, bottom);
        head.closeSubpath();
        p.fillPath(head, ink);
    }
};

AudioChainVisualizer::AudioChainVisualizer(QWidget* parent)
    : CanonWindow(QStringLiteral("Audio Chain Visualizer"), parent, Kind::Workspace)
{
    theme::setContainer(this, QStringLiteral("dialog/audioChainVisualizer"));
    setGeometryKey(QStringLiteral("AudioChainVisualizerGeometry"));
    setMinimumSize(1160, 640);
    resize(1220, 680);
    FramelessResizer::install(this);
    bodyWidget()->setObjectName(QStringLiteral("audioChainVisualizerBody"));
    applyCanonSheet(bodyWidget(), [] { return QString::fromLatin1(kStyle); });

    buildUi();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(kRefreshMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &AudioChainVisualizer::refresh);

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(kMeterSmootherIntervalMs);
    connect(m_meterTimer, &QTimer::timeout, this, &AudioChainVisualizer::tickMeters);

    const bool pinned = AppSettings::instance()
        .value(QString::fromLatin1(kPinnedKey), QStringLiteral("False")).toString()
        == QLatin1String("True");
    m_pinButton->setChecked(pinned);
    setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
}

AudioChainVisualizer::~AudioChainVisualizer() = default;

void AudioChainVisualizer::buildUi()
{
    auto* body = new QVBoxLayout(bodyWidget());
    body->setSpacing(12);
    body->setContentsMargins(22, 18, 22, 20);

    auto* title = new QLabel(QStringLiteral("Audio Chain Visualizer"), bodyWidget());
    title->setObjectName(QStringLiteral("acvTitle"));
    title->setContentsMargins(0, 0, 40, 0);
    body->addWidget(title);
    auto* rule = new QFrame(bodyWidget());
    rule->setObjectName(QStringLiteral("acvRule"));
    body->addWidget(rule);

    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(12);
    m_micButton = new QToolButton(bodyWidget());
    m_micButton->setObjectName(QStringLiteral("acvMicSource"));
    m_micButton->setPopupMode(QToolButton::InstantPopup);
    m_micButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_micButton->setAccessibleName(QStringLiteral("Mic source"));
    m_micButton->setAccessibleDescription(
        QStringLiteral("Choose which input the radio transmits from"));
    m_micMenu = new QMenu(m_micButton);
    m_micButton->setMenu(m_micMenu);
    toolbar->addWidget(m_micButton);

    m_txStatus = new QLabel(QStringLiteral("Receiving"), bodyWidget());
    m_txStatus->setObjectName(QStringLiteral("acvTxStatus"));
    m_txStatus->setProperty("tx", false);
    m_txStatus->setAccessibleName(QStringLiteral("Transmit state"));
    toolbar->addWidget(m_txStatus);
    toolbar->addStretch(1);

    m_pinButton = new QPushButton(QStringLiteral("Pin on top"), bodyWidget());
    m_pinButton->setObjectName(QStringLiteral("acvButton"));
    m_pinButton->setCheckable(true);
    m_pinButton->setAccessibleName(QStringLiteral("Pin on top"));
    m_pinButton->setAccessibleDescription(
        QStringLiteral("Keep this window above the main window"));
    connect(m_pinButton, &QPushButton::toggled, this, &AudioChainVisualizer::setPinned);
    toolbar->addWidget(m_pinButton);
    body->addLayout(toolbar);

    m_canvas = new ChainCanvas(bodyWidget());
    auto* canvasLayout = new QVBoxLayout(m_canvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);

    auto makeSection = [this](const QString& zone, const QString& heading,
                              QLabel*& note, QHBoxLayout*& row) {
        auto* section = new QFrame(m_canvas);
        section->setObjectName(QStringLiteral("acvSection"));
        section->setProperty("inPath", true);
        auto* v = new QVBoxLayout(section);
        v->setContentsMargins(16, 14, 16, 16);
        v->setSpacing(12);
        auto* head = new QHBoxLayout;
        head->setSpacing(10);
        auto* swatch = new QFrame(section);
        swatch->setObjectName(QStringLiteral("acvSwatch"));
        swatch->setProperty("zone", zone);
        swatch->setFixedSize(10, 10);
        auto* label = new QLabel(heading, section);
        label->setObjectName(QStringLiteral("acvSectionTitle"));
        note = new QLabel(section);
        note->setObjectName(QStringLiteral("acvSectionNote"));
        head->addWidget(swatch, 0, Qt::AlignVCenter);
        head->addWidget(label);
        head->addWidget(note, 1);
        v->addLayout(head);
        row = new QHBoxLayout;
        row->setSpacing(0);
        v->addLayout(row);
        return section;
    };

    // ── On the computer ──
    QHBoxLayout* macRow = nullptr;
    QFrame* macSection = makeSection(QStringLiteral("mac"), QStringLiteral("On your computer"),
                                     m_macNote, macRow);
    m_pcInput = new ChainCard(QStringLiteral("mac"), QStringLiteral("Input"),
                              QStringLiteral("PC microphone"), 140, macSection);
    m_aetherTx = new ChainCard(QStringLiteral("mac"), QStringLiteral("Client DSP · in this order"),
                               QStringLiteral("AetherTX"), 500, macSection);
    m_stageRow = new QWidget(m_aetherTx);
    auto* stageLayout = new QHBoxLayout(m_stageRow);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(4);
    m_aetherTx->addExtra(m_stageRow);
    m_openAetherTx = new QPushButton(QStringLiteral("Open AetherTX"), m_aetherTx);
    m_openAetherTx->setObjectName(QStringLiteral("acvButton"));
    m_openAetherTx->setAccessibleName(QStringLiteral("Open AetherTX"));
    connect(m_openAetherTx, &QPushButton::clicked, this, &AudioChainVisualizer::aetherTxRequested);
    m_aetherTx->addExtra(m_openAetherTx);
    m_pcLevel = new ChainCard(QStringLiteral("mac"), QStringLiteral("Level slider"),
                              QStringLiteral("Mic level"), 140, macSection);
    m_quindar = new ChainCard(QStringLiteral("mac"), QStringLiteral("Tones"),
                              QStringLiteral("Quindar"), 140, macSection);
    m_limiter = new ChainCard(QStringLiteral("mac"), QStringLiteral("Safety"),
                              QStringLiteral("Final limiter"), 140, macSection);

    const QList<ChainCard*> macCards{m_pcInput, m_aetherTx, m_pcLevel, m_quindar, m_limiter};
    for (int i = 0; i < macCards.size(); ++i) {
        if (i > 0) {
            macRow->addWidget(makeArrow(macSection));
        }
        macRow->addWidget(macCards[i]);
    }
    macRow->addStretch(1);

    m_meters.append(m_limiter->addMeter(QStringLiteral("AetherSDR output level")));

    // ── The link between them ──
    auto* linkRow = new QWidget(m_canvas);
    linkRow->setFixedHeight(48);
    auto* linkLayout = new QHBoxLayout(linkRow);
    linkLayout->setContentsMargins(0, 0, 0, 0);
    m_linkLabel = new QLabel(QStringLiteral("Opus · 24 kHz → network → radio"), linkRow);
    m_linkLabel->setObjectName(QStringLiteral("acvLink"));
    linkLayout->addStretch(1);
    linkLayout->addWidget(m_linkLabel, 0, Qt::AlignVCenter);
    linkLayout->addStretch(1);

    // ── In the radio ──
    QLabel* radioNote = nullptr;
    QHBoxLayout* radioRow = nullptr;
    QFrame* radioSection = makeSection(QStringLiteral("radio"), QStringLiteral("In the radio"),
                                       radioNote, radioRow);
    radioNote->setText(QStringLiteral("The radio's own processing, set from the TX and P/CW panels"));

    m_radioInput = new ChainCard(QStringLiteral("radio"), QStringLiteral("Input"),
                                 QStringLiteral("MIC jack"), 140, radioSection);
    m_meters.append(m_radioInput->addMeter(QStringLiteral("Radio mic level")));
    m_radioLevel = new ChainCard(QStringLiteral("radio"), QStringLiteral("Level slider"),
                                 QStringLiteral("Mic gain"), 140, radioSection);
    m_txEq = new ChainCard(QStringLiteral("radio"), QStringLiteral("TX panel"),
                           QStringLiteral("TX EQ"), 150, radioSection);
    auto* eqRow = new QWidget(m_txEq);
    auto* eqLayout = new QHBoxLayout(eqRow);
    eqLayout->setContentsMargins(0, 0, 0, 0);
    eqLayout->setSpacing(3);
    static const char* kBandNames[] = {"63 Hz", "125 Hz", "250 Hz", "500 Hz",
                                       "1 kHz", "2 kHz", "4 kHz", "8 kHz"};
    for (int i = 0; i < 8; ++i) {
        auto* bar = new QProgressBar(eqRow);
        bar->setObjectName(QStringLiteral("acvEqBand"));
        bar->setOrientation(Qt::Vertical);
        bar->setRange(0, 20);
        bar->setValue(10);
        bar->setTextVisible(false);
        bar->setFixedHeight(28);
        bar->setAccessibleName(QStringLiteral("TX EQ %1").arg(QLatin1String(kBandNames[i])));
        eqLayout->addWidget(bar);
        m_eqBars[static_cast<size_t>(i)] = bar;
    }
    eqLayout->addStretch(1);
    m_txEq->addExtra(eqRow);
    m_processor = new ChainCard(QStringLiteral("radio"), QStringLiteral("P/CW panel"),
                                QStringLiteral("Processor"), 150, radioSection);
    m_meters.append(m_processor->addMeter(QStringLiteral("Processor compression")));
    m_filterAlc = new ChainCard(QStringLiteral("radio"), QStringLiteral("Radio"),
                                QStringLiteral("Filter + ALC"), 150, radioSection);
    m_meters.append(m_filterAlc->addMeter(QStringLiteral("ALC level")));
    m_transmitter = new ChainCard(QStringLiteral("radio"), QStringLiteral("Output"),
                                  QStringLiteral("Transmitter"), 150, radioSection);

    radioRow->addWidget(m_radioInput);
    m_radioInputArrow = makeArrow(radioSection);
    radioRow->addWidget(m_radioInputArrow);
    radioRow->addWidget(m_radioLevel);
    m_radioLevelArrow = makeArrow(radioSection);
    radioRow->addWidget(m_radioLevelArrow);
    radioRow->addWidget(m_txEq);
    radioRow->addWidget(makeArrow(radioSection));
    radioRow->addWidget(m_processor);
    radioRow->addWidget(makeArrow(radioSection));
    radioRow->addWidget(m_filterAlc);
    radioRow->addWidget(makeArrow(radioSection));
    radioRow->addWidget(m_transmitter);
    radioRow->addStretch(1);

    canvasLayout->addWidget(macSection);
    canvasLayout->addWidget(linkRow);
    canvasLayout->addWidget(radioSection);
    m_canvas->macSection = macSection;
    m_canvas->radioSection = radioSection;
    m_canvas->from = m_limiter;
    m_canvas->to = m_txEq;
    body->addWidget(m_canvas);

    auto* legend = new QLabel(
        QStringLiteral("Blue: on your computer   ·   Amber: in the radio   ·   "
                       "Dashed: bypassed, audio passes straight through"),
        bodyWidget());
    legend->setObjectName(QStringLiteral("acvLegend"));
    body->addWidget(legend);
    body->addStretch(1);

    m_smoothers.resize(m_meters.size());
}

void AudioChainVisualizer::setSnapshotProvider(SnapshotProvider provider)
{
    m_provider = std::move(provider);
    if (isVisible()) {
        refresh();
    }
}

void AudioChainVisualizer::setPcOutputLevel(float peakDbfs)
{
    m_pcOutDbfs = peakDbfs;
    m_pcOutAtMs = QDateTime::currentMSecsSinceEpoch();
}

void AudioChainVisualizer::showEvent(QShowEvent* event)
{
    CanonWindow::showEvent(event);
    refresh();
    m_refreshTimer->start();
}

void AudioChainVisualizer::hideEvent(QHideEvent* event)
{
    m_refreshTimer->stop();
    m_meterTimer->stop();
    CanonWindow::hideEvent(event);
}

void AudioChainVisualizer::refresh()
{
    if (m_provider) {
        applySnapshot(m_provider());
    }
}

void AudioChainVisualizer::setPinned(bool on)
{
    auto& settings = AppSettings::instance();
    settings.setValue(QString::fromLatin1(kPinnedKey),
                      on ? QStringLiteral("True") : QStringLiteral("False"));
    settings.save();
    if (windowFlags().testFlag(Qt::WindowStaysOnTopHint) == on) {
        return;
    }
    // Changing a window flag re-creates the native window, which hides it.
    const QRect geometryBefore = geometry();
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (wasVisible) {
        setGeometry(geometryBefore);
        show();
        raise();
    }
}

void AudioChainVisualizer::setMeter(int index, float fraction)
{
    if (index < 0 || index >= m_smoothers.size()) {
        return;
    }
    m_smoothers[index].setTarget(std::clamp(fraction, 0.0f, 1.0f));
    if (m_smoothers[index].needsAnimation() && !m_meterTimer->isActive()) {
        m_meterTimer->start();
    }
    m_meters[index]->setValue(static_cast<int>(m_smoothers[index].value() * kMeterRange));
}

void AudioChainVisualizer::tickMeters()
{
    QElapsedTimer& clock = m_meterClock;
    const qint64 elapsed = clock.isValid() ? clock.restart() : kMeterSmootherIntervalMs;
    if (!clock.isValid()) {
        clock.start();
    }
    bool animating = false;
    for (int i = 0; i < m_smoothers.size(); ++i) {
        animating |= m_smoothers[i].tick(elapsed);
        m_meters[i]->setValue(static_cast<int>(m_smoothers[i].value() * kMeterRange));
    }
    if (!animating) {
        m_meterTimer->stop();
        clock.invalidate();
    }
}

void AudioChainVisualizer::rebuildMicMenu(const AudioChainSnapshot& s)
{
    if (s.micSources == m_lastMicSources && s.micSource == m_lastMicSource) {
        return;
    }
    m_lastMicSources = s.micSources;
    m_lastMicSource = s.micSource;
    m_micMenu->clear();
    auto* group = new QActionGroup(m_micMenu);
    group->setExclusive(true);
    for (const QString& source : s.micSources) {
        QAction* action = m_micMenu->addAction(micSourceDescription(source));
        action->setCheckable(true);
        action->setChecked(source == s.micSource);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, source] {
            emit micSourceRequested(source);
        });
    }
}

void AudioChainVisualizer::rebuildStageChips(const AudioChainSnapshot& s)
{
    QStringList key;
    for (const auto& stage : s.aetherTxStages) {
        key << stage.label + (stage.enabled && !s.aetherTxBypass ? QStringLiteral("+")
                                                                 : QStringLiteral("-"));
    }
    if (key == m_lastStageKey) {
        return;
    }
    m_lastStageKey = key;

    auto* layout = static_cast<QHBoxLayout*>(m_stageRow->layout());
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int i = 0; i < s.aetherTxStages.size(); ++i) {
        const auto& stage = s.aetherTxStages[i];
        if (i > 0) {
            auto* arrow = new QLabel(QStringLiteral("→"), m_stageRow);
            arrow->setObjectName(QStringLiteral("acvStageArrow"));
            layout->addWidget(arrow);
        }
        const bool on = stage.enabled && !s.aetherTxBypass;
        auto* chip = new QLabel(stage.label, m_stageRow);
        chip->setObjectName(QStringLiteral("acvStage"));
        chip->setProperty("on", on);
        chip->setAccessibleName(QStringLiteral("%1, %2").arg(
            stage.label, on ? QStringLiteral("on") : QStringLiteral("bypassed")));
        layout->addWidget(chip);
    }
    layout->addStretch(1);
}

void AudioChainVisualizer::applySnapshot(const AudioChainSnapshot& s)
{
    const bool pcPath = s.connected && !s.daxTx && s.micSource == QLatin1String("PC");
    const bool radioInputPath = s.connected && !s.daxTx && !pcPath;

    // Toolbar.
    m_micButton->setEnabled(s.connected && !s.micSources.isEmpty());
    m_micButton->setText(s.connected
        ? QStringLiteral("Mic source: %1").arg(s.daxTx ? QStringLiteral("DAX") : s.micSource)
        : QStringLiteral("Mic source: not connected"));
    rebuildMicMenu(s);
    m_txStatus->setText(s.transmitting ? QStringLiteral("TRANSMITTING") : QStringLiteral("Receiving"));
    if (setPropertyIfChanged(m_txStatus, "tx", s.transmitting)) {
        restyle(m_txStatus);
    }

    // On the computer.
    QString note;
    if (!s.connected) {
        note = QStringLiteral("Not connected to a radio");
    } else if (s.daxTx) {
        note = QStringLiteral("Not in the path: DAX is sending audio from another app, which skips AetherTX");
    } else if (!pcPath) {
        note = QStringLiteral("Not in the path: the mic is on the radio's %1 input. Choose PC as the mic source to use AetherTX")
                   .arg(s.micSource);
    } else if (s.aetherTxBypass) {
        note = QStringLiteral("AetherTX BYPASS is on: every stage passes the audio straight through");
    } else {
        note = QStringLiteral("AetherSDR processes the audio before sending it to the radio");
    }
    m_macNote->setText(note);
    if (setPropertyIfChanged(m_canvas->macSection, "inPath", pcPath)) {
        restyle(m_canvas->macSection);
    }

    const QString notInPath = QStringLiteral("NOT IN PATH");
    QList<ChainCard*> dirty;
    auto mark = [&dirty](ChainCard* card, bool changed) {
        if (changed) {
            dirty << card;
        }
    };

    m_pcInput->setValue(s.pcInputDevice.isEmpty() ? QStringLiteral("System default input")
                                                   : s.pcInputDevice);
    mark(m_pcInput, m_pcInput->setState(pcPath, pcPath ? QStringLiteral("LIVE") : notInPath));

    rebuildStageChips(s);
    int enabledCount = 0;
    for (const auto& stage : s.aetherTxStages) {
        enabledCount += stage.enabled ? 1 : 0;
    }
    const bool aetherOn = pcPath && !s.aetherTxBypass && enabledCount > 0;
    m_aetherTx->setValue(QStringLiteral("%1 of %2 stages on").arg(enabledCount).arg(s.aetherTxStages.size()));
    mark(m_aetherTx, m_aetherTx->setState(aetherOn,
        !pcPath ? notInPath : (aetherOn ? QStringLiteral("ON") : QStringLiteral("BYPASS"))));

    m_pcLevel->setValue(QStringLiteral("%1 %").arg(s.pcLevelPercent));
    mark(m_pcLevel, m_pcLevel->setState(pcPath, pcPath ? QStringLiteral("ON") : notInPath));

    m_quindar->setValue(s.quindarOn ? QStringLiteral("Start and end tones") : QStringLiteral("Off"));
    mark(m_quindar, m_quindar->setState(pcPath && s.quindarOn,
        !pcPath ? notInPath : (s.quindarOn ? QStringLiteral("ON") : QStringLiteral("BYPASS"))));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const float pcOut = (pcPath && now - m_pcOutAtMs <= kPcOutStaleMs)
        ? m_pcOutDbfs : AudioChainSnapshot::kNoReading;
    m_limiter->setValue(QStringLiteral("Out %1").arg(formatDbfs(pcOut)));
    mark(m_limiter, m_limiter->setState(pcPath && s.limiterOn,
        !pcPath ? notInPath : (s.limiterOn ? QStringLiteral("ON") : QStringLiteral("BYPASS"))));
    setMeter(0, dbfsFraction(pcOut));

    m_canvas->linked = pcPath;
    m_linkLabel->setVisible(pcPath);

    // In the radio.
    const bool showRadioInput = s.connected && !pcPath;
    m_radioInput->setVisible(showRadioInput);
    m_radioInputArrow->setVisible(showRadioInput);
    m_radioLevel->setVisible(radioInputPath);
    m_radioLevelArrow->setVisible(radioInputPath);
    const float radioMic = s.transmitting ? s.radioMicDbfs : AudioChainSnapshot::kNoReading;
    if (s.daxTx) {
        m_radioInput->setName(QStringLiteral("DAX audio"));
        m_radioInput->setValue(QStringLiteral("From another app"));
    } else {
        m_radioInput->setName(QStringLiteral("%1 input").arg(s.micSource));
        m_radioInput->setValue(formatDbfs(radioMic));
    }
    mark(m_radioInput, m_radioInput->setState(true, QStringLiteral("LIVE")));
    setMeter(1, dbfsFraction(radioMic));
    m_radioLevel->setValue(QStringLiteral("%1 %").arg(s.radioMicLevel));
    mark(m_radioLevel, m_radioLevel->setState(true, QStringLiteral("ON")));

    for (size_t i = 0; i < m_eqBars.size(); ++i) {
        m_eqBars[i]->setValue(std::clamp(s.txEqBands[i] + 10, 0, 20));
        m_eqBars[i]->setAccessibleDescription(QStringLiteral("%1 dB").arg(s.txEqBands[i]));
    }
    m_txEq->setValue(s.txEqOn ? QStringLiteral("8-band, on") : QStringLiteral("8-band, off"));
    mark(m_txEq, m_txEq->setState(s.txEqOn, s.txEqOn ? QStringLiteral("ON") : QStringLiteral("BYPASS")));

    QString procSetting;
    if (s.procMaximum == 2) {
        static const char* kPresets[] = {"NOR", "DX", "DX+"};
        procSetting = QLatin1String(kPresets[std::clamp(s.procLevel, 0, 2)]);
    } else {
        procSetting = QStringLiteral("Level %1").arg(s.procLevel);
    }
    const float compression = (s.transmitting && s.procOn) ? s.compressionDb : AudioChainSnapshot::kNoReading;
    m_processor->setValue(std::isfinite(compression)
        ? QStringLiteral("%1 · %2 dB").arg(procSetting).arg(-std::fabs(compression), 0, 'f', 1)
        : procSetting);
    mark(m_processor, m_processor->setState(s.procOn, s.procOn ? QStringLiteral("ON") : QStringLiteral("BYPASS")));
    setMeter(2, std::isfinite(compression) ? std::fabs(compression) / 20.0f : 0.0f);

    m_filterAlc->setValue(QStringLiteral("%1–%2 Hz").arg(s.txFilterLowHz).arg(s.txFilterHighHz));
    mark(m_filterAlc, m_filterAlc->setState(true, QStringLiteral("ON")));
    float alcFraction = 0.0f;
    if (s.transmitting && std::isfinite(s.alcValue)) {
        alcFraction = s.alcUnit.contains(QLatin1Char('%')) ? s.alcValue / 100.0f
                                                           : dbfsFraction(s.alcValue);
    }
    setMeter(3, alcFraction);

    m_transmitter->setValue(s.transmitting && std::isfinite(s.forwardWatts)
        ? QStringLiteral("Forward %1 W").arg(s.forwardWatts, 0, 'f', 0)
        : QStringLiteral("RF power %1 %").arg(s.rfPowerPercent));
    mark(m_transmitter, m_transmitter->setState(true, s.transmitting ? QStringLiteral("ON AIR")
                                                                     : QStringLiteral("STANDBY")));

    m_canvas->to = showRadioInput ? static_cast<QWidget*>(m_radioInput) : m_txEq;

    for (ChainCard* card : dirty) {
        restyle(card);
    }
    m_canvas->update();
}

} // namespace AetherSDR
