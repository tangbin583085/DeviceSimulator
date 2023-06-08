#include <DeviceSimulator/Configuration.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <memory>

using namespace DeviceSimulator;

namespace {

volatile std::sig_atomic_t StopRequested = 0;

void requestStop(int)
{
    StopRequested = 1;
}

int logLevel(const QString &name)
{
    if (name.compare(QStringLiteral("Trace"), Qt::CaseInsensitive) == 0) {
        return 0;
    }
    if (name.compare(QStringLiteral("Debug"), Qt::CaseInsensitive) == 0) {
        return 1;
    }
    if (name.compare(QStringLiteral("Information"), Qt::CaseInsensitive) == 0) {
        return 2;
    }
    if (name.compare(QStringLiteral("Warning"), Qt::CaseInsensitive) == 0) {
        return 3;
    }
    if (name.compare(QStringLiteral("Error"), Qt::CaseInsensitive) == 0) {
        return 4;
    }
    return 5;
}

int eventLevel(const QString &eventType)
{
    if (eventType == QStringLiteral("NetworkError")) {
        return 4;
    }
    if (eventType == QStringLiteral("ProtocolError")
        || eventType == QStringLiteral("ChecksumCorrupted")
        || eventType == QStringLiteral("InvalidLengthInjected")) {
        return 3;
    }
    return 2;
}

bool loadAndValidate(
    const QString &path,
    SimulatorConfiguration *configuration,
    QTextStream &errorOutput)
{
    QString loadError;
    if (!ConfigurationLoader::load(path, configuration, &loadError)) {
        errorOutput << loadError << '\n';
        return false;
    }

    const ConfigurationValidationResult validation =
        ConfigurationValidator::validate(*configuration);
    if (validation.isValid()) {
        return true;
    }

    errorOutput << QStringLiteral("Configuration is invalid:\n");
    for (const ConfigurationError &item : validation.errors) {
        errorOutput << item.path << QStringLiteral(": ") << item.message << '\n';
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("device-simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("tangbin"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt TCP device simulator"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("run, validate, sample-config, version or help"));
    const QCommandLineOption configOption(
        QStringList{QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Configuration JSON path."),
        QStringLiteral("path"));
    parser.addOption(configOption);
    parser.process(application);

    QTextStream output(stdout);
    QTextStream errorOutput(stderr);
    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1) {
        errorOutput << QStringLiteral("A command is required.\n\n") << parser.helpText();
        return 1;
    }

    const QString command = positional.first().toLower();
    const bool requiresConfig = command == QStringLiteral("run")
        || command == QStringLiteral("validate");
    if (requiresConfig && !parser.isSet(configOption)) {
        errorOutput << QStringLiteral("The %1 command requires --config <path>.\n")
                           .arg(command);
        return 1;
    }
    if (!requiresConfig && parser.isSet(configOption)) {
        errorOutput << QStringLiteral("The %1 command does not accept --config.\n")
                           .arg(command);
        return 1;
    }

    if (command == QStringLiteral("version")) {
        output << QCoreApplication::applicationVersion() << '\n';
        return 0;
    }
    if (command == QStringLiteral("help")) {
        output << parser.helpText();
        return 0;
    }
    if (command == QStringLiteral("sample-config")) {
        QFile sample(QStringLiteral(":/samples/simulator.sample.json"));
        if (!sample.open(QIODevice::ReadOnly)) {
            errorOutput << QStringLiteral("Embedded sample configuration is unavailable.\n");
            return 2;
        }
        output << QString::fromUtf8(sample.readAll());
        return 0;
    }
    if (command != QStringLiteral("run") && command != QStringLiteral("validate")) {
        errorOutput << QStringLiteral("Unknown command '%1'.\n\n").arg(command)
                    << parser.helpText();
        return 1;
    }

    SimulatorConfiguration configuration;
    if (!loadAndValidate(parser.value(configOption), &configuration, errorOutput)) {
        return 2;
    }
    if (command == QStringLiteral("validate")) {
        output << QStringLiteral("Configuration is valid.\n");
        return 0;
    }

    QString factoryError;
    std::unique_ptr<DeviceSimulatorServer> server =
        SimulatorConfigurationFactory::createServer(configuration, &factoryError);
    if (!server) {
        errorOutput << factoryError << '\n';
        return 2;
    }

    const int minimumLogLevel = logLevel(configuration.logging.minimumLevel);
    QObject::connect(server.get(), &DeviceSimulatorServer::eventOccurred,
        &application,
        [&output, minimumLogLevel](const QString &eventType, const QString &message) {
            if (eventLevel(eventType) < minimumLogLevel || minimumLogLevel >= 5) {
                return;
            }
            output << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                   << QStringLiteral(" [") << eventType << QStringLiteral("] ")
                   << message << '\n';
            output.flush();
        });
    QObject::connect(&application, &QCoreApplication::aboutToQuit,
        server.get(), &DeviceSimulatorServer::stop);

    output << QStringLiteral("Listen: %1:%2\n")
                  .arg(configuration.server.host)
                  .arg(configuration.server.port);
    output << QStringLiteral("Protocol: %1\n").arg(configuration.protocol.name);
    output << QStringLiteral("Rules: %1\n").arg(configuration.rules.size());
    output << QStringLiteral("Telemetry: %1\n")
                  .arg(configuration.telemetry.enabled
                          ? QStringLiteral("enabled")
                          : QStringLiteral("disabled"));
    if (configuration.server.host == QStringLiteral("0.0.0.0")) {
        output << QStringLiteral(
            "Warning: listening on 0.0.0.0 exposes the simulator to other hosts.\n");
    }
    output.flush();

    QString startError;
    if (!server->start(&startError)) {
        errorOutput << startError << '\n';
        return 2;
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    QTimer signalTimer;
    signalTimer.setInterval(100);
    QObject::connect(&signalTimer, &QTimer::timeout, &application, [&application]() {
        if (StopRequested != 0) {
            StopRequested = 0;
            application.quit();
        }
    });
    signalTimer.start();

    const int exitCode = application.exec();
    output << QStringLiteral("Simulator stopped.\n");
    return exitCode;
}
