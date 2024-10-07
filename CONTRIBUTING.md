# 参与开发

DeviceSimulator 只想做一个小而实用的 TCP 设备模拟器。改代码时请保持现有边界：单客户端、协议与网络分开、资源有上限、概率故障可重复、JSON 只保存数据。

## 提交前检查

准备好 Qt 6、CMake 和 C++20 编译器后运行：

```shell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

改了协议收发或故障行为，需要补对应的 Qt Test 或 loopback TCP 测试。改了 JSON 字段，也要更新配置测试、样例和文档。

Core 不应该依赖控制台、GUI、动态脚本或外部命令执行。不要提交真实私有协议、账号密钥、设备密钥、生产地址和构建产物。
