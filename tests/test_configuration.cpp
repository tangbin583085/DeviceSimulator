#include <DeviceSimulator/Configuration.h>

#include <QtTest>

#include <algorithm>

using namespace DeviceSimulator;

class ConfigurationTests final : public QObject
{
    Q_OBJECT

private slots:
    void defaultConfigurationIsValid()
    {
        const ConfigurationValidationResult result =
            ConfigurationValidator::validate(SimulatorConfiguration{});
        QVERIFY(result.isValid());
    }

    void validJsonLoadsAndBuildsServer()
    {
        const QByteArray json = R"json({
          "server": {"host": "127.0.0.1", "port": 9000},
          "protocol": {"name": "DemoBinaryProtocol", "version": 1},
          "rules": [{
            "requestCommand": "0x01",
            "behavior": "Respond",
            "response": {"command": "0x81", "payloadHex": "01 02 03"},
            "faults": {"delayMs": 10}
          }],
          "telemetry": {
            "enabled": true,
            "intervalMs": 1000,
            "command": "0x90",
            "payloadHex": "00 FA"
          }
        })json";

        SimulatorConfiguration configuration;
        QString error;
        QVERIFY2(ConfigurationLoader::fromJson(json, &configuration, &error), qPrintable(error));
        QVERIFY(ConfigurationValidator::validate(configuration).isValid());
        QVERIFY2(SimulatorConfigurationFactory::createServer(configuration, &error) != nullptr,
            qPrintable(error));
    }

    void unknownPropertyIsRejected()
    {
        SimulatorConfiguration configuration;
        QString error;
        QVERIFY(!ConfigurationLoader::fromJson(
            QByteArrayLiteral(R"json({"server":{"unexpected":true}})json"),
            &configuration,
            &error));
        QVERIFY(error.contains(QStringLiteral("unknown property")));
    }

    void malformedJsonIsRejected()
    {
        SimulatorConfiguration configuration;
        QString error;
        QVERIFY(!ConfigurationLoader::fromJson(
            QByteArrayLiteral("{ invalid json"), &configuration, &error));
        QVERIFY(error.contains(QStringLiteral("invalid"), Qt::CaseInsensitive));
    }

    void invalidHexAndDuplicateRulesAreReported()
    {
        SimulatorConfiguration configuration;
        RuleConfiguration first;
        first.requestCommand = QStringLiteral("0x01");
        first.response = ResponseConfiguration{QStringLiteral("0x81"), QStringLiteral("GG")};
        RuleConfiguration second;
        second.requestCommand = QStringLiteral("01");
        second.behavior = QStringLiteral("NoResponse");
        configuration.rules = {first, second};

        const ConfigurationValidationResult result =
            ConfigurationValidator::validate(configuration);
        QVERIFY(!result.isValid());
        QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(),
            [](const ConfigurationError &item) {
                return item.path.endsWith(QStringLiteral("payloadHex"));
            }));
        QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(),
            [](const ConfigurationError &item) {
                return item.message.contains(QStringLiteral("Duplicate"));
            }));
    }

    void cliPortZeroIsRejected()
    {
        SimulatorConfiguration configuration;
        configuration.server.port = 0;
        const ConfigurationValidationResult result =
            ConfigurationValidator::validate(configuration);
        QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(),
            [](const ConfigurationError &item) {
                return item.path == QStringLiteral("$.server.port");
            }));
    }
};

QTEST_APPLESS_MAIN(ConfigurationTests)

#include "test_configuration.moc"
