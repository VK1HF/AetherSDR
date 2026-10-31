#pragma once

#include <QJsonObject>
#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <functional>

class QLocalServer;
class QLocalSocket;
class QTimer;

namespace AetherSDR {

// Keys several AetherSDR windows together, so that one operator's MOX keys
// every linked radio. Windows on one Mac find each other through a local
// socket. The first window to start hosts it and relays for the rest; if
// the host closes, the others drop their keying and one of them takes over.
//
// Only voice MOX is linked. A key from TUNE, a voice keyer or CW is local to
// its window, because a second radio would carry no audio for it. A window
// unkeys at once when the link is lost, so a half-linked pair never keeps
// transmitting.
class TxLinkPeer : public QObject {
    Q_OBJECT

public:
    // Applies a key from another window to this window's radio. Called with
    // the link's own echo suppressed, so it never sends the key back.
    using KeyHandler = std::function<void(bool on)>;

    explicit TxLinkPeer(QString serverName = QStringLiteral("AetherSDR-TxLink"),
                        QObject* parent = nullptr);
    ~TxLinkPeer() override;

    void setKeyHandler(KeyHandler handler);

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }
    bool isLinked() const { return m_hub || m_upstream; }
    bool isHub() const { return m_hub != nullptr; }
    // Other linked windows this one can reach: the joined windows on a host,
    // and at least the host on a joiner.
    int peerCount() const;

    // Feed every MOX key this window's operator was accepted for. Keys that
    // came from the link itself are ignored here.
    void onLocalKey(bool on);

    // Sends a slice-copy payload to every linked window. It is not applied
    // here. Returns false when this window is not linked.
    bool sendCopy(const QJsonObject& payload);
    // Sends a DVK loop message or a transmit state to the linked windows, so
    // a loop started in one window plays in all of them and any key stops it.
    // Returns false when this window is not linked.
    bool sendDvk(const QJsonObject& payload);
    bool sendTx(bool on);

    // Set by the link while it applies a key, so the echo is not repeated.
    bool isApplyingRemote() const { return m_applying; }

signals:
    void linkChanged(bool linked);
    // Fires when the number of other linked windows changes.
    void peersChanged(int count);
    // A slice-copy payload from another linked window.
    void copyReceived(const QJsonObject& payload);
    // A DVK loop message from another linked window.
    void dvkReceived(const QJsonObject& payload);
    // Another linked window keyed or unkeyed, by any means.
    void remoteTxChanged(bool on);

private:
    void joinOrHost();
    void startHosting();
    void joinAsClient(QLocalSocket* socket);
    void scheduleRejoin();
    void dropLink();
    void emitPeers();

    void onNewClient();
    void onClientData(QLocalSocket* socket);
    void onClientGone(QLocalSocket* socket);
    void onUpstreamData();
    void onUpstreamGone();

    void handleLine(const QByteArray& line, QLocalSocket* from);
    void broadcast(const QByteArray& line, QLocalSocket* except);
    void applyRemote(bool on);

    QString m_serverName;
    QString m_id;
    KeyHandler m_keyHandler;

    bool m_enabled{false};
    bool m_applying{false};
    // True while this window's radio is keyed as part of the link, whichever
    // window's operator pressed MOX. Unkeyed when the link is lost.
    bool m_linkKeyed{false};
    // Hub only: the key state the linked windows were last told.
    bool m_groupKeyed{false};

    QLocalServer* m_hub{nullptr};
    QList<QLocalSocket*> m_clients;
    QLocalSocket* m_upstream{nullptr};
    QHash<QLocalSocket*, QByteArray> m_buffers;
    QTimer* m_rejoinTimer{nullptr};
};

} // namespace AetherSDR
