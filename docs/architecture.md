# 架构说明

项目由 Core、CLI 和 Tests 三部分组成。Core 负责协议、规则、TCP、故障注入、Telemetry 和配置，不直接写控制台；CLI 只处理参数、日志和 Ctrl+C。

## 协议

`AbstractProtocolCodec` 隔离字节流解析与 Server。每个客户端连接都会创建新的 Codec，因此半包缓存不会跨连接保留。

`DemoBinaryProtocolCodec` 负责：

- 编码和解析 `AA 55` Demo 帧。
- 保存未完成的半包。
- 从噪声、坏 CRC、非法长度和错误版本中恢复。
- 为故障注入修改 CRC 和长度字段。

## TCP 和规则

`DeviceSimulatorServer` 使用 `QTcpServer` 和 `QTcpSocket`。同一时间只保留一个客户端，连接断开后继续等待下一次连接。

完整请求进入规则队列后按顺序处理。规则用命令字匹配，并生成 `ResponsePlan`。未知命令默认返回 `0xFF`，Payload 是 `01 <原命令字>`。

## 发送和 Telemetry

普通响应和 Telemetry 共用容量为 128 的输出队列，避免两条帧在分片过程中交叉写入。

- 延迟使用 `QTimer::singleShot`。
- 分片使用单次 `QTimer` 逐段发送。
- 合并发送在有上限的窗口内等待兼容输出。
- Telemetry 使用每个连接自己的 `QTimer` 和序列号。

客户端断开时，相关 Timer、请求、发送队列、Codec 和随机状态都会清理。

## 配置

`ConfigurationLoader` 使用 Qt JSON API 读取文件，并手动拒绝未知字段和错误类型。`ConfigurationValidator` 再检查 Hex、重复命令、行为组合和资源边界。

配置通过后，`SimulatorConfigurationFactory` 才创建 Server。CLI 配置端口必须在 `1..65535`；代码方式允许端口 `0`，方便测试使用动态端口。
