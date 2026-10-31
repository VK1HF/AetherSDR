#include "core/TxLinkPeer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace AetherSDR {

Q_LOGGING_CATEGORY(lcTxLink, "aether.txlink")

namespace {

constexpr int kProbeTimeoutMs = 150;
constexpr int kRejoinDelayMs = 1000;

QByteArray keyLine(const QString& from, bool on)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("from"), from);
    obj.insert(QStringLiteral("on"), on);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

}  // namespace

TxLinkPeer::TxLinkPeer(QString serverName, QObject* parent)
    : QObject(parent)
    , m_serverName(std::move(serverName))
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_rejoinTimer = new QTimer(this);
    m_rejoinTimer->setSingleShot(true);
    connect(m_rejoinTimer, &QTimer::timeout, this, &TxLinkPeer::joinOrHost);
}

TxLinkPeer::~TxLinkPeer()
{
    setEnabled(false);
}

void TxLinkPeer::setKeyHandler(KeyHandler handler)
{
    m_keyHandler = std::move(handler);
}

void TxLinkPeer::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (on) {
        joinOrHost();
        return;
    }
    m_rejoinTimer->stop();
    dropLink();
}

void TxLinkPeer::joinOrHost()
{
    if (!m_enabled || m_hub || m_upstream)
        return;
    // A live host answers a probe; if none does, any socket file left by a
    // crashed window is stale and is cleared before this window listens.
    auto* probe = new QLocalSocket(this);
    probe->connectToServer(m_serverName);
    if (probe->waitForConnected(kProbeTimeoutMs)) {
        qCInfo(lcTxLink) << "joined transmit link" << m_serverName;
        joinAsClient(probe);
        return;
    }
    probe->deleteLater();
    startHosting();
}

void TxLinkPeer::startHosting()
{
    QLocalServer::removeServer(m_serverName);
    auto* server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(m_serverName)) {
        qCWarning(lcTxLink) << "cannot host transmit link" << m_serverName
                            << server->errorString();
        server->deleteLater();
        scheduleRejoin();
        return;
    }
    m_hub = server;
    connect(m_hub, &QLocalServer::newConnection, this, &TxLinkPeer::onNewClient);
    qCInfo(lcTxLink) << "hosting transmit link" << m_serverName;
    emit linkChanged(true);
}

void TxLinkPeer::joinAsClient(QLocalSocket* socket)
{
    m_upstream = socket;
    connect(m_upstream, &QLocalSocket::readyRead, this, &TxLinkPeer::onUpstreamData);
    connect(m_upstream, &QLocalSocket::disconnected, this, &TxLinkPeer::onUpstreamGone);
    emit linkChanged(true);
    emitPeers();
}

int TxLinkPeer::peerCount() const
{
    if (m_hub)
        return m_clients.size();
    return m_upstream ? 1 : 0;
}

void TxLinkPeer::emitPeers()
{
    emit peersChanged(peerCount());
}

void TxLinkPeer::scheduleRejoin()
{
    if (m_enabled && !m_rejoinTimer->isActive())
        m_rejoinTimer->start(kRejoinDelayMs);
}

void TxLinkPeer::dropLink()
{
    // Fail closed: a window keyed through the link unkeys when the link goes.
    const bool wasLinked = isLinked();
    applyRemote(false);
    m_groupKeyed = false;

    for (QLocalSocket* client : std::as_const(m_clients)) {
        client->disconnect(this);
        client->deleteLater();
    }
    m_clients.clear();
    m_buffers.clear();

    if (m_hub) {
        m_hub->close();
        m_hub->deleteLater();
        m_hub = nullptr;
    }
    if (m_upstream) {
        m_upstream->disconnect(this);
        m_upstream->abort();
        m_upstream->deleteLater();
        m_upstream = nullptr;
    }
    if (wasLinked)
        emit linkChanged(false);
    emitPeers();
}

void TxLinkPeer::onNewClient()
{
    while (m_hub && m_hub->hasPendingConnections()) {
        QLocalSocket* client = m_hub->nextPendingConnection();
        m_clients.append(client);
        connect(client, &QLocalSocket::readyRead, this, [this, client] { onClientData(client); });
        connect(client, &QLocalSocket::disconnected, this, [this, client] { onClientGone(client); });
        emitPeers();
    }
}

void TxLinkPeer::onClientData(QLocalSocket* socket)
{
    QByteArray& buffer = m_buffers[socket];
    buffer += socket->readAll();
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        handleLine(line, socket);
    }
}

void TxLinkPeer::onClientGone(QLocalSocket* socket)
{
    m_clients.removeAll(socket);
    m_buffers.remove(socket);
    socket->deleteLater();
    emitPeers();
    // The departed window may have been the one keying. The rest unkey with it.
    if (m_groupKeyed) {
        m_groupKeyed = false;
        broadcast(keyLine(m_id, false), nullptr);
        applyRemote(false);
    }
}

void TxLinkPeer::onUpstreamData()
{
    if (!m_upstream)
        return;
    QByteArray& buffer = m_buffers[m_upstream];
    buffer += m_upstream->readAll();
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        handleLine(line, m_upstream);
    }
}

void TxLinkPeer::onUpstreamGone()
{
    applyRemote(false);
    if (m_upstream) {
        m_upstream->deleteLater();
        m_upstream = nullptr;
    }
    m_buffers.clear();
    emit linkChanged(false);
    emitPeers();
    scheduleRejoin();
}

void TxLinkPeer::handleLine(const QByteArray& line, QLocalSocket* from)
{
    const QJsonObject obj = QJsonDocument::fromJson(line).object();
    if (obj.value(QStringLiteral("from")).toString() == m_id)
        return;
    if (obj.contains(QStringLiteral("copy"))) {
        if (m_hub)
            broadcast(line + '\n', from);
        emit copyReceived(obj.value(QStringLiteral("copy")).toObject());
        return;
    }
    if (obj.contains(QStringLiteral("dvk"))) {
        if (m_hub)
            broadcast(line + '\n', from);
        emit dvkReceived(obj.value(QStringLiteral("dvk")).toObject());
        return;
    }
    if (obj.contains(QStringLiteral("tx"))) {
        if (m_hub)
            broadcast(line + '\n', from);
        emit remoteTxChanged(obj.value(QStringLiteral("tx")).toBool());
        return;
    }
    if (!obj.contains(QStringLiteral("on")))
        return;
    const bool on = obj.value(QStringLiteral("on")).toBool();
    if (m_hub) {
        m_groupKeyed = on;
        // The line arrives without its newline; the relay must carry one.
        broadcast(line + '\n', from);
    }
    applyRemote(on);
}

void TxLinkPeer::broadcast(const QByteArray& line, QLocalSocket* except)
{
    for (QLocalSocket* client : std::as_const(m_clients)) {
        if (client != except)
            client->write(line);
    }
}

void TxLinkPeer::applyRemote(bool on)
{
    if (!on && !m_linkKeyed)
        return;
    m_linkKeyed = on;
    if (!m_keyHandler)
        return;
    m_applying = true;
    m_keyHandler(on);
    m_applying = false;
}

bool TxLinkPeer::sendCopy(const QJsonObject& payload)
{
    if (!isLinked())
        return false;
    QJsonObject obj;
    obj.insert(QStringLiteral("from"), m_id);
    obj.insert(QStringLiteral("copy"), payload);
    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    if (m_hub)
        broadcast(line, nullptr);
    else
        m_upstream->write(line);
    return true;
}

bool TxLinkPeer::sendDvk(const QJsonObject& payload)
{
    if (!isLinked())
        return false;
    QJsonObject obj;
    obj.insert(QStringLiteral("from"), m_id);
    obj.insert(QStringLiteral("dvk"), payload);
    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    if (m_hub)
        broadcast(line, nullptr);
    else
        m_upstream->write(line);
    return true;
}

bool TxLinkPeer::sendTx(bool on)
{
    if (!isLinked())
        return false;
    QJsonObject obj;
    obj.insert(QStringLiteral("from"), m_id);
    obj.insert(QStringLiteral("tx"), on);
    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    if (m_hub)
        broadcast(line, nullptr);
    else
        m_upstream->write(line);
    return true;
}

void TxLinkPeer::onLocalKey(bool on)
{
    if (!m_enabled || m_applying)
        return;
    if (!on) {
        if (!m_linkKeyed)
            return;
        m_linkKeyed = false;
    } else {
        // Already keyed through the link: a confirmation from the radio is not
        // a new key, and sending it on would echo around the group.
        if (!isLinked() || m_linkKeyed)
            return;
        m_linkKeyed = true;
    }
    const QByteArray line = keyLine(m_id, on);
    if (m_hub) {
        m_groupKeyed = on;
        broadcast(line, nullptr);
    } else if (m_upstream) {
        m_upstream->write(line);
    }
}

} // namespace AetherSDR
