#include "core/EqPresetFile.h"

#include "core/ChannelStripPresets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace AetherSDR {

namespace {

const char* chainLabel(EqPresetFile::Chain chain)
{
    return chain == EqPresetFile::Chain::Rx ? "RX" : "TX";
}

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

}  // namespace

bool EqPresetFile::save(AudioEngine* engine, Chain chain, const QString& path, QString* error)
{
    if (!engine) {
        setError(error, QStringLiteral("The audio engine is not available."));
        return false;
    }
    const QJsonObject captured = chain == Chain::Rx
        ? ChannelStripPresets::captureRxJson(engine)
        : ChannelStripPresets::captureTxJson(engine);
    const QJsonValue eq = captured.value(QStringLiteral("eq"));
    if (!eq.isObject()) {
        setError(error, QStringLiteral("The %1 equalizer is not available.").arg(QLatin1String(chainLabel(chain))));
        return false;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray json = QJsonDocument(buildDocument(chain, eq.toObject()))
                                .toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size()) {
        setError(error, QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool EqPresetFile::load(AudioEngine* engine, Chain chain, const QString& path, QString* error)
{
    if (!engine) {
        setError(error, QStringLiteral("The audio engine is not available."));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not read %1: %2").arg(path, file.errorString()));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parsed.isNull() || !parsed.isObject()) {
        setError(error, QStringLiteral("%1 is not a valid equalizer file.").arg(path));
        return false;
    }

    QJsonObject eq;
    if (!parseDocument(parsed.object(), chain, &eq, error))
        return false;

    // Apply only the EQ block; the apply helpers touch just the modules present.
    QJsonObject block;
    block.insert(QStringLiteral("eq"), eq);
    if (chain == Chain::Rx)
        ChannelStripPresets::applyRxJson(engine, block);
    else
        ChannelStripPresets::applyTxJson(engine, block);
    return true;
}

} // namespace AetherSDR
