#pragma once

#include <DeviceSimulator/DeviceSimulatorServer.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace DeviceSimulator {

struct ServerConfiguration
{
    QString host = QStringLiteral("127.0.0.1");
    int port = 9000;
};

struct ProtocolConfiguration
{
    QString name = QStringLiteral("DemoBinaryProtocol");
    int version = 1;
};

struct SimulationLimitsConfiguration
{
    int maxPayloadLength = 4096;
    int maxBufferedBytes = 8192;
    int maxDelayMs = 30000;
    int maxFragmentCount = 64;
    int maxNoiseLength = 256;
    int maxCoalesceCount = 64;
};

struct SimulationConfiguration
{
    int randomSeed = 20260804;
};

struct ResponseConfiguration
{
    QString command;
    QString payloadHex;
};

struct FaultConfiguration
{
    int delayMs = 0;
    double dropProbability = 0.0;
    bool corruptChecksum = false;
    int invalidLengthAdjustment = 0;
    QString noisePrefixHex;
    QList<int> fragmentSizes;
    int fragmentDelayMs = 0;
    int coalesceCount = 1;
    int coalesceWindowMs = 50;
    bool disconnectAfterSend = false;
};

struct RuleConfiguration
{
    QString requestCommand;
    QString behavior = QStringLiteral("Respond");
    std::optional<ResponseConfiguration> response;
    FaultConfiguration faults;
};

struct TelemetryConfiguration
{
    bool enabled = false;
    int intervalMs = 1000;
    QString command = QStringLiteral("0x90");
    QString payloadHex;
};

struct LoggingConfiguration
{
    QString minimumLevel = QStringLiteral("Information");
};

struct SimulatorConfiguration
{
    ServerConfiguration server;
    ProtocolConfiguration protocol;
    SimulationLimitsConfiguration limits;
    SimulationConfiguration simulation;
    QList<RuleConfiguration> rules;
    TelemetryConfiguration telemetry;
    LoggingConfiguration logging;
};

struct ConfigurationError
{
    QString path;
    QString message;
};

struct ConfigurationValidationResult
{
    QList<ConfigurationError> errors;

    [[nodiscard]] bool isValid() const
    {
        return errors.isEmpty();
    }
};

class ConfigurationValues
{
public:
    [[nodiscard]] static bool parseHexByte(const QString &text, quint8 *value);
    [[nodiscard]] static bool parseHexBytes(const QString &text, QByteArray *value);
};

class ConfigurationLoader
{
public:
    [[nodiscard]] static bool load(
        const QString &path,
        SimulatorConfiguration *configuration,
        QString *error);
    [[nodiscard]] static bool fromJson(
        const QByteArray &json,
        SimulatorConfiguration *configuration,
        QString *error);
};

class ConfigurationValidator
{
public:
    [[nodiscard]] static ConfigurationValidationResult validate(
        const SimulatorConfiguration &configuration);
};

class SimulatorConfigurationFactory
{
public:
    [[nodiscard]] static std::unique_ptr<DeviceSimulatorServer> createServer(
        const SimulatorConfiguration &configuration,
        QString *error = nullptr);
};

} // namespace DeviceSimulator
