#pragma once

#include <DeviceSimulator/Protocol.h>
#include <DeviceSimulator/Rules.h>

#include <QAbstractSocket>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QTcpServer>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <random>

class QTcpSocket;

namespace DeviceSimulator {

struct DeviceSimulatorOptions
{
    QHostAddress listenAddress{QHostAddress::LocalHost};
    quint16 port = 9000;
    qsizetype maxPayloadLength = 4096;
    qsizetype maxBufferedBytes = 8192;
    int maxDelayMs = 30000;
    int maxFragmentCount = 64;
    int maxNoiseLength = 256;
    int maxCoalesceCount = 64;
    int randomSeed = 20260804;

    [[nodiscard]] bool isValid(QString *error = nullptr) const;
};

struct TelemetryOptions
{
    using FrameFactory = std::function<ProtocolResponse(quint16 sequence)>;

    int intervalMs = 0;
    quint16 initialSequence = 1;
    FrameFactory frameFactory;

    [[nodiscard]] bool isEnabled() const;
    [[nodiscard]] bool isValid(QString *error = nullptr) const;
};

class DeviceSimulatorServer final : public QObject
{
    Q_OBJECT

public:
    using CodecFactory = std::function<std::unique_ptr<AbstractProtocolCodec>()>;
    using UnknownCommandHandler = SimulationRule::Handler;

    explicit DeviceSimulatorServer(
        DeviceSimulatorOptions options,
        CodecFactory codecFactory,
        QList<SimulationRule> rules = {},
        UnknownCommandHandler unknownCommandHandler = {},
        std::optional<TelemetryOptions> telemetryOptions = std::nullopt,
        QObject *parent = nullptr);
    ~DeviceSimulatorServer() override;

    [[nodiscard]] bool start(QString *error = nullptr);
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QHostAddress serverAddress() const;
    [[nodiscard]] quint16 serverPort() const;
    [[nodiscard]] bool hasActiveClient() const;

signals:
    void serverStarted(const QHostAddress &address, quint16 port);
    void serverStopped();
    void clientConnected(const QString &peer);
    void clientDisconnected(const QString &peer);
    void requestReceived(const DeviceSimulator::ProtocolRequest &request);
    void responseSent(const DeviceSimulator::ProtocolResponse &response);
    void telemetrySent(const DeviceSimulator::ProtocolResponse &response);
    void unknownCommand(const DeviceSimulator::ProtocolRequest &request);
    void protocolError(const DeviceSimulator::ProtocolDiagnostic &diagnostic);
    void networkError(const QString &message);
    void eventOccurred(const QString &eventType, const QString &message);

private slots:
    void acceptPendingConnections();
    void readClientData();
    void handleClientDisconnected();
    void handleSocketError(QAbstractSocket::SocketError socketError);
    void processOutputQueue();
    void writeNextFragment();
    void publishTelemetry();

private:
    struct OutboundTransmission
    {
        QByteArray data;
        ProtocolResponse response;
        QList<int> fragmentSizes;
        int fragmentDelayMs = 0;
        int coalesceCount = 1;
        int coalesceWindowMs = 0;
        bool disconnectAfterSend = false;
        bool telemetry = false;
    };

    void attachClient(QTcpSocket *socket);
    void releaseClient();
    void processNextRequest();
    void finishRequest();
    void executePlan(const ProtocolRequest &request, const ResponsePlan &plan);
    [[nodiscard]] bool queueResponse(
        const ProtocolResponse &response,
        const FaultInjectionOptions &faults,
        bool telemetry = false);
    [[nodiscard]] bool enqueueTransmission(OutboundTransmission transmission);
    void writeTransmission(OutboundTransmission transmission);
    void writeCoalesced(int count);
    void finishTransmission(const OutboundTransmission &transmission);
    void emitEvent(const QString &eventType, const QString &message);
    [[nodiscard]] bool shouldDrop(double probability);
    [[nodiscard]] bool validateFaults(
        const FaultInjectionOptions &faults,
        qsizetype encodedLength,
        QString *error) const;
    [[nodiscard]] ResponsePlan createUnknownCommandResponse(
        const ProtocolRequest &request) const;

    static constexpr qsizetype OutputQueueCapacity = 128;

    DeviceSimulatorOptions m_options;
    CodecFactory m_codecFactory;
    QList<SimulationRule> m_rules;
    UnknownCommandHandler m_unknownCommandHandler;
    std::optional<TelemetryOptions> m_telemetryOptions;
    QTcpServer m_server;
    QPointer<QTcpSocket> m_client;
    std::unique_ptr<AbstractProtocolCodec> m_codec;
    QString m_clientPeer;
    QQueue<ProtocolRequest> m_requestQueue;
    QQueue<OutboundTransmission> m_outputQueue;
    std::optional<OutboundTransmission> m_activeTransmission;
    QTimer m_coalesceTimer;
    QTimer m_fragmentTimer;
    QTimer m_telemetryTimer;
    std::mt19937 m_random;
    qsizetype m_fragmentOffset = 0;
    qsizetype m_fragmentIndex = 0;
    quint64 m_sessionGeneration = 0;
    int m_sessionOrdinal = 0;
    quint16 m_telemetrySequence = 1;
    bool m_stopping = false;
    bool m_processingRequest = false;
    bool m_writing = false;
    bool m_coalesceWindowExpired = false;
    bool m_disconnectScheduled = false;
};

} // namespace DeviceSimulator
