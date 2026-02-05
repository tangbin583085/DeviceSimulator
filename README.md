# DeviceSimulator

## 中文说明

DeviceSimulator 是一个用 Qt 6 和 C++20 编写的轻量 TCP 设备模拟器，主要给上位机开发、二进制协议调试、自动化测试和设备联调使用。

项目是纯命令行程序，只依赖 `Qt Core`、`Qt Network` 和 `Qt Test`，不使用 Widgets 或 QML。当前版本保持单客户端设计，不打算扩展成设备管理平台。

## 目录

- `src/core`：协议、规则、TCP Server、故障注入、Telemetry 和 JSON 配置。
- `src/cli`：命令行入口和内置样例配置。
- `tests`：协议、配置、TCP、故障和 Telemetry 测试。
- `samples`：可直接使用的 JSON 配置。
- `docs`：协议、配置、架构、测试和发布说明。

## 构建

需要 Qt 6.2 或更高版本、CMake 3.21 或更高版本，以及支持 C++20 的编译器。

```shell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

生成的命令行程序通常位于：

```text
build/src/cli/device-simulator
```

## 使用

先检查样例配置：

```shell
./build/src/cli/device-simulator validate \
  --config samples/simulator.sample.json
```

启动模拟器：

```shell
./build/src/cli/device-simulator run \
  --config samples/simulator.sample.json
```

其他命令：

```shell
./build/src/cli/device-simulator sample-config
./build/src/cli/device-simulator version
./build/src/cli/device-simulator help
```

Ctrl+C 会停止监听并关闭当前连接。命令成功返回 `0`，参数错误返回 `1`，文件、配置或启动错误返回 `2`。

## 主要功能

- `AA 55` Demo 二进制协议和 CRC16-Modbus。
- 连续字节流解析，支持半包、粘包和错误数据恢复。
- 同一时间只服务一个客户端，断开后继续等待下一次连接。
- 按命令字匹配响应，未知命令返回 `0xFF` 和 `01 <原命令字>`。
- 延迟、无响应、丢弃、CRC 破坏、长度修改、前置噪声、分片、合并和发送后断线。
- 固定随机种子，概率故障可以重复。
- 每个连接独立的周期 Telemetry，重连后序列号重新开始。
- 严格 JSON 配置，拒绝未知字段、错误类型、非法 Hex、注释和尾随逗号。

普通响应和 Telemetry 共用一个有界发送队列。分片发送时不会穿插其他帧；合并发送只收集兼容的相邻输出。

## 配置

完整配置见 [samples/simulator.sample.json](samples/simulator.sample.json)。顶层字段包括：

- `server`：监听 IP 和端口。
- `protocol`：协议名称和版本。
- `limits`：Payload、缓存、延迟、分片、噪声和合并上限。
- `simulation`：概率故障使用的固定随机种子。
- `rules`：请求命令、响应和故障参数。
- `telemetry`：周期上报设置。
- `logging`：CLI 最低日志等级。

CLI 配置端口必须在 `1..65535`。代码方式可以把端口设为 `0`，让操作系统自动分配端口，主要用于测试。

字段说明见 [docs/configuration.md](docs/configuration.md)，故障处理顺序见 [docs/fault-injection.md](docs/fault-injection.md)。

## Demo 协议

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Header `AA 55` |
| 2 | 1 | Version |
| 3 | 1 | Command |
| 4 | 2 | Sequence，大端 UInt16 |
| 6 | 2 | Payload length，大端 UInt16 |
| 8 | N | Payload |
| 8 + N | 2 | CRC16-Modbus，低字节在前 |

CRC 从 Version 开始计算到 Payload 最后一个字节，不包含 Header 和 CRC 自身。详细说明见 [docs/sample-protocol.md](docs/sample-protocol.md)。

## 测试

测试使用 Qt Test，TCP 集成测试只连接 loopback，并使用动态端口，不需要真实设备或公网。

```shell
ctest --test-dir build --output-on-failure
```

项目可以在具备 Qt 和 CMake 的环境中运行完整构建和测试。

## 安全提醒

默认只监听 `127.0.0.1`。配置成 `0.0.0.0` 后，同一网络里的其他机器也可能连接，只建议在可信网络中使用。

JSON 只描述数据，不执行脚本、外部命令或动态加载代码。不要把真实公司的私有协议、密钥、生产地址或客户数据提交到仓库。

## License

Copyright `tangbin`，MIT License。见 [LICENSE](LICENSE)。

---

## English

DeviceSimulator is a small TCP device simulator written with Qt 6 and C++20.
It is mainly used while developing desktop tools, testing a binary protocol,
or running integration tests without a real device.

The project is command-line only. It uses Qt Core, Qt Network, and Qt Test.
The current server accepts one client at a time. When that client disconnects,
the server goes back to listening for the next connection.

### Project layout

- `src/core`: protocol codec, rules, TCP server, faults, telemetry, and configuration.
- `src/cli`: command-line program and embedded sample configuration.
- `tests`: protocol, configuration, TCP, and behavior tests.
- `samples`: example JSON configuration.
- `docs`: notes about the protocol, configuration, and testing.

### Build

You need Qt 6.2 or newer, CMake 3.21 or newer, and a compiler with C++20 support.

```shell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The executable is usually created at:

```text
build/src/cli/device-simulator
```

### Run

Check the sample configuration first:

```shell
./build/src/cli/device-simulator validate \
  --config samples/simulator.sample.json
```

Start the simulator:

```shell
./build/src/cli/device-simulator run \
  --config samples/simulator.sample.json
```

The CLI also provides `sample-config`, `version`, and `help` commands. Press
Ctrl+C to stop the server.

### Main features

- A small `AA 55` binary demo protocol with CRC16-Modbus.
- Stream parsing for partial frames, combined frames, and invalid input.
- Responses selected by command rules.
- Delay, dropped responses, bad CRC, invalid length, noise, fragmentation,
  coalescing, and disconnect-after-send behavior.
- Repeatable random faults with a fixed seed.
- Periodic telemetry for each client connection.
- JSON configuration with type and range checks.

The demo protocol is only included so the project can be run and tested. It is
not based on a real device. A different protocol can be added by implementing
the codec interface.

### Configuration

See [samples/simulator.sample.json](samples/simulator.sample.json) for a complete
example. It contains the listen address, protocol settings, limits, response
rules, fault options, telemetry, and logging level.

More details are available in [docs/configuration.md](docs/configuration.md) and
[docs/fault-injection.md](docs/fault-injection.md).

### Tests

The tests use Qt Test and connect only to the local loopback address. They do not
need a real device or an Internet connection.

```shell
ctest --test-dir build --output-on-failure
```

### Safety note

The default listen address is `127.0.0.1`. Using `0.0.0.0` makes the simulator
reachable by other machines on the same network, so use that setting only on a
network you trust.

Do not put private protocols, keys, production addresses, or customer data in a
public configuration file.

### License

MIT License. See [LICENSE](LICENSE).
