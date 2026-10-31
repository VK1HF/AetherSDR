// TxLinkPeer: linked windows key together. One window hosts the link, the
// others join it; a key from any window's operator keys all of them, nothing
// echoes back, and a lost link unkeys the windows it was keying.
// Run: ./build/tx_link_test

#include "core/TxLinkPeer.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QObject>
#include <QElapsedTimer>
#include <QString>

#include <cstdio>
#include <memory>
#include <string>

using namespace AetherSDR;

namespace {

int g_failed = 0;

void report(const char* name, bool ok, const std::string& detail = {})
{
    std::printf("%s %-58s %s\n", ok ? "[ OK ]" : "[FAIL]", name, detail.c_str());
    if (!ok) ++g_failed;
}

// Pumps events until the condition holds or the time runs out.
template <typename Pred>
bool waitFor(Pred pred, int ms = 2000)
{
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return pred();
}

void spin(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// One AetherSDR window: a link peer plus a record of the keys it applied to
// its radio.
struct Window {
    std::unique_ptr<TxLinkPeer> peer;
    int applied{0};
    bool keyed{false};

    Window(const QString& name)
        : peer(std::make_unique<TxLinkPeer>(name))
    {
        peer->setKeyHandler([this](bool on) {
            ++applied;
            keyed = on;
        });
    }
};

QString uniqueName(const char* tag)
{
    static int serial = 0;
    return QStringLiteral("aether-txlink-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(++serial) + QLatin1Char('-') + QLatin1String(tag);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // 1. The first window to enable hosts the link; the second joins it.
    {
        const QString name = uniqueName("join");
        Window hub(name), client(name);
        hub.peer->setEnabled(true);
        client.peer->setEnabled(true);
        report("first window hosts, second joins",
               hub.peer->isHub() && !client.peer->isHub() && client.peer->isLinked()
                   && waitFor([&] { return hub.peer->isHub(); }));
    }

    // 2. A key from the hosting window keys the joined window, once.
    {
        const QString name = uniqueName("hub-key");
        Window hub(name), client(name);
        hub.peer->setEnabled(true);
        client.peer->setEnabled(true);
        spin(50);
        hub.peer->onLocalKey(true);
        const bool keyed = waitFor([&] { return client.keyed; });
        report("hub MOX keys the joined window", keyed);
        report("hub never applies its own key", hub.applied == 0);
        hub.peer->onLocalKey(false);
        report("hub MOX off unkeys the joined window",
               waitFor([&] { return !client.keyed; }));
    }

    // 3. A key from a joined window keys the host and the other joiners.
    {
        const QString name = uniqueName("client-key");
        Window hub(name), client(name), other(name);
        hub.peer->setEnabled(true);
        client.peer->setEnabled(true);
        other.peer->setEnabled(true);
        spin(50);
        client.peer->onLocalKey(true);
        report("joined window MOX keys the host and the other joiner",
               waitFor([&] { return hub.keyed && other.keyed; }));
        report("the window that pressed MOX is not keyed twice",
               client.applied == 0);
        client.peer->onLocalKey(false);
        report("joined window MOX off unkeys the group",
               waitFor([&] { return !hub.keyed && !other.keyed; }));
    }

    // 4. Keys the link did not make are not sent: a window with no link, and a
    // window's key that is not on the link, reach nobody.
    {
        const QString name = uniqueName("solo");
        Window solo(name);
        solo.peer->onLocalKey(true);
        spin(20);
        report("a key with the link off is not linked", !solo.peer->isLinked() && solo.applied == 0);
    }

    // 5. Losing the host unkeys the joined windows, and they rejoin.
    {
        const QString name = uniqueName("hub-loss");
        auto hub = std::make_unique<Window>(name);
        Window client(name);
        hub->peer->setEnabled(true);
        client.peer->setEnabled(true);
        spin(50);
        hub->peer->onLocalKey(true);
        waitFor([&] { return client.keyed; });
        hub.reset();
        report("losing the host unkeys the joined window",
               waitFor([&] { return !client.keyed; }));
        report("the joined window takes over hosting",
               waitFor([&] { return client.peer->isHub(); }, 4000));
    }

    // 6. A window that leaves while the group is keyed unkeys the others.
    {
        const QString name = uniqueName("leave");
        Window hub(name), client(name), other(name);
        hub.peer->setEnabled(true);
        client.peer->setEnabled(true);
        other.peer->setEnabled(true);
        spin(50);
        client.peer->onLocalKey(true);
        waitFor([&] { return hub.keyed && other.keyed; });
        client.peer->setEnabled(false);
        report("a leaving keyed window unkeys the others",
               waitFor([&] { return !hub.keyed && !other.keyed; }));
    }

    // 7. Turning the link off while keyed unkeys at once.
    {
        const QString name = uniqueName("disable");
        Window hub(name), client(name);
        hub.peer->setEnabled(true);
        client.peer->setEnabled(true);
        spin(50);
        client.peer->onLocalKey(true);
        waitFor([&] { return hub.keyed; });
        hub.peer->setEnabled(false);
        report("disabling the link unkeys the group at once",
               !hub.keyed && waitFor([&] { return !client.keyed; }));
    }

    // 8. A slice copy from a joined window reaches the host and the other
    // joiner, and is not applied as a key.
    {
        const QString name = uniqueName("copy");
        Window hub(name), client(name), other(name);
        hub.peer->setEnabled(true);
        client.peer->setEnabled(true);
        other.peer->setEnabled(true);
        spin(50);
        QString hubSlice, otherSlice;
        QObject::connect(hub.peer.get(), &TxLinkPeer::copyReceived, [&](const QJsonObject& p) {
            hubSlice = p.value(QStringLiteral("slice")).toString();
        });
        QObject::connect(other.peer.get(), &TxLinkPeer::copyReceived, [&](const QJsonObject& p) {
            otherSlice = p.value(QStringLiteral("slice")).toString();
        });
        QJsonObject payload;
        payload.insert(QStringLiteral("slice"), QStringLiteral("B"));
        payload.insert(QStringLiteral("freq"), 7.1114);
        report("a copy is sent from a linked window", client.peer->sendCopy(payload));
        report("the host receives the copy",
               waitFor([&] { return hubSlice == QLatin1String("B"); }));
        report("the other joiner receives the copy",
               waitFor([&] { return otherSlice == QLatin1String("B"); }));
        report("a copy does not key any window", !hub.keyed && !other.keyed && !client.keyed);
    }

    // 9. A loop message and a transmit state from a joined window reach the
    // host and the other joiner, and key nothing.
    {
        const QString name = uniqueName("dvk");
        Window hub(name), client(name), other(name);
        hub.peer->setEnabled(true);
        client.peer->setEnabled(true);
        other.peer->setEnabled(true);
        spin(50);
        int hubPlays = 0;
        int otherPlays = 0;
        bool hubSawTx = false;
        bool otherSawTx = false;
        QObject::connect(hub.peer.get(), &TxLinkPeer::dvkReceived, [&](const QJsonObject& p) {
            if (p.contains(QStringLiteral("play"))) ++hubPlays;
        });
        QObject::connect(other.peer.get(), &TxLinkPeer::dvkReceived, [&](const QJsonObject& p) {
            if (p.contains(QStringLiteral("play"))) ++otherPlays;
        });
        QObject::connect(hub.peer.get(), &TxLinkPeer::remoteTxChanged, [&](bool on) { hubSawTx = on; });
        QObject::connect(other.peer.get(), &TxLinkPeer::remoteTxChanged, [&](bool on) { otherSawTx = on; });
        report("a loop play is sent from a linked window",
               client.peer->sendDvk(QJsonObject{{QStringLiteral("play"), 2}}));
        report("the host receives the loop play",
               waitFor([&] { return hubPlays == 1; }));
        report("the other joiner receives the loop play",
               waitFor([&] { return otherPlays == 1; }));
        client.peer->sendTx(true);
        report("a transmit state reaches the host and the other joiner",
               waitFor([&] { return hubSawTx && otherSawTx; }));
        report("a loop play or transmit state keys nothing",
               !hub.keyed && !other.keyed && !client.keyed);
    }

    if (g_failed == 0)
        std::printf("\nAll TX link checks passed.\n");
    else
        std::printf("\n%d TX link check(s) FAILED.\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
