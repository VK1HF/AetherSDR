// VoiceKeyerLoop: repeat a voice-keyer slot on air with a pause, and give way
// to the operator at once. A fake keyer; timings scaled to milliseconds.
// Run: ./build/voice_keyer_loop_test

#include "models/VoiceKeyer.h"
#include "models/VoiceKeyerLoop.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>

#include <cstdio>
#include <string>

using namespace AetherSDR;

namespace {

int g_failed = 0;

void report(const char* name, bool ok, const std::string& detail = {})
{
    std::printf("%s %-58s %s\n", ok ? "[ OK ]" : "[FAIL]", name, detail.c_str());
    if (!ok) ++g_failed;
}

void spin(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

class FakeKeyer : public VoiceKeyer {
public:
    QStringList calls;
    Status st{Idle};
    int active{-1};
    QVector<VoiceKeyerRecording> recs{{1, QStringLiteral("CQ"), 200},
                                      {2, QStringLiteral("Call"), 200},
                                      {3, QStringLiteral("Empty"), 0}};

    Status status() const override { return st; }
    int activeId() const override { return active; }
    const QVector<VoiceKeyerRecording>& recordings() const override { return recs; }
    void recStart(int) override {}
    void recStop(int) override {}
    void previewStart(int) override {}
    void previewStop(int) override {}
    void playbackStart(int id) override { calls << QString("start %1").arg(id); }
    void playbackStop(int id) override { calls << QString("stop %1").arg(id); }
    void clear(int) override {}
    void remove(int) override {}
    void setName(int, const QString&) override {}
    void importWav(int, const QString&) override {}
    void exportWav(int, const QString&) override {}
    bool canTransferWav() const override { return false; }
    bool isTransferring() const override { return false; }
    int maxRecordingMs() const override { return 10'000; }
    int maxNameBytes() const override { return 61; }
    QString sourceLabel() const override { return QStringLiteral("Radio"); }

    void push(Status s, int id)
    {
        st = s;
        active = id;
        emit statusChanged(s, id);
    }
};

// 1 "second" of gap = 10 ms, so the 3 s minimum pause is 30 ms.
const VoiceKeyerLoop::Timing kFast{10, 60, 100};

std::string joined(const QStringList& l) { return l.join(" | ").toStdString(); }

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    {
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        report("an empty slot does not loop", !loop.start(3) && !loop.isActive()
               && k.calls.isEmpty());
        loop.setGapSeconds(1);
        report("pause clamps to 3 s minimum", loop.gapSeconds() == 3);
        loop.setGapSeconds(99);
        report("pause clamps to 20 s maximum", loop.gapSeconds() == 20);
        report("default pause is 6 s", VoiceKeyerLoop::kDefaultGapSeconds == 6);
    }

    {
        // The full cycle: play, finish, pause, play again.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.setGapSeconds(3);
        const bool started = loop.start(1);
        report("start plays the slot", started && k.calls == QStringList{"start 1"}
               && loop.phase() == VoiceKeyerLoop::Phase::Starting, joined(k.calls));
        loop.onTransmittingChanged(true);   // the keyer's own key-down
        k.push(VoiceKeyer::Playback, 1);
        report("keyer playing → loop playing", loop.phase() == VoiceKeyerLoop::Phase::Playing);
        spin(180);
        k.push(VoiceKeyer::Idle, -1);
        report("finished play waits for TX to drop",
               loop.phase() == VoiceKeyerLoop::Phase::AwaitUnkey);
        loop.onTransmittingChanged(false);
        report("TX dropped → pause", loop.phase() == VoiceKeyerLoop::Phase::Waiting
               && loop.msUntilNextPlay() > 0);
        spin(15);
        report("no replay before the pause ends", k.calls.size() == 1, joined(k.calls));
        spin(40);
        report("replays after the pause", k.calls == QStringList({"start 1", "start 1"})
               && loop.phase() == VoiceKeyerLoop::Phase::Starting, joined(k.calls));
        loop.stop();
        report("stop mid-start stops the keyer", k.calls.last() == "stop 1" && !loop.isActive());
    }

    {
        // Operator keys up during the pause.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        QString why;
        QObject::connect(&loop, &VoiceKeyerLoop::interrupted, [&](const QString& r) { why = r; });
        loop.start(1);
        k.push(VoiceKeyer::Playback, 1);
        spin(180);
        k.push(VoiceKeyer::Idle, -1);
        report("finished play with TX already down → pause",
               loop.phase() == VoiceKeyerLoop::Phase::Waiting);
        loop.onTransmittingChanged(true);
        spin(60);
        report("TX during the pause ends the loop", !loop.isActive() && !why.isEmpty()
               && k.calls == QStringList{"start 1"}, joined(k.calls));
    }

    {
        // TUNE mid-play stops it straight away.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.start(1);
        k.push(VoiceKeyer::Playback, 1);
        loop.onTuneChanged(true);
        report("TUNE mid-play stops the play and the loop",
               !loop.isActive() && k.calls.last() == "stop 1", joined(k.calls));
    }

    {
        // TUNE during the pause.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.start(1);
        k.push(VoiceKeyer::Playback, 1);
        spin(180);
        k.push(VoiceKeyer::Idle, -1);
        loop.onTuneChanged(true);
        spin(60);
        report("TUNE during the pause ends the loop",
               !loop.isActive() && k.calls == QStringList{"start 1"}, joined(k.calls));
    }

    {
        // A play cut off (unkey or abort mid-play) ends the loop.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.start(1);
        k.push(VoiceKeyer::Playback, 1);
        spin(20);
        k.push(VoiceKeyer::Idle, -1);
        spin(60);
        report("a play cut short ends the loop",
               !loop.isActive() && k.calls == QStringList{"start 1"}, joined(k.calls));
    }

    {
        // PTT held over the end of the recording.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.start(1);
        loop.onTransmittingChanged(true);
        k.push(VoiceKeyer::Playback, 1);
        spin(180);
        k.push(VoiceKeyer::Idle, -1);
        spin(100);
        report("TX held past the recording ends the loop",
               !loop.isActive() && k.calls == QStringList{"start 1"}, joined(k.calls));
    }

    {
        // The operator uses the keyer during the pause.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.start(1);
        k.push(VoiceKeyer::Playback, 1);
        spin(180);
        k.push(VoiceKeyer::Idle, -1);
        k.push(VoiceKeyer::Preview, 2);
        report("another keyer operation in the pause ends the loop", !loop.isActive());
    }

    {
        // A refused command.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.start(1);
        emit k.commandFailed(QStringLiteral("playback_start"), 1, 0x50000001u, QStringLiteral("no"));
        report("a refused command ends the loop", !loop.isActive());
    }

    {
        // The keyer never answers the start.
        FakeKeyer k;
        VoiceKeyerLoop loop(&k, kFast);
        loop.start(1);
        spin(150);
        report("an unanswered start times out", !loop.isActive()
               && k.calls.last() == "stop 1", joined(k.calls));
    }

    {
        // LOOP pressed while the slot is already on air.
        FakeKeyer k;
        k.push(VoiceKeyer::Playback, 2);
        VoiceKeyerLoop loop(&k, kFast);
        report("adopts a play already on air", loop.start(2) && k.calls.isEmpty()
               && loop.phase() == VoiceKeyerLoop::Phase::Playing);
        loop.stop();
        report("stop mid-play stops it at once", k.calls == QStringList{"stop 2"}, joined(k.calls));
    }

    {
        // Keyer swapped while looping.
        FakeKeyer a;
        FakeKeyer b;
        VoiceKeyerLoop loop(&a, kFast);
        loop.start(1);
        loop.setKeyer(&b);
        report("a keyer swap ends the loop", !loop.isActive() && a.calls.last() == "stop 1");
    }

    std::printf("%s\n", g_failed ? "FAILED" : "PASSED");
    return g_failed ? 1 : 0;
}
