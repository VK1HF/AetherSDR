// Which SmartLink sign-in failures remove a kept sign-in. Only the
// server refusing it does; no network, a timeout or server trouble keep it
// (MobileEngine then tries again).

#include "core/backends/flex/SmartLinkClient.h"

#include <QTest>

using AetherSDR::SmartLinkClient;

class SmartLinkRefusalTest : public QObject {
    Q_OBJECT
private slots:
    void refusals()
    {
        QVERIFY(SmartLinkClient::isRefusalStatus(400));  // invalid_grant
        QVERIFY(SmartLinkClient::isRefusalStatus(401));
        QVERIFY(SmartLinkClient::isRefusalStatus(403));  // Auth0's wrong password / revoked token
    }
    void notReached()
    {
        QVERIFY(!SmartLinkClient::isRefusalStatus(0));    // no network, DNS, TLS, aborted
        QVERIFY(!SmartLinkClient::isRefusalStatus(408));  // timeout
        QVERIFY(!SmartLinkClient::isRefusalStatus(429));  // rate limited
        QVERIFY(!SmartLinkClient::isRefusalStatus(500));
        QVERIFY(!SmartLinkClient::isRefusalStatus(502));
        QVERIFY(!SmartLinkClient::isRefusalStatus(503));
        QVERIFY(!SmartLinkClient::isRefusalStatus(200));
    }
};

QTEST_GUILESS_MAIN(SmartLinkRefusalTest)
#include "smartlink_refusal_test.moc"
