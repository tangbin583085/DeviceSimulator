#include <DeviceSimulator/DemoBinaryProtocolCodec.h>
#include <DeviceSimulator/Rules.h>

#include <QtTest>

#include <algorithm>

using namespace DeviceSimulator;

class ProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void encodeAndDecodeRoundTrip()
    {
        DemoBinaryProtocolCodec codec;
        const QByteArray encoded = codec.encode({1, 0x04, 0x1234, QByteArray::fromHex("1020")});

        const ProtocolDecodeResult result = codec.append(encoded);
        QCOMPARE(result.requests.size(), 1);
        QCOMPARE(result.requests.first().version, quint8(1));
        QCOMPARE(result.requests.first().command, quint8(0x04));
        QCOMPARE(result.requests.first().sequence, quint16(0x1234));
        QCOMPARE(result.requests.first().payload, QByteArray::fromHex("1020"));
        QCOMPARE(result.requests.first().rawData, encoded);
    }

    void usesDocumentedByteOrderAndCrc()
    {
        DemoBinaryProtocolCodec codec;
        const QByteArray encoded = codec.encode({1, 0x01, 0x1234, QByteArray::fromHex("AABB")});

        QCOMPARE(static_cast<quint8>(encoded.at(4)), quint8(0x12));
        QCOMPARE(static_cast<quint8>(encoded.at(5)), quint8(0x34));
        QCOMPARE(static_cast<quint8>(encoded.at(6)), quint8(0x00));
        QCOMPARE(static_cast<quint8>(encoded.at(7)), quint8(0x02));
        QCOMPARE(static_cast<quint8>(encoded.at(encoded.size() - 2)), quint8(0xBC));
        QCOMPARE(static_cast<quint8>(encoded.at(encoded.size() - 1)), quint8(0xA2));
    }

    void acceptsFragmentedAndCoalescedInput()
    {
        DemoBinaryProtocolCodec encoder;
        const QByteArray first = encoder.encode({1, 0x01, 1, {}});
        const QByteArray second = encoder.encode({1, 0x02, 2, QByteArray::fromHex("44")});

        DemoBinaryProtocolCodec codec;
        const ProtocolDecodeResult partial = codec.append(QByteArrayView(first).first(5));
        QVERIFY(partial.requests.isEmpty());

        QByteArray remaining = first.mid(5);
        remaining.append(second);
        const ProtocolDecodeResult completed = codec.append(remaining);
        QCOMPARE(completed.requests.size(), 2);
        QCOMPARE(completed.requests.at(0).sequence, quint16(1));
        QCOMPARE(completed.requests.at(1).sequence, quint16(2));
    }

    void recoversAfterNoiseAndBadChecksum()
    {
        DemoBinaryProtocolCodec codec;
        QByteArray bad = codec.encode({1, 0x01, 1, QByteArray::fromHex("10")});
        bad[bad.size() - 1] = static_cast<char>(bad.at(bad.size() - 1) ^ 0x01);
        const QByteArray good = codec.encode({1, 0x02, 2, QByteArray::fromHex("20")});

        QByteArray input = QByteArray::fromHex("001122");
        input.append(bad);
        input.append(good);
        const ProtocolDecodeResult result = codec.append(input);

        QCOMPARE(result.requests.size(), 1);
        QCOMPARE(result.requests.first().command, quint8(0x02));
        QVERIFY(std::any_of(
            result.diagnostics.cbegin(),
            result.diagnostics.cend(),
            [](const ProtocolDiagnostic &item) { return item.code == QStringLiteral("ChecksumMismatch"); }));
    }

    void ruleMatchesCommandAndExtraCondition()
    {
        const SimulationRule rule(
            0x01,
            [](const ProtocolRequest &request) {
                return ResponsePlan::respond({request.version, 0x81, request.sequence, {}});
            },
            [](const ProtocolRequest &request) { return request.payload == QByteArrayLiteral("ok"); });

        QVERIFY(rule.matches({1, 0x01, 1, QByteArrayLiteral("ok"), {}}));
        QVERIFY(!rule.matches({1, 0x01, 1, QByteArrayLiteral("no"), {}}));
        QCOMPARE(rule.createResponse({1, 0x01, 7, QByteArrayLiteral("ok"), {}})
                     .response->command,
            quint8(0x81));
    }
};

QTEST_APPLESS_MAIN(ProtocolTests)

#include "test_protocol.moc"
