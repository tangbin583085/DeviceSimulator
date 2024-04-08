# Demo 协议

项目自带一套完全虚构的二进制协议，只用于示例和测试，不对应任何真实公司的设备。

## 帧结构

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Header `AA 55` |
| 2 | 1 | Version |
| 3 | 1 | Command |
| 4 | 2 | Sequence，大端 UInt16 |
| 6 | 2 | Payload length，大端 UInt16 |
| 8 | N | Payload |
| 8 + N | 2 | CRC16-Modbus，低字节在前 |

完整帧长度是 `10 + Payload length`。CRC 从 Version 开始算到 Payload 最后一个字节，不包含 Header 和 CRC 字段。

## 示例命令

| Command | 含义 |
| ---: | --- |
| `0x01` | 读取设备信息 |
| `0x02` | 开始采集 |
| `0x03` | 停止采集 |
| `0x04` | 读取当前数据 |
| `0x05` | 模拟重启，响应后断开 |
| `0x90` | 周期上报 |
| `0xFF` | 未知命令默认响应 |

Core 用 `ProtocolResponse` 表示待编码的数据。测试客户端也复用 Codec 构造请求，避免再复制一套 CRC 代码。

## 一条空 Payload 请求

Command 为 `0x01`、Sequence 为 `0x0001` 时，帧大致是：

```text
AA 55 01 01 00 01 00 00 CRC_LO CRC_HI
```

响应由 `DemoBinaryProtocolCodec` 继续按同一规则解析。
