#include <DeviceSimulator/DemoBinaryProtocolCodec.h>
#include <DeviceSimulator/DeviceSimulatorServer.h>

#include <QTcpSocket>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace DeviceSimulator {

bool DeviceSimulatorOptions::isValid(QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (listenAddress.isNull()) {
        return fail(QStringLiteral("Listen address is required."));
    }
    if (maxPayloadLength < 0 || maxPayloadLength > 65535) {
        return fail(QStringLiteral("Maximum payload length must be between 0 and 65535."));
    }
    if (maxBufferedBytes < maxPayloadLength + DemoBinaryProtocolCodec::FixedFrameOverhead
        || maxBufferedBytes > 1024 * 1024) {
        return fail(QStringLiteral(
            "Maximum buffered bytes must hold one full frame and cannot exceed 1 MiB."));
    }
    if (maxDelayMs < 0 || maxDelayMs > 300000) {
        return fail(QStringLiteral("Maximum delay must be between 0 and 300000 ms."));
    }
    if (maxFragmentCount < 1 || maxFragmentCount > 256) {
        return fail(QStringLiteral("Maximum fragment count must be between 1 and 256."));
    }
    if (maxNoiseLength < 0 || maxNoiseLength > 65536) {
        return fail(QStringLiteral("Maximum noise length must be between 0 and 65536."));
    }
    if (maxCoalesceCount < 1 || maxCoalesceCount > 64) {
        return fail(QStringLiteral("Maximum coalesce count must be between 1 and 64."));
    }
    return true;
}

bool TelemetryOptions::isEnabled() const
{
    return intervalMs > 0 && static_cast<bool>(frameFactory);
}

bool TelemetryOptions::isValid(QString *error) const
{
    if (!isEnabled()) {
        if (error != nullptr) {
            *error = QStringLiteral("Telemetry interval and frame factory are required.");
        }
        return false;
    }
    if (intervalMs < 50 || intervalMs > 86400000) {
        if (error != nullptr) {
            *error = QStringLiteral("Telemetry interval must be between 50 ms and 24 hours.");
        }
        return false;
    }
    return true;
}

DeviceSimulatorServer::DeviceSimulatorServer(
    DeviceSimulatorOptions options,
    CodecFactory codecFactory,
    QList<SimulationRule> rules,
    UnknownCommandHandler unknownCommandHandler,
    std::optional<TelemetryOptions> telemetryOptions,
    QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_codecFactory(std::move(codecFactory))
    , m_rules(std::move(rules))
    , m_unknownCommandHandler(std::move(unknownCommandHandler))
    , m_telemetryOptions(std::move(telemetryOptions))
    , m_random(static_cast<std::mt19937::result_type>(m_options.randomSeed))
{
    m_server.setMaxPendingConnections(1);
    m_coalesceTimer.setSingleShot(true);
    m_fragmentTimer.setSingleShot(true);

    connect(&m_server, &QTcpServer::newConnection,
        this, &DeviceSimulatorServer::acceptPendingConnections);
    connect(&m_coalesceTimer, &QTimer::timeout, this, [this]() {
        m_coalesceWindowExpired = true;
        processOutputQueue();
    });
    connect(&m_fragmentTimer, &QTimer::timeout,
        this, &DeviceSimulatorServer::writeNextFragment);
    connect(&m_telemetryTimer, &QTimer::timeout,
        this, &DeviceSimulatorServer::publishTelemetry);
}

DeviceSimulatorServer::~DeviceSimulatorServer()
{
    stop();
}

bool DeviceSimulatorServer::start(QString *error)
{
    if (m_server.isListening()) {
        if (error != nullptr) {
            *error = QStringLiteral("Device simulator is already running.");
        }
        return false;
    }
    if (!m_options.isValid(error)) {
        return false;
    }
    if (!m_codecFactory) {
        if (error != nullptr) {
            *error = QStringLiteral("Protocol codec factory is required.");
        }
        return false;
    }
    if (m_telemetryOptions.has_value() && !m_telemetryOptions->isValid(error)) {
        return false;
    }

    m_stopping = false;
    if (!m_server.listen(m_options.listenAddress, m_options.port)) {
        if (error != nullptr) {
            *error = QStringLiteral("Unable to listen on %1:%2: %3")
                         .arg(m_options.listenAddress.toString())
                         .arg(m_options.port)
                         .arg(m_server.errorString());
        }
        return false;
    }

    emit serverStarted(m_server.serverAddress(), m_server.serverPort());
    emitEvent(QStringLiteral("ServerStarted"),
        QStringLiteral("Listening on %1:%2.")
            .arg(m_server.serverAddress().toString())
            .arg(m_server.serverPort()));
    return true;
}

void DeviceSimulatorServer::stop()
{
    if (!m_server.isListening() && m_client.isNull()) {
        return;
    }

    m_stopping = true;
    m_server.close();
    if (!m_client.isNull()) {
        m_client->disconnect(this);
        m_client->abort();
        m_client->deleteLater();
    }
    releaseClient();
    emit serverStopped();
    emitEvent(QStringLiteral("ServerStopped"), QStringLiteral("Simulator stopped."));
}

bool DeviceSimulatorServer::isRunning() const
{
    return m_server.isListening();
}

QHostAddress DeviceSimulatorServer::serverAddress() const
{
    return m_server.serverAddress();
}

quint16 DeviceSimulatorServer::serverPort() const
{
    return m_server.serverPort();
}

bool DeviceSimulatorServer::hasActiveClient() const
{
    return !m_client.isNull();
}

void DeviceSimulatorServer::acceptPendingConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        if (!m_client.isNull()) {
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        attachClient(socket);
    }
}

void DeviceSimulatorServer::readClientData()
{
    if (m_client.isNull() || !m_codec) {
        return;
    }

    const QByteArray data = m_client->readAll();
    if (data.isEmpty()) {
        return;
    }

    ProtocolDecodeResult result;
    try {
        result = m_codec->append(data);
    } catch (const std::exception &exception) {
        const QString message = QStringLiteral("Protocol parser failed: %1")
                                    .arg(QString::fromUtf8(exception.what()));
        emit networkError(message);
        emitEvent(QStringLiteral("ProtocolError"), message);
        m_client->disconnectFromHost();
        return;
    }

    for (const ProtocolDiagnostic &diagnostic : result.diagnostics) {
        emit protocolError(diagnostic);
        emitEvent(QStringLiteral("ProtocolError"),
            QStringLiteral("%1: %2").arg(diagnostic.code, diagnostic.message));
    }
    for (const ProtocolRequest &request : result.requests) {
        emit requestReceived(request);
        emitEvent(QStringLiteral("RequestReceived"),
            QStringLiteral("Received command 0x%1, sequence %2.")
                .arg(request.command, 2, 16, QLatin1Char('0'))
                .arg(request.sequence));
        m_requestQueue.enqueue(request);
    }
    processNextRequest();
}

void DeviceSimulatorServer::handleClientDisconnected()
{
    const QString peer = m_clientPeer;
    if (!m_client.isNull()) {
        m_client->deleteLater();
    }
    releaseClient();
    emit clientDisconnected(peer);
    emitEvent(QStringLiteral("ClientDisconnected"),
        QStringLiteral("Client %1 disconnected.").arg(peer));

    if (!m_stopping && m_server.isListening() && m_server.hasPendingConnections()) {
        QTimer::singleShot(0, this, &DeviceSimulatorServer::acceptPendingConnections);
    }
}

void DeviceSimulatorServer::handleSocketError(QAbstractSocket::SocketError socketError)
{
    if (socketError == QAbstractSocket::RemoteHostClosedError || m_client.isNull()) {
        return;
    }
    const QString message = m_client->errorString();
    emit networkError(message);
    emitEvent(QStringLiteral("NetworkError"), message);
}

void DeviceSimulatorServer::processOutputQueue()
{
    if (m_writing || m_client.isNull() || m_outputQueue.isEmpty()) {
        return;
    }

    const OutboundTransmission &leader = m_outputQueue.head();
    if (leader.coalesceCount > 1 && leader.fragmentSizes.isEmpty()) {
        int compatibleCount = 1;
        while (compatibleCount < m_outputQueue.size()
            && compatibleCount < leader.coalesceCount) {
            const OutboundTransmission &candidate = m_outputQueue.at(compatibleCount);
            if (!candidate.fragmentSizes.isEmpty()
                || candidate.coalesceCount != leader.coalesceCount) {
                break;
            }
            ++compatibleCount;
        }

        const bool blockedByDifferentTransmission = compatibleCount < m_outputQueue.size();
        if (compatibleCount < leader.coalesceCount
            && !blockedByDifferentTransmission
            && !m_coalesceWindowExpired) {
            if (!m_coalesceTimer.isActive()) {
                m_coalesceTimer.start(leader.coalesceWindowMs);
            }
            return;
        }

        m_coalesceTimer.stop();
        m_coalesceWindowExpired = false;
        writeCoalesced(compatibleCount);
        return;
    }

    m_coalesceTimer.stop();
    m_coalesceWindowExpired = false;
    writeTransmission(m_outputQueue.dequeue());
}

void DeviceSimulatorServer::writeNextFragment()
{
    if (!m_activeTransmission.has_value() || m_client.isNull()) {
        m_activeTransmission.reset();
        m_writing = false;
        return;
    }

    OutboundTransmission &transmission = *m_activeTransmission;
    const qsizetype remaining = transmission.data.size() - m_fragmentOffset;
    qsizetype count = remaining;
    if (m_fragmentIndex < transmission.fragmentSizes.size()) {
        count = transmission.fragmentSizes.at(m_fragmentIndex);
        ++m_fragmentIndex;
    }

    const qint64 accepted = m_client->write(transmission.data.mid(m_fragmentOffset, count));
    if (accepted != count) {
        const QString message = QStringLiteral("TCP socket did not accept a complete output fragment.");
        emit networkError(message);
        emitEvent(QStringLiteral("NetworkError"), message);
        m_client->disconnectFromHost();
        return;
    }
    m_fragmentOffset += count;

    if (m_fragmentOffset < transmission.data.size()) {
        m_fragmentTimer.start(transmission.fragmentDelayMs);
        return;
    }

    const OutboundTransmission completed = transmission;
    if (!completed.fragmentSizes.isEmpty()) {
        emitEvent(QStringLiteral("ResponseFragmented"),
            QStringLiteral("Sent command 0x%1 using fragmented writes.")
                .arg(completed.response.command, 2, 16, QLatin1Char('0')));
    }
    m_activeTransmission.reset();
    m_writing = false;
    finishTransmission(completed);
}

void DeviceSimulatorServer::publishTelemetry()
{
    if (m_client.isNull() || !m_telemetryOptions.has_value()
        || !m_telemetryOptions->isEnabled() || m_disconnectScheduled) {
        return;
    }

    try {
        const ProtocolResponse response = m_telemetryOptions->frameFactory(m_telemetrySequence);
        if (queueResponse(response, {}, true)) {
            m_telemetrySequence = static_cast<quint16>(m_telemetrySequence + 1);
        }
    } catch (const std::exception &exception) {
        const QString message = QStringLiteral("Telemetry generation failed: %1")
                                    .arg(QString::fromUtf8(exception.what()));
        emit protocolError({QStringLiteral("TelemetryError"), message, 0});
        emitEvent(QStringLiteral("ProtocolError"), message);
        m_client->disconnectFromHost();
    }
}

void DeviceSimulatorServer::attachClient(QTcpSocket *socket)
{
    try {
        m_codec = m_codecFactory();
    } catch (const std::exception &exception) {
        const QString message = QStringLiteral("Unable to create protocol codec: %1")
                                    .arg(QString::fromUtf8(exception.what()));
        emit networkError(message);
        emitEvent(QStringLiteral("NetworkError"), message);
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }
    if (!m_codec) {
        const QString message = QStringLiteral("Protocol codec factory returned null.");
        emit networkError(message);
        emitEvent(QStringLiteral("NetworkError"), message);
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }

    ++m_sessionGeneration;
    m_random.seed(static_cast<std::mt19937::result_type>(m_options.randomSeed + m_sessionOrdinal));
    ++m_sessionOrdinal;
    m_client = socket;
    m_clientPeer = QStringLiteral("%1:%2")
                       .arg(socket->peerAddress().toString())
                       .arg(socket->peerPort());
    m_disconnectScheduled = false;
    m_telemetrySequence = m_telemetryOptions.has_value()
        ? m_telemetryOptions->initialSequence
        : 1;

    connect(socket, &QTcpSocket::readyRead,
        this, &DeviceSimulatorServer::readClientData);
    connect(socket, &QTcpSocket::disconnected,
        this, &DeviceSimulatorServer::handleClientDisconnected);
    connect(socket, &QTcpSocket::errorOccurred,
        this, &DeviceSimulatorServer::handleSocketError);

    if (m_telemetryOptions.has_value() && m_telemetryOptions->isEnabled()) {
        m_telemetryTimer.start(m_telemetryOptions->intervalMs);
    }
    emit clientConnected(m_clientPeer);
    emitEvent(QStringLiteral("ClientConnected"),
        QStringLiteral("Client connected from %1.").arg(m_clientPeer));
}

void DeviceSimulatorServer::releaseClient()
{
    ++m_sessionGeneration;
    m_coalesceTimer.stop();
    m_fragmentTimer.stop();
    m_telemetryTimer.stop();
    m_requestQueue.clear();
    m_outputQueue.clear();
    m_activeTransmission.reset();
    m_client.clear();
    m_codec.reset();
    m_clientPeer.clear();
    m_fragmentOffset = 0;
    m_fragmentIndex = 0;
    m_telemetrySequence = m_telemetryOptions.has_value()
        ? m_telemetryOptions->initialSequence
        : 1;
    m_processingRequest = false;
    m_writing = false;
    m_coalesceWindowExpired = false;
    m_disconnectScheduled = false;
}

void DeviceSimulatorServer::processNextRequest()
{
    if (m_processingRequest || m_disconnectScheduled
        || m_client.isNull() || m_requestQueue.isEmpty()) {
        return;
    }

    const ProtocolRequest request = m_requestQueue.dequeue();
    m_processingRequest = true;
    try {
        const SimulationRule *rule = RuleMatcher::findMatch(m_rules, request);
        ResponsePlan plan;
        if (rule != nullptr) {
            plan = rule->createResponse(request);
        } else {
            emit unknownCommand(request);
            emitEvent(QStringLiteral("UnknownCommand"),
                QStringLiteral("No rule matched command 0x%1.")
                    .arg(request.command, 2, 16, QLatin1Char('0')));
            plan = createUnknownCommandResponse(request);
        }
        executePlan(request, plan);
    } catch (const std::exception &exception) {
        const QString message = QStringLiteral("Simulation rule failed: %1")
                                    .arg(QString::fromUtf8(exception.what()));
        emit networkError(message);
        emitEvent(QStringLiteral("ProtocolError"), message);
        m_processingRequest = false;
        m_client->disconnectFromHost();
    }
}

void DeviceSimulatorServer::finishRequest()
{
    m_processingRequest = false;
    QTimer::singleShot(0, this, &DeviceSimulatorServer::processNextRequest);
}

void DeviceSimulatorServer::executePlan(
    const ProtocolRequest &request,
    const ResponsePlan &plan)
{
    switch (plan.behavior) {
    case SimulationBehavior::NoResponse:
        emitEvent(QStringLiteral("FaultApplied"),
            QStringLiteral("Command 0x%1 intentionally produced no response.")
                .arg(request.command, 2, 16, QLatin1Char('0')));
        finishRequest();
        return;
    case SimulationBehavior::DropResponse:
        emitEvent(QStringLiteral("ResponseDropped"),
            QStringLiteral("Command 0x%1 dropped its response.")
                .arg(request.command, 2, 16, QLatin1Char('0')));
        finishRequest();
        return;
    case SimulationBehavior::Disconnect:
        emitEvent(QStringLiteral("ConnectionClosed"),
            QStringLiteral("Command 0x%1 requested a disconnect.")
                .arg(request.command, 2, 16, QLatin1Char('0')));
        m_processingRequest = false;
        m_disconnectScheduled = true;
        m_telemetryTimer.stop();
        m_client->disconnectFromHost();
        return;
    case SimulationBehavior::Respond:
        break;
    }

    if (!plan.response.has_value()) {
        const QString message = QStringLiteral("Responding plan does not contain a response.");
        emit networkError(message);
        emitEvent(QStringLiteral("ProtocolError"), message);
        m_processingRequest = false;
        m_client->disconnectFromHost();
        return;
    }

    QString faultError;
    if (!validateFaults(plan.faults,
            DemoBinaryProtocolCodec::FixedFrameOverhead + plan.response->payload.size(),
            &faultError)) {
        emit networkError(faultError);
        emitEvent(QStringLiteral("ProtocolError"), faultError);
        m_processingRequest = false;
        m_client->disconnectFromHost();
        return;
    }

    if (shouldDrop(plan.faults.dropProbability)) {
        emitEvent(QStringLiteral("ResponseDropped"),
            QStringLiteral("Dropped response using probability %1.")
                .arg(plan.faults.dropProbability));
        finishRequest();
        return;
    }

    if (plan.faults.delayMs > 0) {
        emitEvent(QStringLiteral("ResponseDelayed"),
            QStringLiteral("Delayed the response by %1 ms.").arg(plan.faults.delayMs));
        const quint64 generation = m_sessionGeneration;
        const ProtocolResponse response = *plan.response;
        const FaultInjectionOptions faults = plan.faults;
        QTimer::singleShot(plan.faults.delayMs, this,
            [this, generation, response, faults]() {
                if (generation != m_sessionGeneration || m_client.isNull()) {
                    return;
                }
                if (queueResponse(response, faults)) {
                    finishRequest();
                } else {
                    m_processingRequest = false;
                }
            });
        return;
    }

    if (queueResponse(*plan.response, plan.faults)) {
        finishRequest();
    } else {
        m_processingRequest = false;
    }
}

bool DeviceSimulatorServer::queueResponse(
    const ProtocolResponse &response,
    const FaultInjectionOptions &faults,
    bool telemetry)
{
    if (m_client.isNull() || !m_codec) {
        return false;
    }

    QByteArray encoded;
    try {
        encoded = m_codec->encode(response);
        if (faults.invalidLengthAdjustment != 0 || faults.corruptChecksum) {
            auto *demoCodec = dynamic_cast<DemoBinaryProtocolCodec *>(m_codec.get());
            if (demoCodec == nullptr) {
                throw std::runtime_error("Protocol codec does not support frame mutation.");
            }
            if (faults.invalidLengthAdjustment != 0) {
                encoded = demoCodec->adjustPayloadLength(
                    encoded, faults.invalidLengthAdjustment);
                emitEvent(QStringLiteral("InvalidLengthInjected"),
                    QStringLiteral("Adjusted encoded payload length by %1.")
                        .arg(faults.invalidLengthAdjustment));
            }
            if (faults.corruptChecksum) {
                encoded = demoCodec->corruptChecksum(encoded);
                emitEvent(QStringLiteral("ChecksumCorrupted"),
                    QStringLiteral("Corrupted the encoded response checksum."));
            }
        }
    } catch (const std::exception &exception) {
        const QString message = QStringLiteral("Response encoding failed: %1")
                                    .arg(QString::fromUtf8(exception.what()));
        emit networkError(message);
        emitEvent(QStringLiteral("ProtocolError"), message);
        m_client->disconnectFromHost();
        return false;
    }

    QByteArray outboundData = faults.noisePrefix;
    outboundData.append(encoded);
    if (!faults.noisePrefix.isEmpty()) {
        emitEvent(QStringLiteral("NoiseInserted"),
            QStringLiteral("Inserted %1 noise bytes before the response.")
                .arg(faults.noisePrefix.size()));
    }

    OutboundTransmission transmission;
    transmission.data = std::move(outboundData);
    transmission.response = response;
    transmission.fragmentSizes = faults.fragmentSizes;
    transmission.fragmentDelayMs = faults.fragmentDelayMs;
    transmission.coalesceCount = faults.coalesceCount;
    transmission.coalesceWindowMs = faults.coalesceWindowMs;
    transmission.disconnectAfterSend = faults.disconnectAfterSend;
    transmission.telemetry = telemetry;

    if (faults.disconnectAfterSend) {
        m_disconnectScheduled = true;
        m_telemetryTimer.stop();
    }
    return enqueueTransmission(std::move(transmission));
}

bool DeviceSimulatorServer::enqueueTransmission(OutboundTransmission transmission)
{
    const qsizetype activeWriteCount = m_writing ? 1 : 0;
    if (m_outputQueue.size() + activeWriteCount >= OutputQueueCapacity) {
        const QString message = QStringLiteral("Session output queue reached its limit.");
        emit networkError(message);
        emitEvent(QStringLiteral("NetworkError"), message);
        if (!m_client.isNull()) {
            m_client->disconnectFromHost();
        }
        return false;
    }
    m_outputQueue.enqueue(std::move(transmission));
    processOutputQueue();
    return true;
}

void DeviceSimulatorServer::writeTransmission(OutboundTransmission transmission)
{
    m_writing = true;
    m_fragmentOffset = 0;
    m_fragmentIndex = 0;
    m_activeTransmission = std::move(transmission);
    writeNextFragment();
}

void DeviceSimulatorServer::writeCoalesced(int count)
{
    m_writing = true;
    QList<OutboundTransmission> batch;
    QByteArray combined;
    bool disconnectAfterSend = false;
    for (int index = 0; index < count; ++index) {
        OutboundTransmission transmission = m_outputQueue.dequeue();
        combined.append(transmission.data);
        disconnectAfterSend = disconnectAfterSend || transmission.disconnectAfterSend;
        batch.append(std::move(transmission));
    }

    const qint64 accepted = m_client->write(combined);
    if (accepted != combined.size()) {
        const QString message = QStringLiteral("TCP socket did not accept the coalesced output.");
        emit networkError(message);
        emitEvent(QStringLiteral("NetworkError"), message);
        m_client->disconnectFromHost();
        return;
    }
    if (batch.size() > 1) {
        emitEvent(QStringLiteral("ResponsesCoalesced"),
            QStringLiteral("Combined %1 responses into one TCP write.").arg(batch.size()));
    }
    for (const OutboundTransmission &transmission : batch) {
        if (transmission.telemetry) {
            emit telemetrySent(transmission.response);
            emitEvent(QStringLiteral("TelemetrySent"),
                QStringLiteral("Sent telemetry command 0x%1.")
                    .arg(transmission.response.command, 2, 16, QLatin1Char('0')));
        } else {
            emit responseSent(transmission.response);
            emitEvent(QStringLiteral("ResponseSent"),
                QStringLiteral("Sent response command 0x%1.")
                    .arg(transmission.response.command, 2, 16, QLatin1Char('0')));
        }
    }
    m_writing = false;
    if (disconnectAfterSend) {
        emitEvent(QStringLiteral("ConnectionClosed"),
            QStringLiteral("Configured response was sent before disconnecting."));
        m_client->disconnectFromHost();
    } else {
        QTimer::singleShot(0, this, &DeviceSimulatorServer::processOutputQueue);
    }
}

void DeviceSimulatorServer::finishTransmission(
    const OutboundTransmission &transmission)
{
    if (transmission.telemetry) {
        emit telemetrySent(transmission.response);
        emitEvent(QStringLiteral("TelemetrySent"),
            QStringLiteral("Sent telemetry command 0x%1.")
                .arg(transmission.response.command, 2, 16, QLatin1Char('0')));
    } else {
        emit responseSent(transmission.response);
        emitEvent(QStringLiteral("ResponseSent"),
            QStringLiteral("Sent response command 0x%1.")
                .arg(transmission.response.command, 2, 16, QLatin1Char('0')));
    }

    if (transmission.disconnectAfterSend) {
        emitEvent(QStringLiteral("ConnectionClosed"),
            QStringLiteral("Configured response was sent before disconnecting."));
        m_client->disconnectFromHost();
    } else {
        QTimer::singleShot(0, this, &DeviceSimulatorServer::processOutputQueue);
    }
}

void DeviceSimulatorServer::emitEvent(
    const QString &eventType,
    const QString &message)
{
    emit eventOccurred(eventType, message);
}

bool DeviceSimulatorServer::shouldDrop(double probability)
{
    if (probability <= 0.0) {
        return false;
    }
    if (probability >= 1.0) {
        return true;
    }
    return std::uniform_real_distribution<double>(0.0, 1.0)(m_random) < probability;
}

bool DeviceSimulatorServer::validateFaults(
    const FaultInjectionOptions &faults,
    qsizetype encodedLength,
    QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (!faults.isValid(error)) {
        return false;
    }
    if (faults.delayMs > m_options.maxDelayMs
        || (!faults.fragmentSizes.isEmpty()
            && faults.fragmentDelayMs > m_options.maxDelayMs)
        || (faults.coalesceCount > 1
            && faults.coalesceWindowMs > m_options.maxDelayMs)) {
        return fail(QStringLiteral("A configured fault delay exceeds the server limit."));
    }
    if (faults.fragmentSizes.size() > m_options.maxFragmentCount) {
        return fail(QStringLiteral("Fragment count exceeds the server limit."));
    }
    if (faults.noisePrefix.size() > m_options.maxNoiseLength) {
        return fail(QStringLiteral("Noise prefix exceeds the server limit."));
    }
    if (faults.coalesceCount > m_options.maxCoalesceCount) {
        return fail(QStringLiteral("Coalesce count exceeds the server limit."));
    }

    const qsizetype payloadLength = encodedLength - DemoBinaryProtocolCodec::FixedFrameOverhead;
    const qint64 adjustedLength = payloadLength + faults.invalidLengthAdjustment;
    if (adjustedLength < 0 || adjustedLength > 65535) {
        return fail(QStringLiteral("Adjusted payload length must remain between 0 and 65535."));
    }

    qsizetype fragmentTotal = 0;
    for (const int size : faults.fragmentSizes) {
        fragmentTotal += size;
    }
    if (fragmentTotal > encodedLength + faults.noisePrefix.size()) {
        return fail(QStringLiteral("Fragment sizes exceed the outgoing byte length."));
    }
    return true;
}

ResponsePlan DeviceSimulatorServer::createUnknownCommandResponse(
    const ProtocolRequest &request) const
{
    if (m_unknownCommandHandler) {
        return m_unknownCommandHandler(request);
    }

    QByteArray payload;
    payload.append(static_cast<char>(0x01));
    payload.append(static_cast<char>(request.command));
    return ResponsePlan::respond({request.version, 0xFF, request.sequence, payload});
}

} // namespace DeviceSimulator
