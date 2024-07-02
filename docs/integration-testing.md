# 集成测试

集成测试使用 Qt Test 和真实 loopback TCP，但端口设为 `0`，让操作系统分配空闲端口。这样不依赖真实设备、公网或固定端口。

## 启动测试 Server

```cpp
DeviceSimulatorOptions options;
options.listenAddress = QHostAddress::LocalHost;
options.port = 0;

DeviceSimulatorServer server(
    options,
    []() { return std::make_unique<DemoBinaryProtocolCodec>(); });

QString error;
QVERIFY2(server.start(&error), qPrintable(error));

QTcpSocket client;
client.connectToHost(QHostAddress::LocalHost, server.serverPort());
QVERIFY(client.waitForConnected(3000));
```

JSON 配置不接受端口 `0`，只有代码方式可以这样用。

## 半包和粘包

TCP 是字节流，不要把一次 `readyRead` 当成一条完整协议帧。

- 半包测试：把同一请求拆成几次 `write()`。
- 粘包测试：把多条请求拼在同一个 `QByteArray` 里一次写出。
- 响应解析：把每次 `readAll()` 的结果交给同一个 Codec，直到拿到完整帧。

同线程测试需要持续处理 Qt 事件循环，不能长时间阻塞，否则 Server 的 `readyRead` 也得不到执行。

## 运行

```shell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
