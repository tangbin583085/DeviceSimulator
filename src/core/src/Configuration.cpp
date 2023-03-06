#include <DeviceSimulator/Configuration.h>
#include <DeviceSimulator/DemoBinaryProtocolCodec.h>

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace DeviceSimulator {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool keyMatches(const QString &key, const char *expected)
{
    return key.compare(QString::fromLatin1(expected), Qt::CaseInsensitive) == 0;
}

QJsonValue findValue(const QJsonObject &object, const char *name)
{
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (keyMatches(iterator.key(), name)) {
            return iterator.value();
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}

bool rejectUnknown(
    const QJsonObject &object,
    std::initializer_list<const char *> allowed,
    const QString &path,
    QString *error)
{
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        const bool known = std::any_of(allowed.begin(), allowed.end(),
            [&iterator](const char *name) { return keyMatches(iterator.key(), name); });
        if (!known) {
            return fail(error,
                QStringLiteral("%1.%2: unknown property.").arg(path, iterator.key()));
        }
    }
    return true;
}

bool readObject(
    const QJsonObject &parent,
    const char *name,
    const QString &path,
    QJsonObject *value,
    QString *error)
{
    const QJsonValue jsonValue = findValue(parent, name);
    if (jsonValue.isUndefined()) {
        return true;
    }
    if (!jsonValue.isObject()) {
        return fail(error, QStringLiteral("%1 must be an object.").arg(path));
    }
    *value = jsonValue.toObject();
    return true;
}

bool readString(
    const QJsonObject &object,
    const char *name,
    const QString &path,
    QString *value,
    QString *error)
{
    const QJsonValue jsonValue = findValue(object, name);
    if (jsonValue.isUndefined()) {
        return true;
    }
    if (!jsonValue.isString()) {
        return fail(error, QStringLiteral("%1 must be a string.").arg(path));
    }
    *value = jsonValue.toString();
    return true;
}

bool readBool(
    const QJsonObject &object,
    const char *name,
    const QString &path,
    bool *value,
    QString *error)
{
    const QJsonValue jsonValue = findValue(object, name);
    if (jsonValue.isUndefined()) {
        return true;
    }
    if (!jsonValue.isBool()) {
        return fail(error, QStringLiteral("%1 must be true or false.").arg(path));
    }
    *value = jsonValue.toBool();
    return true;
}

bool readInt(
    const QJsonObject &object,
    const char *name,
    const QString &path,
    int *value,
    QString *error)
{
    const QJsonValue jsonValue = findValue(object, name);
    if (jsonValue.isUndefined()) {
        return true;
    }
    if (!jsonValue.isDouble()) {
        return fail(error, QStringLiteral("%1 must be an integer.").arg(path));
    }
    const double number = jsonValue.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) {
        return fail(error, QStringLiteral("%1 must be a 32-bit integer.").arg(path));
    }
    *value = static_cast<int>(number);
    return true;
}

bool readDouble(
    const QJsonObject &object,
    const char *name,
    const QString &path,
    double *value,
    QString *error)
{
    const QJsonValue jsonValue = findValue(object, name);
    if (jsonValue.isUndefined()) {
        return true;
    }
    if (!jsonValue.isDouble() || !std::isfinite(jsonValue.toDouble())) {
        return fail(error, QStringLiteral("%1 must be a finite number.").arg(path));
    }
    *value = jsonValue.toDouble();
    return true;
}

bool readIntArray(
    const QJsonObject &object,
    const char *name,
    const QString &path,
    QList<int> *value,
    QString *error)
{
    const QJsonValue jsonValue = findValue(object, name);
    if (jsonValue.isUndefined()) {
        return true;
    }
    if (!jsonValue.isArray()) {
        return fail(error, QStringLiteral("%1 must be an array.").arg(path));
    }

    QList<int> parsed;
    const QJsonArray array = jsonValue.toArray();
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QJsonValue item = array.at(index);
        const double number = item.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!item.isDouble() || !std::isfinite(number) || std::floor(number) != number
            || number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max()) {
            return fail(error,
                QStringLiteral("%1[%2] must be a 32-bit integer.").arg(path).arg(index));
        }
        parsed.append(static_cast<int>(number));
    }
    *value = std::move(parsed);
    return true;
}

std::optional<SimulationBehavior> parseBehavior(const QString &text)
{
    if (text.compare(QStringLiteral("Respond"), Qt::CaseInsensitive) == 0) {
        return SimulationBehavior::Respond;
    }
    if (text.compare(QStringLiteral("NoResponse"), Qt::CaseInsensitive) == 0) {
        return SimulationBehavior::NoResponse;
    }
    if (text.compare(QStringLiteral("DropResponse"), Qt::CaseInsensitive) == 0) {
        return SimulationBehavior::DropResponse;
    }
    if (text.compare(QStringLiteral("Disconnect"), Qt::CaseInsensitive) == 0) {
        return SimulationBehavior::Disconnect;
    }
    return std::nullopt;
}

void addError(
    ConfigurationValidationResult &result,
    const QString &path,
    const QString &message)
{
    result.errors.append({path, message});
}

bool parseFaults(
    const QJsonObject &object,
    const QString &path,
    FaultConfiguration *faults,
    QString *error)
{
    if (!rejectUnknown(object,
            {"delayMs", "dropProbability", "corruptChecksum", "invalidLengthAdjustment",
                "noisePrefixHex", "fragmentSizes", "fragmentDelayMs", "coalesceCount",
                "coalesceWindowMs", "disconnectAfterSend"},
            path, error)) {
        return false;
    }
    return readInt(object, "delayMs", path + QStringLiteral(".delayMs"), &faults->delayMs, error)
        && readDouble(object, "dropProbability", path + QStringLiteral(".dropProbability"),
            &faults->dropProbability, error)
        && readBool(object, "corruptChecksum", path + QStringLiteral(".corruptChecksum"),
            &faults->corruptChecksum, error)
        && readInt(object, "invalidLengthAdjustment",
            path + QStringLiteral(".invalidLengthAdjustment"),
            &faults->invalidLengthAdjustment, error)
        && readString(object, "noisePrefixHex", path + QStringLiteral(".noisePrefixHex"),
            &faults->noisePrefixHex, error)
        && readIntArray(object, "fragmentSizes", path + QStringLiteral(".fragmentSizes"),
            &faults->fragmentSizes, error)
        && readInt(object, "fragmentDelayMs", path + QStringLiteral(".fragmentDelayMs"),
            &faults->fragmentDelayMs, error)
        && readInt(object, "coalesceCount", path + QStringLiteral(".coalesceCount"),
            &faults->coalesceCount, error)
        && readInt(object, "coalesceWindowMs", path + QStringLiteral(".coalesceWindowMs"),
            &faults->coalesceWindowMs, error)
        && readBool(object, "disconnectAfterSend",
            path + QStringLiteral(".disconnectAfterSend"),
            &faults->disconnectAfterSend, error);
}

} // namespace

bool ConfigurationValues::parseHexByte(const QString &text, quint8 *value)
{
    QString normalized = text.trimmed();
    if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        normalized.remove(0, 2);
    }
    if (normalized.isEmpty() || normalized.size() > 2) {
        return false;
    }
    bool ok = false;
    const uint parsed = normalized.toUInt(&ok, 16);
    if (!ok || parsed > 0xFF) {
        return false;
    }
    if (value != nullptr) {
        *value = static_cast<quint8>(parsed);
    }
    return true;
}

bool ConfigurationValues::parseHexBytes(const QString &text, QByteArray *value)
{
    QByteArray parsed;
    const QString normalized = text.simplified();
    if (!normalized.isEmpty()) {
        const QStringList tokens = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        parsed.reserve(tokens.size());
        for (const QString &token : tokens) {
            quint8 byte = 0;
            if (!parseHexByte(token, &byte)) {
                return false;
            }
            parsed.append(static_cast<char>(byte));
        }
    }
    if (value != nullptr) {
        *value = std::move(parsed);
    }
    return true;
}

bool ConfigurationLoader::load(
    const QString &path,
    SimulatorConfiguration *configuration,
    QString *error)
{
    if (path.trimmed().isEmpty()) {
        return fail(error, QStringLiteral("A configuration file path is required."));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error,
            QStringLiteral("Cannot read configuration file '%1': %2")
                .arg(path, file.errorString()));
    }
    return fromJson(file.readAll(), configuration, error);
}

bool ConfigurationLoader::fromJson(
    const QByteArray &json,
    SimulatorConfiguration *configuration,
    QString *error)
{
    if (configuration == nullptr) {
        return fail(error, QStringLiteral("Configuration output is required."));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(error,
            QStringLiteral("Configuration JSON is invalid at offset %1: %2")
                .arg(parseError.offset)
                .arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return fail(error, QStringLiteral("Configuration root must be an object."));
    }

    const QJsonObject root = document.object();
    if (!rejectUnknown(root,
            {"server", "protocol", "limits", "simulation", "rules", "telemetry", "logging"},
            QStringLiteral("$"), error)) {
        return false;
    }

    SimulatorConfiguration parsed;
    QJsonObject section;
    if (!readObject(root, "server", QStringLiteral("$.server"), &section, error)
        || !rejectUnknown(section, {"host", "port"}, QStringLiteral("$.server"), error)
        || !readString(section, "host", QStringLiteral("$.server.host"), &parsed.server.host, error)
        || !readInt(section, "port", QStringLiteral("$.server.port"), &parsed.server.port, error)) {
        return false;
    }

    section = {};
    if (!readObject(root, "protocol", QStringLiteral("$.protocol"), &section, error)
        || !rejectUnknown(section, {"name", "version"}, QStringLiteral("$.protocol"), error)
        || !readString(section, "name", QStringLiteral("$.protocol.name"), &parsed.protocol.name, error)
        || !readInt(section, "version", QStringLiteral("$.protocol.version"),
            &parsed.protocol.version, error)) {
        return false;
    }

    section = {};
    if (!readObject(root, "limits", QStringLiteral("$.limits"), &section, error)
        || !rejectUnknown(section,
            {"maxPayloadLength", "maxBufferedBytes", "maxDelayMs", "maxFragmentCount",
                "maxNoiseLength", "maxCoalesceCount"},
            QStringLiteral("$.limits"), error)
        || !readInt(section, "maxPayloadLength", QStringLiteral("$.limits.maxPayloadLength"),
            &parsed.limits.maxPayloadLength, error)
        || !readInt(section, "maxBufferedBytes", QStringLiteral("$.limits.maxBufferedBytes"),
            &parsed.limits.maxBufferedBytes, error)
        || !readInt(section, "maxDelayMs", QStringLiteral("$.limits.maxDelayMs"),
            &parsed.limits.maxDelayMs, error)
        || !readInt(section, "maxFragmentCount", QStringLiteral("$.limits.maxFragmentCount"),
            &parsed.limits.maxFragmentCount, error)
        || !readInt(section, "maxNoiseLength", QStringLiteral("$.limits.maxNoiseLength"),
            &parsed.limits.maxNoiseLength, error)
        || !readInt(section, "maxCoalesceCount", QStringLiteral("$.limits.maxCoalesceCount"),
            &parsed.limits.maxCoalesceCount, error)) {
        return false;
    }

    section = {};
    if (!readObject(root, "simulation", QStringLiteral("$.simulation"), &section, error)
        || !rejectUnknown(section, {"randomSeed"}, QStringLiteral("$.simulation"), error)
        || !readInt(section, "randomSeed", QStringLiteral("$.simulation.randomSeed"),
            &parsed.simulation.randomSeed, error)) {
        return false;
    }

    const QJsonValue rulesValue = findValue(root, "rules");
    if (!rulesValue.isUndefined()) {
        if (!rulesValue.isArray()) {
            return fail(error, QStringLiteral("$.rules must be an array."));
        }
        const QJsonArray rules = rulesValue.toArray();
        for (qsizetype index = 0; index < rules.size(); ++index) {
            const QString path = QStringLiteral("$.rules[%1]").arg(index);
            if (!rules.at(index).isObject()) {
                return fail(error, path + QStringLiteral(" must be an object."));
            }
            const QJsonObject ruleObject = rules.at(index).toObject();
            if (!rejectUnknown(ruleObject,
                    {"requestCommand", "behavior", "response", "faults"}, path, error)) {
                return false;
            }

            RuleConfiguration rule;
            if (!readString(ruleObject, "requestCommand", path + QStringLiteral(".requestCommand"),
                    &rule.requestCommand, error)
                || !readString(ruleObject, "behavior", path + QStringLiteral(".behavior"),
                    &rule.behavior, error)) {
                return false;
            }

            const QJsonValue responseValue = findValue(ruleObject, "response");
            if (!responseValue.isUndefined()) {
                if (!responseValue.isObject()) {
                    return fail(error, path + QStringLiteral(".response must be an object."));
                }
                const QJsonObject responseObject = responseValue.toObject();
                if (!rejectUnknown(responseObject, {"command", "payloadHex"},
                        path + QStringLiteral(".response"), error)) {
                    return false;
                }
                ResponseConfiguration response;
                if (!readString(responseObject, "command", path + QStringLiteral(".response.command"),
                        &response.command, error)
                    || !readString(responseObject, "payloadHex",
                        path + QStringLiteral(".response.payloadHex"),
                        &response.payloadHex, error)) {
                    return false;
                }
                rule.response = std::move(response);
            }

            const QJsonValue faultsValue = findValue(ruleObject, "faults");
            if (!faultsValue.isUndefined()) {
                if (!faultsValue.isObject()) {
                    return fail(error, path + QStringLiteral(".faults must be an object."));
                }
                if (!parseFaults(faultsValue.toObject(), path + QStringLiteral(".faults"),
                        &rule.faults, error)) {
                    return false;
                }
            }
            parsed.rules.append(std::move(rule));
        }
    }

    section = {};
    if (!readObject(root, "telemetry", QStringLiteral("$.telemetry"), &section, error)
        || !rejectUnknown(section, {"enabled", "intervalMs", "command", "payloadHex"},
            QStringLiteral("$.telemetry"), error)
        || !readBool(section, "enabled", QStringLiteral("$.telemetry.enabled"),
            &parsed.telemetry.enabled, error)
        || !readInt(section, "intervalMs", QStringLiteral("$.telemetry.intervalMs"),
            &parsed.telemetry.intervalMs, error)
        || !readString(section, "command", QStringLiteral("$.telemetry.command"),
            &parsed.telemetry.command, error)
        || !readString(section, "payloadHex", QStringLiteral("$.telemetry.payloadHex"),
            &parsed.telemetry.payloadHex, error)) {
        return false;
    }

    section = {};
    if (!readObject(root, "logging", QStringLiteral("$.logging"), &section, error)
        || !rejectUnknown(section, {"minimumLevel"}, QStringLiteral("$.logging"), error)
        || !readString(section, "minimumLevel", QStringLiteral("$.logging.minimumLevel"),
            &parsed.logging.minimumLevel, error)) {
        return false;
    }

    *configuration = std::move(parsed);
    return true;
}

ConfigurationValidationResult ConfigurationValidator::validate(
    const SimulatorConfiguration &configuration)
{
    ConfigurationValidationResult result;
    if (QHostAddress(configuration.server.host).isNull()) {
        addError(result, QStringLiteral("$.server.host"),
            QStringLiteral("Host must be a valid IP address."));
    }
    if (configuration.server.port < 1 || configuration.server.port > 65535) {
        addError(result, QStringLiteral("$.server.port"),
            QStringLiteral("Port must be between 1 and 65535."));
    }
    if (configuration.protocol.name.compare(
            QStringLiteral("DemoBinaryProtocol"), Qt::CaseInsensitive) != 0) {
        addError(result, QStringLiteral("$.protocol.name"),
            QStringLiteral("Only DemoBinaryProtocol is supported."));
    }
    if (configuration.protocol.version < 0 || configuration.protocol.version > 255) {
        addError(result, QStringLiteral("$.protocol.version"),
            QStringLiteral("Version must be between 0 and 255."));
    }

    const SimulationLimitsConfiguration &limits = configuration.limits;
    if (limits.maxPayloadLength < 0 || limits.maxPayloadLength > 65535) {
        addError(result, QStringLiteral("$.limits.maxPayloadLength"),
            QStringLiteral("Value must be between 0 and 65535."));
    }
    const qint64 minimumBuffer = static_cast<qint64>(limits.maxPayloadLength)
        + DemoBinaryProtocolCodec::FixedFrameOverhead;
    if (limits.maxBufferedBytes < minimumBuffer || limits.maxBufferedBytes > 1024 * 1024) {
        addError(result, QStringLiteral("$.limits.maxBufferedBytes"),
            QStringLiteral("Value must cover one maximum frame and cannot exceed 1 MiB."));
    }
    if (limits.maxDelayMs < 0 || limits.maxDelayMs > 300000) {
        addError(result, QStringLiteral("$.limits.maxDelayMs"),
            QStringLiteral("Value must be between 0 and 300000 ms."));
    }
    if (limits.maxFragmentCount < 1 || limits.maxFragmentCount > 256) {
        addError(result, QStringLiteral("$.limits.maxFragmentCount"),
            QStringLiteral("Value must be between 1 and 256."));
    }
    if (limits.maxNoiseLength < 0 || limits.maxNoiseLength > 65536) {
        addError(result, QStringLiteral("$.limits.maxNoiseLength"),
            QStringLiteral("Value must be between 0 and 65536 bytes."));
    }
    if (limits.maxCoalesceCount < 1 || limits.maxCoalesceCount > 64) {
        addError(result, QStringLiteral("$.limits.maxCoalesceCount"),
            QStringLiteral("Value must be between 1 and 64."));
    }

    QSet<quint8> commands;
    for (qsizetype index = 0; index < configuration.rules.size(); ++index) {
        const RuleConfiguration &rule = configuration.rules.at(index);
        const QString path = QStringLiteral("$.rules[%1]").arg(index);
        quint8 requestCommand = 0;
        if (!ConfigurationValues::parseHexByte(rule.requestCommand, &requestCommand)) {
            addError(result, path + QStringLiteral(".requestCommand"),
                QStringLiteral("Command must be a hexadecimal byte such as 0x01."));
        } else if (commands.contains(requestCommand)) {
            addError(result, path + QStringLiteral(".requestCommand"),
                QStringLiteral("Duplicate rule for this command."));
        } else {
            commands.insert(requestCommand);
        }

        const std::optional<SimulationBehavior> behavior = parseBehavior(rule.behavior);
        if (!behavior.has_value()) {
            addError(result, path + QStringLiteral(".behavior"),
                QStringLiteral("Unsupported simulation behavior."));
            continue;
        }

        QByteArray payload;
        bool validPayload = true;
        if (*behavior == SimulationBehavior::Respond) {
            if (!rule.response.has_value()) {
                addError(result, path + QStringLiteral(".response"),
                    QStringLiteral("A responding rule requires a response."));
                validPayload = false;
            } else {
                if (!ConfigurationValues::parseHexByte(rule.response->command, nullptr)) {
                    addError(result, path + QStringLiteral(".response.command"),
                        QStringLiteral("Command must be a hexadecimal byte."));
                }
                if (!ConfigurationValues::parseHexBytes(rule.response->payloadHex, &payload)) {
                    addError(result, path + QStringLiteral(".response.payloadHex"),
                        QStringLiteral("Payload must contain hexadecimal bytes."));
                    validPayload = false;
                } else if (payload.size() > limits.maxPayloadLength) {
                    addError(result, path + QStringLiteral(".response.payloadHex"),
                        QStringLiteral("Payload exceeds the configured maximum."));
                }
            }
        }

        const FaultConfiguration &faults = rule.faults;
        const QString faultPath = path + QStringLiteral(".faults");
        if (faults.delayMs < 0 || faults.delayMs > limits.maxDelayMs) {
            addError(result, faultPath + QStringLiteral(".delayMs"),
                QStringLiteral("Delay is outside the configured limit."));
        }
        if (!std::isfinite(faults.dropProbability)
            || faults.dropProbability < 0.0 || faults.dropProbability > 1.0) {
            addError(result, faultPath + QStringLiteral(".dropProbability"),
                QStringLiteral("Probability must be between 0 and 1."));
        }
        QByteArray noise;
        if (!ConfigurationValues::parseHexBytes(faults.noisePrefixHex, &noise)) {
            addError(result, faultPath + QStringLiteral(".noisePrefixHex"),
                QStringLiteral("Noise must contain hexadecimal bytes."));
        } else if (noise.size() > limits.maxNoiseLength) {
            addError(result, faultPath + QStringLiteral(".noisePrefixHex"),
                QStringLiteral("Noise exceeds the configured maximum."));
        }
        if (faults.fragmentSizes.size() > limits.maxFragmentCount
            || std::any_of(faults.fragmentSizes.cbegin(), faults.fragmentSizes.cend(),
                [](int size) { return size <= 0; })) {
            addError(result, faultPath + QStringLiteral(".fragmentSizes"),
                QStringLiteral("Fragment sizes must be positive and within the count limit."));
        }
        if (!faults.fragmentSizes.isEmpty()
            && (faults.fragmentDelayMs < 0 || faults.fragmentDelayMs > limits.maxDelayMs)) {
            addError(result, faultPath + QStringLiteral(".fragmentDelayMs"),
                QStringLiteral("Fragment delay is outside the configured limit."));
        }
        if (faults.coalesceCount < 1 || faults.coalesceCount > limits.maxCoalesceCount) {
            addError(result, faultPath + QStringLiteral(".coalesceCount"),
                QStringLiteral("Coalesce count is outside the configured limit."));
        }
        if (faults.coalesceCount > 1
            && (faults.coalesceWindowMs < 0 || faults.coalesceWindowMs > limits.maxDelayMs)) {
            addError(result, faultPath + QStringLiteral(".coalesceWindowMs"),
                QStringLiteral("Coalesce window is outside the configured limit."));
        }
        if (!faults.fragmentSizes.isEmpty() && faults.coalesceCount > 1) {
            addError(result, faultPath,
                QStringLiteral("Fragmentation and coalescing cannot be enabled together."));
        }
        if (*behavior != SimulationBehavior::Respond && faults.disconnectAfterSend) {
            addError(result, faultPath + QStringLiteral(".disconnectAfterSend"),
                QStringLiteral("DisconnectAfterSend requires a responding rule."));
        }
        if (*behavior == SimulationBehavior::Respond && validPayload) {
            const qint64 adjustedLength = payload.size() + faults.invalidLengthAdjustment;
            if (adjustedLength < 0 || adjustedLength > 65535) {
                addError(result, faultPath + QStringLiteral(".invalidLengthAdjustment"),
                    QStringLiteral("Adjusted length must remain between 0 and 65535."));
            }
            qint64 fragmentTotal = 0;
            for (const int size : faults.fragmentSizes) {
                fragmentTotal += size;
            }
            const qint64 outboundLength = DemoBinaryProtocolCodec::FixedFrameOverhead
                + payload.size() + noise.size();
            if (fragmentTotal > outboundLength) {
                addError(result, faultPath + QStringLiteral(".fragmentSizes"),
                    QStringLiteral("Fragment sizes exceed the outgoing byte length."));
            }
        }
    }

    if (configuration.telemetry.enabled) {
        if (configuration.telemetry.intervalMs < 50
            || configuration.telemetry.intervalMs > 86400000) {
            addError(result, QStringLiteral("$.telemetry.intervalMs"),
                QStringLiteral("Interval must be between 50 ms and 24 hours."));
        }
        if (!ConfigurationValues::parseHexByte(configuration.telemetry.command, nullptr)) {
            addError(result, QStringLiteral("$.telemetry.command"),
                QStringLiteral("Command must be a hexadecimal byte."));
        }
        QByteArray telemetryPayload;
        if (!ConfigurationValues::parseHexBytes(
                configuration.telemetry.payloadHex, &telemetryPayload)) {
            addError(result, QStringLiteral("$.telemetry.payloadHex"),
                QStringLiteral("Payload must contain hexadecimal bytes."));
        } else if (telemetryPayload.size() > limits.maxPayloadLength) {
            addError(result, QStringLiteral("$.telemetry.payloadHex"),
                QStringLiteral("Payload exceeds the configured maximum."));
        }
    }

    const QStringList levels = {
        QStringLiteral("Trace"), QStringLiteral("Debug"), QStringLiteral("Information"),
        QStringLiteral("Warning"), QStringLiteral("Error"), QStringLiteral("None")};
    const bool validLevel = std::any_of(levels.cbegin(), levels.cend(),
        [&configuration](const QString &level) {
            return level.compare(configuration.logging.minimumLevel, Qt::CaseInsensitive) == 0;
        });
    if (!validLevel) {
        addError(result, QStringLiteral("$.logging.minimumLevel"),
            QStringLiteral("Unsupported log level."));
    }
    return result;
}

std::unique_ptr<DeviceSimulatorServer> SimulatorConfigurationFactory::createServer(
    const SimulatorConfiguration &configuration,
    QString *error)
{
    const ConfigurationValidationResult validation = ConfigurationValidator::validate(configuration);
    if (!validation.isValid()) {
        QStringList lines;
        for (const ConfigurationError &item : validation.errors) {
            lines.append(QStringLiteral("%1: %2").arg(item.path, item.message));
        }
        fail(error, QStringLiteral("Simulator configuration is invalid.\n%1")
            .arg(lines.join(QLatin1Char('\n'))));
        return nullptr;
    }

    DeviceSimulatorOptions serverOptions;
    serverOptions.listenAddress = QHostAddress(configuration.server.host);
    serverOptions.port = static_cast<quint16>(configuration.server.port);
    serverOptions.maxPayloadLength = configuration.limits.maxPayloadLength;
    serverOptions.maxBufferedBytes = configuration.limits.maxBufferedBytes;
    serverOptions.maxDelayMs = configuration.limits.maxDelayMs;
    serverOptions.maxFragmentCount = configuration.limits.maxFragmentCount;
    serverOptions.maxNoiseLength = configuration.limits.maxNoiseLength;
    serverOptions.maxCoalesceCount = configuration.limits.maxCoalesceCount;
    serverOptions.randomSeed = configuration.simulation.randomSeed;

    DemoBinaryProtocolOptions protocolOptions;
    protocolOptions.version = static_cast<quint8>(configuration.protocol.version);
    protocolOptions.maxPayloadLength = configuration.limits.maxPayloadLength;
    protocolOptions.maxBufferedBytes = configuration.limits.maxBufferedBytes;

    QList<SimulationRule> rules;
    for (const RuleConfiguration &ruleConfiguration : configuration.rules) {
        quint8 requestCommand = 0;
        ConfigurationValues::parseHexByte(ruleConfiguration.requestCommand, &requestCommand);
        const SimulationBehavior behavior = *parseBehavior(ruleConfiguration.behavior);
        quint8 responseCommand = 0;
        QByteArray responsePayload;
        if (ruleConfiguration.response.has_value()) {
            ConfigurationValues::parseHexByte(ruleConfiguration.response->command, &responseCommand);
            ConfigurationValues::parseHexBytes(ruleConfiguration.response->payloadHex, &responsePayload);
        }

        FaultInjectionOptions faults;
        faults.delayMs = ruleConfiguration.faults.delayMs;
        faults.dropProbability = ruleConfiguration.faults.dropProbability;
        faults.corruptChecksum = ruleConfiguration.faults.corruptChecksum;
        faults.invalidLengthAdjustment = ruleConfiguration.faults.invalidLengthAdjustment;
        ConfigurationValues::parseHexBytes(
            ruleConfiguration.faults.noisePrefixHex, &faults.noisePrefix);
        faults.fragmentSizes = ruleConfiguration.faults.fragmentSizes;
        faults.fragmentDelayMs = ruleConfiguration.faults.fragmentDelayMs;
        faults.coalesceCount = ruleConfiguration.faults.coalesceCount;
        faults.coalesceWindowMs = ruleConfiguration.faults.coalesceWindowMs;
        faults.disconnectAfterSend = ruleConfiguration.faults.disconnectAfterSend;

        rules.append(SimulationRule(requestCommand,
            [behavior, responseCommand, responsePayload, faults](const ProtocolRequest &request) {
                switch (behavior) {
                case SimulationBehavior::Respond:
                    return ResponsePlan::respond(
                        {request.version, responseCommand, request.sequence, responsePayload}, faults);
                case SimulationBehavior::NoResponse:
                    return ResponsePlan::noResponse();
                case SimulationBehavior::DropResponse:
                    return ResponsePlan::dropResponse();
                case SimulationBehavior::Disconnect:
                    return ResponsePlan::disconnect();
                }
                return ResponsePlan::noResponse();
            }));
    }

    std::optional<TelemetryOptions> telemetry;
    if (configuration.telemetry.enabled) {
        quint8 telemetryCommand = 0;
        QByteArray telemetryPayload;
        ConfigurationValues::parseHexByte(configuration.telemetry.command, &telemetryCommand);
        ConfigurationValues::parseHexBytes(configuration.telemetry.payloadHex, &telemetryPayload);
        TelemetryOptions options;
        options.intervalMs = configuration.telemetry.intervalMs;
        options.frameFactory = [version = protocolOptions.version,
                                   telemetryCommand,
                                   telemetryPayload](quint16 sequence) {
            return ProtocolResponse{version, telemetryCommand, sequence, telemetryPayload};
        };
        telemetry = std::move(options);
    }

    return std::make_unique<DeviceSimulatorServer>(
        serverOptions,
        [protocolOptions]() -> std::unique_ptr<AbstractProtocolCodec> {
            return std::make_unique<DemoBinaryProtocolCodec>(protocolOptions);
        },
        std::move(rules),
        DeviceSimulatorServer::UnknownCommandHandler{},
        std::move(telemetry));
}

} // namespace DeviceSimulator
