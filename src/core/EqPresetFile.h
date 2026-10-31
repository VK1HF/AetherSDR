#pragma once

#include <QJsonObject>
#include <QString>

namespace AetherSDR {

class AudioEngine;

// Saves and loads one chain's equalizer by itself, as a JSON file. The RX and
// TX equalizers each have a file of their own, and a file made for one chain
// is refused when loaded into the other. Only the EQ block is read or written;
// the rest of the chain is left as it is.
//
// File format:
//   {
//     "version": 1,
//     "chain": "rx" | "tx",
//     "createdBy": "AetherSDR",
//     "createdAt": "ISO-8601",
//     "eq": { "enabled", "masterGain", "filterFamily", "activeBandCount", "bands": [...] }
//   }
class EqPresetFile {
public:
    enum class Chain { Rx, Tx };

    static QString defaultPath(Chain chain);

    // Builds the file contents from one chain's EQ block (the "eq" object).
    static QJsonObject buildDocument(Chain chain, const QJsonObject& eq);
    // Checks a parsed file for this chain and returns its EQ block.
    static bool parseDocument(const QJsonObject& doc, Chain chain,
                              QJsonObject* eq, QString* error);

    static bool save(AudioEngine* engine, Chain chain, const QString& path,
                     QString* error = nullptr);
    static bool load(AudioEngine* engine, Chain chain, const QString& path,
                     QString* error = nullptr);
};

} // namespace AetherSDR
