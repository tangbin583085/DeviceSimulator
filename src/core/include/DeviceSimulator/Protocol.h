#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace DeviceSimulator {

struct ProtocolRequest
{
    quint8 version = 0;
    quint8 command = 0;
    quint16 sequence = 0;
    QByteArray payload;
    QByteArray rawData;
};

struct ProtocolResponse
{
    quint8 version = 0;
    quint8 command = 0;
    quint16 sequence = 0;
    QByteArray payload;
};

struct ProtocolDiagnostic
{
    QString code;
    QString message;
    qsizetype discardedByteCount = 0;
};

struct ProtocolDecodeResult
{
    QList<ProtocolRequest> requests;
    QList<ProtocolDiagnostic> diagnostics;
};

class AbstractProtocolCodec
{
public:
    virtual ~AbstractProtocolCodec() = default;

    [[nodiscard]] virtual QString name() const = 0;
    virtual ProtocolDecodeResult append(QByteArrayView data) = 0;
    [[nodiscard]] virtual QByteArray encode(const ProtocolResponse &response) const = 0;
    virtual void reset() = 0;
};

} // namespace DeviceSimulator

Q_DECLARE_METATYPE(DeviceSimulator::ProtocolRequest)
Q_DECLARE_METATYPE(DeviceSimulator::ProtocolResponse)
Q_DECLARE_METATYPE(DeviceSimulator::ProtocolDiagnostic)
