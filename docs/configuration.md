# JSON 配置

CLI 使用普通 JSON 配置。下面这条命令只检查配置，不会监听端口：

```shell
./build/src/cli/device-simulator validate --config simulator.json
```

## 顶层结构

| Section | 用途 |
| --- | --- |
| `server` | 监听 IP 和端口 |
| `protocol` | `DemoBinaryProtocol` 和版本号 |
| `limits` | Payload、缓存、延迟、分片、噪声和合并上限 |
| `simulation` | 固定随机种子 |
| `rules` | 按请求命令字匹配响应 |
| `telemetry` | 周期上报 |
| `logging` | CLI 最低日志等级 |

完整样例在 [../samples/simulator.sample.json](../samples/simulator.sample.json)。未知字段、JSON 注释、尾随逗号、非法 Hex、重复规则和超出限制的值都会报错。

## Hex 写法

每个字节用空格隔开：

```json
{
  "command": "0x81",
  "payloadHex": "01 02 FF"
}
```

空 Payload 可以写成空字符串。配置里不支持表达式和脚本，也不会执行外部命令。

## Rules

一个 `requestCommand` 只能出现一次。

- `Respond`：发送 `response`，并按需叠加 faults。
- `NoResponse`：收到请求，但什么都不发。
- `DropResponse`：把准备好的响应丢掉。
- `Disconnect`：直接关闭当前连接。

分片大小针对完整发送数据，前置噪声也包含在内。分片和合并不能在同一条响应上同时启用。

## Telemetry

`telemetry.enabled` 为 `true` 时才会上报。`intervalMs` 最小 50 毫秒，最大 24 小时。JSON 里使用固定 command 和 Payload；需要动态内容时，改用代码里的 `TelemetryOptions::FrameFactory`。

## Exit code

- `0`：执行成功。
- `1`：CLI 参数不对。
- `2`：文件、JSON、配置或启动过程出错。
