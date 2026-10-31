// EqPresetFile: the EQ-only file format. An RX equalizer file must not load
// into the TX equalizer, and the reverse; the EQ block must round-trip intact.
// Run: ./build/eq_preset_file_test

#include "core/EqPresetFile.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

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

QJsonObject sampleEq()
{
    QJsonObject eq;
    eq.insert(QStringLiteral("enabled"), true);
    eq.insert(QStringLiteral("masterGain"), 1.5);
    eq.insert(QStringLiteral("filterFamily"), QStringLiteral("Butterworth"));
    eq.insert(QStringLiteral("activeBandCount"), 1);
    QJsonObject band;
    band.insert(QStringLiteral("freqHz"), 1000.0);
    band.insert(QStringLiteral("gainDb"), -3.0);
    QJsonArray bands;
    bands.append(band);
    eq.insert(QStringLiteral("bands"), bands);
    return eq;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QJsonObject eq = sampleEq();

    // 1. A built document names its chain and keeps the EQ block as it was.
    const QJsonObject rxDoc = EqPresetFile::buildDocument(EqPresetFile::Chain::Rx, eq);
    report("an RX document is marked rx", rxDoc.value("chain").toString() == "rx");
    report("the EQ block is written unchanged", rxDoc.value("eq").toObject() == eq);

    // 2. The same chain reads back.
    QJsonObject back;
    QString error;
    const bool rxOk = EqPresetFile::parseDocument(rxDoc, EqPresetFile::Chain::Rx, &back, &error);
    report("an RX file loads into the RX equalizer", rxOk, error.toStdString());
    report("the EQ block reads back equal", back == eq);

    // 3. Each chain refuses the other's file.
    QJsonObject txDoc = EqPresetFile::buildDocument(EqPresetFile::Chain::Tx, eq);
    error.clear();
    report("a TX file is refused by the RX equalizer",
           !EqPresetFile::parseDocument(txDoc, EqPresetFile::Chain::Rx, nullptr, &error)
               && error.contains("TX"));
    report("an RX file is refused by the TX equalizer",
           !EqPresetFile::parseDocument(rxDoc, EqPresetFile::Chain::Tx, nullptr, &error));

    // 4. A file with no EQ block, or from a future version, is refused.
    QJsonObject noEq = rxDoc;
    noEq.remove("eq");
    report("a file with no EQ settings is refused",
           !EqPresetFile::parseDocument(noEq, EqPresetFile::Chain::Rx, nullptr, nullptr));
    QJsonObject future = rxDoc;
    future.insert("version", 99);
    report("a file from a later version is refused",
           !EqPresetFile::parseDocument(future, EqPresetFile::Chain::Rx, nullptr, nullptr));

    if (g_failed == 0)
        std::printf("\nAll EQ preset file checks passed.\n");
    else
        std::printf("\n%d EQ preset file check(s) FAILED.\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
