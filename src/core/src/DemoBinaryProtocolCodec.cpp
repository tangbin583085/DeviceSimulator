#include <DeviceSimulator/DemoBinaryProtocolCodec.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace DeviceSimulator {
namespace {

constexpr quint8 HeaderFirst = 0xAA;
constexpr quint8 HeaderSecond = 0x55;
constexpr qsizetype VersionOffset = 2;
constexpr qsizetype CommandOffset = 3;
constexpr qsizetype SequenceOffset = 4;
constexpr qsizetype PayloadLengthOffset = 6;
constexpr qsizetype PayloadOffset = 8;

quint8 byteAt(const QByteArray &data, qsizetype index)
{
    return static_cast<quint8>(static_cast<unsigned char>(data.at(index)));
}

quint16 readBigEndianUInt16(const QByteArray &data, qsizetype offset)
{
    return static_cast<quint16>((static_cast<quint16>(byteAt(data, offset)) << 8)
        | byteAt(data, offset + 1));
}

void writeBigEndianUInt16(QByteArray &data, qsizetype offset, quint16 value)
{
    data[offset] = static_cast<char>((value >> 8) & 0xFF);
    data[offset + 1] = static_cast<char>(value & 0xFF);
}

qsizetype findHeader(const QByteArray &data)
{
    for (qsizetype index = 0; index + 1 < data.size(); ++index) {
        if (byteAt(data, index) == HeaderFirst
            && byteAt(data, index + 1) == HeaderSecond) {
            return index;
        }
    }
    return -1;
}

} // namespace

bool DemoBinaryProtocolOptions::isValid(QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (maxPayloadLength < 0
        || maxPayloadLength > std::numeric_limits<quint16>::max()) {
        return fail(QStringLiteral("Maximum payload length must be between 0 and 65535."));
    }
    if (maxBufferedBytes < maxPayloadLength + DemoBinaryProtocolCodec::FixedFrameOverhead
        || maxBufferedBytes > 1024 * 1024) {
        return fail(QStringLiteral(
            "Maximum buffered bytes must hold one full frame and cannot exceed 1 MiB."));
    }
    return true;
}

DemoBinaryProtocolCodec::DemoBinaryProtocolCodec(DemoBinaryProtocolOptions options)
    : m_options(options)
{
    QString error;
    if (!m_options.isValid(&error)) {
        throw std::invalid_argument(error.toStdString());
    }
    m_buffer.reserve(std::min<qsizetype>(m_options.maxBufferedBytes, 64));
}

QString DemoBinaryProtocolCodec::name() const
{
    return QStringLiteral("DemoBinaryProtocol");
}

ProtocolDecodeResult DemoBinaryProtocolCodec::append(QByteArrayView data)
{
    ProtocolDecodeResult result;
    qsizetype offset = 0;

    while (offset < data.size()) {
        parseAvailable(result);

        if (m_buffer.size() == m_options.maxBufferedBytes) {
            m_buffer.remove(0, 1);
            result.diagnostics.append({
                QStringLiteral("BufferLimitExceeded"),
                QStringLiteral("Protocol buffer was full and discarded one byte."),
                1,
            });
            continue;
        }

        const qsizetype room = m_options.maxBufferedBytes - m_buffer.size();
        const qsizetype count = std::min(room, data.size() - offset);
        m_buffer.append(data.data() + offset, count);
        offset += count;
    }

    parseAvailable(result);
    return result;
}

QByteArray DemoBinaryProtocolCodec::encode(const ProtocolResponse &response) const
{
    if (response.payload.size() > m_options.maxPayloadLength) {
        throw std::invalid_argument("Response payload exceeds the configured maximum.");
    }

    const qsizetype frameLength = FixedFrameOverhead + response.payload.size();
    QByteArray frame(frameLength, '\0');
    frame[0] = static_cast<char>(HeaderFirst);
    frame[1] = static_cast<char>(HeaderSecond);
    frame[VersionOffset] = static_cast<char>(response.version);
    frame[CommandOffset] = static_cast<char>(response.command);
    writeBigEndianUInt16(frame, SequenceOffset, response.sequence);
    writeBigEndianUInt16(
        frame,
        PayloadLengthOffset,
        static_cast<quint16>(response.payload.size()));
    if (!response.payload.isEmpty()) {
        std::copy(
            response.payload.cbegin(),
            response.payload.cend(),
            frame.begin() + PayloadOffset);
    }

    const quint16 crc = calculateCrc(
        QByteArrayView(frame.constData() + VersionOffset, frameLength - 4));
    frame[frameLength - 2] = static_cast<char>(crc & 0xFF);
    frame[frameLength - 1] = static_cast<char>((crc >> 8) & 0xFF);
    return frame;
}

void DemoBinaryProtocolCodec::reset()
{
    m_buffer.clear();
}

const DemoBinaryProtocolOptions &DemoBinaryProtocolCodec::options() const
{
    return m_options;
}

qsizetype DemoBinaryProtocolCodec::bufferedByteCount() const
{
    return m_buffer.size();
}

QByteArray DemoBinaryProtocolCodec::corruptChecksum(QByteArrayView encodedFrame) const
{
    if (encodedFrame.size() < FixedFrameOverhead) {
        throw std::invalid_argument("Encoded frame is too short.");
    }
    QByteArray corrupted(encodedFrame.data(), encodedFrame.size());
    corrupted[corrupted.size() - 1] = static_cast<char>(
        static_cast<unsigned char>(corrupted.at(corrupted.size() - 1)) ^ 0x01);
    return corrupted;
}

QByteArray DemoBinaryProtocolCodec::adjustPayloadLength(
    QByteArrayView encodedFrame,
    int adjustment) const
{
    if (encodedFrame.size() < FixedFrameOverhead) {
        throw std::invalid_argument("Encoded frame is too short.");
    }

    QByteArray adjusted(encodedFrame.data(), encodedFrame.size());
    const qint64 originalLength = readBigEndianUInt16(adjusted, PayloadLengthOffset);
    const qint64 newLength = originalLength + adjustment;
    if (newLength < 0 || newLength > std::numeric_limits<quint16>::max()) {
        throw std::out_of_range("Adjusted payload length must remain a UInt16 value.");
    }
    writeBigEndianUInt16(adjusted, PayloadLengthOffset, static_cast<quint16>(newLength));
    return adjusted;
}

quint16 DemoBinaryProtocolCodec::calculateCrc(QByteArrayView data)
{
    quint16 crc = 0xFFFF;
    for (const char rawValue : data) {
        crc ^= static_cast<quint8>(static_cast<unsigned char>(rawValue));
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0
                ? static_cast<quint16>((crc >> 1) ^ 0xA001)
                : static_cast<quint16>(crc >> 1);
        }
    }
    return crc;
}

void DemoBinaryProtocolCodec::parseAvailable(ProtocolDecodeResult &result)
{
    while (!m_buffer.isEmpty()) {
        const qsizetype headerIndex = findHeader(m_buffer);
        if (headerIndex < 0) {
            const qsizetype retained = byteAt(m_buffer, m_buffer.size() - 1) == HeaderFirst ? 1 : 0;
            const qsizetype discarded = m_buffer.size() - retained;
            if (discarded > 0) {
                m_buffer.remove(0, discarded);
                result.diagnostics.append({
                    QStringLiteral("NoiseDiscarded"),
                    QStringLiteral("Bytes before the next protocol header were discarded."),
                    discarded,
                });
            }
            return;
        }

        if (headerIndex > 0) {
            m_buffer.remove(0, headerIndex);
            result.diagnostics.append({
                QStringLiteral("NoiseDiscarded"),
                QStringLiteral("Bytes before the protocol header were discarded."),
                headerIndex,
            });
            continue;
        }

        if (m_buffer.size() < PayloadOffset) {
            return;
        }

        const quint16 payloadLength = readBigEndianUInt16(m_buffer, PayloadLengthOffset);
        if (payloadLength > m_options.maxPayloadLength) {
            m_buffer.remove(0, 1);
            result.diagnostics.append({
                QStringLiteral("InvalidFrameLength"),
                QStringLiteral("Payload length exceeds the configured maximum."),
                1,
            });
            continue;
        }

        const qsizetype frameLength = FixedFrameOverhead + payloadLength;
        if (m_buffer.size() < frameLength) {
            return;
        }

        const QByteArray rawData = m_buffer.first(frameLength);
        const quint16 expectedCrc = static_cast<quint16>(
            byteAt(rawData, frameLength - 2)
            | (static_cast<quint16>(byteAt(rawData, frameLength - 1)) << 8));
        const quint16 actualCrc = calculateCrc(
            QByteArrayView(rawData.constData() + VersionOffset, frameLength - 4));
        if (expectedCrc != actualCrc) {
            m_buffer.remove(0, 1);
            result.diagnostics.append({
                QStringLiteral("ChecksumMismatch"),
                QStringLiteral("CRC16-Modbus check failed."),
                1,
            });
            continue;
        }

        const quint8 version = byteAt(rawData, VersionOffset);
        if (version != m_options.version) {
            m_buffer.remove(0, frameLength);
            result.diagnostics.append({
                QStringLiteral("InvalidVersion"),
                QStringLiteral("Protocol version is not supported."),
                frameLength,
            });
            continue;
        }

        result.requests.append({
            version,
            byteAt(rawData, CommandOffset),
            readBigEndianUInt16(rawData, SequenceOffset),
            rawData.mid(PayloadOffset, payloadLength),
            rawData,
        });
        m_buffer.remove(0, frameLength);
    }
}

} // namespace DeviceSimulator
