#pragma once

#include <DeviceSimulator/Protocol.h>

#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace DeviceSimulator {

enum class SimulationBehavior
{
    Respond,
    NoResponse,
    DropResponse,
    Disconnect,
};

struct FaultInjectionOptions
{
    int delayMs = 0;
    double dropProbability = 0.0;
    bool corruptChecksum = false;
    int invalidLengthAdjustment = 0;
    QByteArray noisePrefix;
    QList<int> fragmentSizes;
    int fragmentDelayMs = 0;
    int coalesceCount = 1;
    int coalesceWindowMs = 50;
    bool disconnectAfterSend = false;

    [[nodiscard]] bool isValid(QString *error = nullptr) const
    {
        const auto fail = [error](const QString &message) {
            if (error != nullptr) {
                *error = message;
            }
            return false;
        };

        if (delayMs < 0 || fragmentDelayMs < 0 || coalesceWindowMs < 0) {
            return fail(QStringLiteral("Delay values cannot be negative."));
        }
        if (!std::isfinite(dropProbability)
            || dropProbability < 0.0
            || dropProbability > 1.0) {
            return fail(QStringLiteral("Drop probability must be between 0 and 1."));
        }
        if (coalesceCount < 1) {
            return fail(QStringLiteral("Coalesce count must be at least 1."));
        }
        for (const int size : fragmentSizes) {
            if (size <= 0) {
                return fail(QStringLiteral("Fragment sizes must be greater than zero."));
            }
        }
        if (!fragmentSizes.isEmpty() && coalesceCount > 1) {
            return fail(QStringLiteral("Fragmentation and coalescing cannot be combined."));
        }

        return true;
    }
};

struct ResponsePlan
{
    SimulationBehavior behavior = SimulationBehavior::NoResponse;
    std::optional<ProtocolResponse> response;
    FaultInjectionOptions faults;

    [[nodiscard]] static ResponsePlan respond(
        ProtocolResponse response,
        FaultInjectionOptions faults = {})
    {
        return {SimulationBehavior::Respond, std::move(response), std::move(faults)};
    }

    [[nodiscard]] static ResponsePlan noResponse()
    {
        return {SimulationBehavior::NoResponse, std::nullopt, {}};
    }

    [[nodiscard]] static ResponsePlan dropResponse()
    {
        return {SimulationBehavior::DropResponse, std::nullopt, {}};
    }

    [[nodiscard]] static ResponsePlan disconnect()
    {
        return {SimulationBehavior::Disconnect, std::nullopt, {}};
    }
};

class SimulationRule
{
public:
    using Handler = std::function<ResponsePlan(const ProtocolRequest &)>;
    using AdditionalMatch = std::function<bool(const ProtocolRequest &)>;

    SimulationRule(
        quint8 requestCommand,
        Handler handler,
        AdditionalMatch additionalMatch = {})
        : m_requestCommand(requestCommand)
        , m_handler(std::move(handler))
        , m_additionalMatch(std::move(additionalMatch))
    {
    }

    [[nodiscard]] quint8 requestCommand() const
    {
        return m_requestCommand;
    }

    [[nodiscard]] bool matches(const ProtocolRequest &request) const
    {
        return request.command == m_requestCommand
            && (!m_additionalMatch || m_additionalMatch(request));
    }

    [[nodiscard]] ResponsePlan createResponse(const ProtocolRequest &request) const
    {
        return m_handler ? m_handler(request) : ResponsePlan::noResponse();
    }

private:
    quint8 m_requestCommand;
    Handler m_handler;
    AdditionalMatch m_additionalMatch;
};

class RuleMatcher
{
public:
    [[nodiscard]] static const SimulationRule *findMatch(
        const QList<SimulationRule> &rules,
        const ProtocolRequest &request)
    {
        for (const SimulationRule &rule : rules) {
            if (rule.matches(request)) {
                return &rule;
            }
        }
        return nullptr;
    }
};

} // namespace DeviceSimulator
