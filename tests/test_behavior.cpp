#include <DeviceSimulator/DemoBinaryProtocolCodec.h>
#include <DeviceSimulator/DeviceSimulatorServer.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest>

#include <memory>
#include <optional>
#include <utility>

using namespace DeviceSimulator;

class BehaviorTests final : public QObject
{
    Q_OBJECT

private:
    static std::unique_ptr<DeviceSimulatorServer> createServer(
        SimulationRule::Handler handler,
        std::optional<TelemetryOptions> telemetry = std::nullopt)
    {
        DeviceSimulatorOptions options;
        options.port = 0;
        options.randomSeed = 20260804;
        QList<SimulationRule> rules;
        if (handler) {
            rules.append(SimulationRule(0x01, std::move(handler)));
        }
        return std::make_unique<DeviceSimulatorServer>(
            options,
            []() { return std::make_unique<DemoBinaryProtocolCodec>(); },
            std::move(rules),
            DeviceSimulatorServer::UnknownCommandHandler{},
            std::move(telemetry));
    }

    static void connectClient(QTcpSocket &socket, quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY2(socket.waitForConnected(3000), qPrintable(socket.errorString()));
        QCoreApplication::processEvents();
    }

    static void sendRequest(QTcpSocket &socket, quint16 sequence = 1)
    {
        DemoBinaryProtocolCodec codec;
        const QByteArray request = codec.encode({1, 0x01, sequence, {}});
        QCOMPARE(socket.write(request), qint64(request.size()));
        socket.flush();
    }

    static QList<ProtocolRequest> readFrames(
        QTcpSocket &socket,
        int expectedCount,
        int timeoutMs = 3000)
    {
        DemoBinaryProtocolCodec codec;
        QList<ProtocolRequest> frames;
        QElapsedTimer timeout;
        timeout.start();
        while (frames.size() < expectedCount && timeout.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            const ProtocolDecodeResult result = codec.append(socket.readAll());
            frames.append(result.requests);
            QTest::qWait(5);
        }
        return frames;
    }

    static bool hasEvent(const QSignalSpy &spy, const QString &eventType)
    {
        for (const QList<QVariant> &arguments : spy) {
            if (!arguments.isEmpty() && arguments.first().toString() == eventType) {
                return true;
            }
        }
        return false;
    }

private slots:
    void probabilityOneDropsResponse()
    {
        FaultInjectionOptions faults;
        faults.dropProbability = 1.0;
        auto server = createServer([faults](const ProtocolRequest &request) {
            return ResponsePlan::respond({request.version, 0x81, request.sequence, {}}, faults);
        });
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));
        QSignalSpy eventSpy(server.get(), &DeviceSimulatorServer::eventOccurred);

        QTcpSocket client;
        connectClient(client, server->serverPort());
        sendRequest(client);
        QTest::qWait(120);
        QCoreApplication::processEvents();

        QCOMPARE(client.bytesAvailable(), qint64(0));
        QVERIFY(hasEvent(eventSpy, QStringLiteral("ResponseDropped")));
    }

    void noiseAndFragmentationStillProduceAFrame()
    {
        FaultInjectionOptions faults;
        faults.noisePrefix = QByteArray::fromHex("DEAD");
        faults.fragmentSizes = {3, 2};
        faults.fragmentDelayMs = 10;
        auto server = createServer([faults](const ProtocolRequest &request) {
            return ResponsePlan::respond(
                {request.version, 0x81, request.sequence, QByteArray::fromHex("010203")}, faults);
        });
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));
        QSignalSpy eventSpy(server.get(), &DeviceSimulatorServer::eventOccurred);

        QTcpSocket client;
        connectClient(client, server->serverPort());
        sendRequest(client, 7);

        const QList<ProtocolRequest> frames = readFrames(client, 1);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().sequence, quint16(7));
        QVERIFY(hasEvent(eventSpy, QStringLiteral("NoiseInserted")));
        QVERIFY(hasEvent(eventSpy, QStringLiteral("ResponseFragmented")));
    }

    void coalescedResponsesAreReported()
    {
        FaultInjectionOptions faults;
        faults.coalesceCount = 2;
        faults.coalesceWindowMs = 200;
        auto server = createServer([faults](const ProtocolRequest &request) {
            return ResponsePlan::respond({request.version, 0x81, request.sequence, {}}, faults);
        });
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));
        QSignalSpy eventSpy(server.get(), &DeviceSimulatorServer::eventOccurred);

        QTcpSocket client;
        connectClient(client, server->serverPort());
        DemoBinaryProtocolCodec codec;
        QByteArray requests = codec.encode({1, 0x01, 8, {}});
        requests.append(codec.encode({1, 0x01, 9, {}}));
        QCOMPARE(client.write(requests), qint64(requests.size()));
        client.flush();

        const QList<ProtocolRequest> frames = readFrames(client, 2);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames.at(0).sequence, quint16(8));
        QCOMPARE(frames.at(1).sequence, quint16(9));
        QVERIFY(hasEvent(eventSpy, QStringLiteral("ResponsesCoalesced")));
    }

    void telemetryStartsAtSequenceOne()
    {
        TelemetryOptions telemetry;
        telemetry.intervalMs = 60;
        telemetry.frameFactory = [](quint16 sequence) {
            return ProtocolResponse{1, 0x90, sequence, QByteArray::fromHex("00FA")};
        };
        auto server = createServer(SimulationRule::Handler{}, telemetry);
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));

        QTcpSocket client;
        connectClient(client, server->serverPort());
        const QList<ProtocolRequest> frames = readFrames(client, 2);

        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames.at(0).command, quint8(0x90));
        QCOMPARE(frames.at(0).sequence, quint16(1));
        QCOMPARE(frames.at(1).sequence, quint16(2));
    }

    void disconnectAfterSendClosesConnection()
    {
        FaultInjectionOptions faults;
        faults.disconnectAfterSend = true;
        auto server = createServer([faults](const ProtocolRequest &request) {
            return ResponsePlan::respond({request.version, 0x81, request.sequence, {}}, faults);
        });
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));

        QTcpSocket client;
        connectClient(client, server->serverPort());
        sendRequest(client, 11);
        QCOMPARE(readFrames(client, 1).size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(
            client.state() == QAbstractSocket::UnconnectedState,
            3000);
    }
};

QTEST_MAIN(BehaviorTests)

#include "test_behavior.moc"
