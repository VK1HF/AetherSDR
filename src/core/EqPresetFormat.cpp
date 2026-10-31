#include "core/EqPresetFile.h"

#include <QDateTime>
#include <QJsonValue>
#include <QStandardPaths>

namespace AetherSDR {

namespace {

constexpr int kFileVersion = 1;

const char* chainName(EqPresetFile::Chain chain)
{
    return chain == EqPresetFile::Chain::Rx ? "rx" : "tx";
}

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

QString EqPresetFile::defaultPath(Chain chain)
{
    const QString name = chain == Chain::Rx ? QStringLiteral("RxEqualizer.json")
                                            : QStringLiteral("TxEqualizer.json");
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/AetherSDR/") + name;
}

QJsonObject EqPresetFile::buildDocument(Chain chain, const QJsonObject& eq)
{
    QJsonObject doc;
    doc.insert(QStringLiteral("version"), kFileVersion);
    doc.insert(QStringLiteral("chain"), QLatin1String(chainName(chain)));
    doc.insert(QStringLiteral("createdBy"), QStringLiteral("AetherSDR"));
    doc.insert(QStringLiteral("createdAt"),
               QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    doc.insert(QStringLiteral("eq"), eq);
    return doc;
}

bool EqPresetFile::parseDocument(const QJsonObject& doc, Chain chain,
                                 QJsonObject* eq, QString* error)
{
    if (doc.value(QStringLiteral("version")).toInt() != kFileVersion) {
        setError(error, QStringLiteral("This is not an equalizer file this version can read."));
        return false;
    }
    const QString fileChain = doc.value(QStringLiteral("chain")).toString();
    if (fileChain != QLatin1String(chainName(chain))) {
        setError(error, QStringLiteral("This file is for the %1 equalizer, not the %2 equalizer.")
                            .arg(fileChain.isEmpty() ? QStringLiteral("unknown")
                                                     : fileChain.toUpper(),
                                 QLatin1String(chainLabel(chain))));
        return false;
    }
    const QJsonValue block = doc.value(QStringLiteral("eq"));
    if (!block.isObject()) {
        setError(error, QStringLiteral("The file has no equalizer settings in it."));
        return false;
    }
    if (eq)
        *eq = block.toObject();
    return true;
}

} // namespace AetherSDR
