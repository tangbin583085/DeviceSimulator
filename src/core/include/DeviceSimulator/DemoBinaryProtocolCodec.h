#pragma once

#include <DeviceSimulator/Protocol.h>

namespace DeviceSimulator {

struct DemoBinaryProtocolOptions
{
    quint8 version = 1;
    qsizetype maxPayloadLength = 4096;
    qsizetype maxBufferedBytes = 8192;

    [[nodiscard]] bool isValid(QString *error = nullptr) const;
};

class DemoBinaryProtocolCodec final : public AbstractProtocolCodec
{
public:
    static constexpr qsizetype FixedFrameOverhead = 10;

    explicit DemoBinaryProtocolCodec(DemoBinaryProtocolOptions options = {});

    [[nodiscard]] QString name() const override;
    ProtocolDecodeResult append(QByteArrayView data) override;
    [[nodiscard]] QByteArray encode(const ProtocolResponse &response) const override;
    void reset() override;

    [[nodiscard]] const DemoBinaryProtocolOptions &options() const;
    [[nodiscard]] qsizetype bufferedByteCount() const;
    [[nodiscard]] QByteArray corruptChecksum(QByteArrayView encodedFrame) const;
    [[nodiscard]] QByteArray adjustPayloadLength(
        QByteArrayView encodedFrame,
        int adjustment) const;

    [[nodiscard]] static quint16 calculateCrc(QByteArrayView data);

private:
    void parseAvailable(ProtocolDecodeResult &result);

    DemoBinaryProtocolOptions m_options;
    QByteArray m_buffer;
};

} // namespace DeviceSimulator
