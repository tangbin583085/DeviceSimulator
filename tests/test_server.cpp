#include <DeviceSimulator/DemoBinaryProtocolCodec.h>
#include <DeviceSimulator/DeviceSimulatorServer.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest>

#include <memory>
#include <utility>

using namespace DeviceSimulator;

class ServerTests final : public QObject
{
    Q_OBJECT

private:
    static std::unique_ptr<DeviceSimulatorServer> createServer()
    {
        DeviceSimulatorOptions options;
        options.listenAddress = QHostAddress::LocalHost;
        options.port = 0;

        QList<SimulationRule> rules;
        rules.append(SimulationRule(
            0x01,
            [](const ProtocolRequest &request) {
                return ResponsePlan::respond({
                    request.version,
                    0x81,
                    request.sequence,
                    QByteArray::fromHex("010203"),
                });
            }));

        return std::make_unique<DeviceSimulatorServer>(
            options,
            []() { return std::make_unique<DemoBinaryProtocolCodec>(); },
            std::move(rules));
    }

    static ProtocolRequest readOneFrame(QTcpSocket &socket)
    {
        DemoBinaryProtocolCodec codec;
        QElapsedTimer timeout;
        timeout.start();
        while (timeout.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            const ProtocolDecodeResult result = codec.append(socket.readAll());
            if (!result.requests.isEmpty()) {
                return result.requests.first();
            }
            QTest::qWait(10);
        }
        return {};
    }

    static void connectClient(QTcpSocket &socket, quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY2(socket.waitForConnected(3000), qPrintable(socket.errorString()));
        QCoreApplication::processEvents();
    }

private slots:
    void clientReceivesConfiguredResponse()
    {
        auto server = createServer();
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));
        QVERIFY(server->serverPort() != 0);

        QTcpSocket client;
        connectClient(client, server->serverPort());

        DemoBinaryProtocolCodec codec;
        const QByteArray request = codec.encode({1, 0x01, 0x1234, QByteArray::fromHex("10")});
        QCOMPARE(client.write(request), qint64(request.size()));
        client.flush();

        const ProtocolRequest response = readOneFrame(client);
        QCOMPARE(response.command, quint8(0x81));
        QCOMPARE(response.sequence, quint16(0x1234));
        QCOMPARE(response.payload, QByteArray::fromHex("010203"));
    }

    void fragmentedRequestIsParsedAfterRemainingBytesArrive()
    {
        auto server = createServer();
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));

        QTcpSocket client;
        connectClient(client, server->serverPort());
        DemoBinaryProtocolCodec codec;
        const QByteArray request = codec.encode({1, 0x01, 9, QByteArray::fromHex("2233")});

        QCOMPARE(client.write(request.left(4)), qint64(4));
        client.flush();
        QTest::qWait(20);
        QCOMPARE(client.write(request.mid(4)), qint64(request.size() - 4));
        client.flush();

        const ProtocolRequest response = readOneFrame(client);
        QCOMPARE(response.command, quint8(0x81));
        QCOMPARE(response.sequence, quint16(9));
    }

    void unknownCommandGetsDefaultErrorResponse()
    {
        auto server = createServer();
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));

        QTcpSocket client;
        connectClient(client, server->serverPort());
        DemoBinaryProtocolCodec codec;
        const QByteArray request = codec.encode({1, 0x44, 7, {}});
        client.write(request);
        client.waitForBytesWritten(3000);

        const ProtocolRequest response = readOneFrame(client);
        QCOMPARE(response.command, quint8(0xFF));
        QCOMPARE(response.sequence, quint16(7));
        QCOMPARE(response.payload, QByteArray::fromHex("0144"));
    }

    void acceptsAnotherClientAfterDisconnect()
    {
        auto server = createServer();
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));

        {
            QTcpSocket first;
            connectClient(first, server->serverPort());
            first.disconnectFromHost();
            QVERIFY(first.waitForDisconnected(3000)
                || first.state() == QAbstractSocket::UnconnectedState);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!server->hasActiveClient(), 3000);

        QTcpSocket second;
        connectClient(second, server->serverPort());
        DemoBinaryProtocolCodec codec;
        const QByteArray request = codec.encode({1, 0x01, 10, {}});
        second.write(request);
        second.waitForBytesWritten(3000);
        QCOMPARE(readOneFrame(second).sequence, quint16(10));
    }

    void stopIsIdempotent()
    {
        auto server = createServer();
        QString error;
        QVERIFY2(server->start(&error), qPrintable(error));
        server->stop();
        server->stop();
        QVERIFY(!server->isRunning());
    }
};

QTEST_MAIN(ServerTests)

#include "test_server.moc"
