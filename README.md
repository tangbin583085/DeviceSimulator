# DeviceSimulator

DeviceSimulator 是一个用 Qt 和 C++ 写的 TCP 设备模拟器，主要用于联调和自动化测试。

项目会模拟一个简单的二进制协议，可以配置设备收到不同命令后的响应，也可以模拟延迟、
丢包、分片和断开连接等情况。

当前项目还在持续开发中，具体功能以源码和样例配置为准。

## 构建

需要 Qt 6、CMake 3.21 以上版本，以及支持 C++20 的编译器。

```shell
cmake -S . -B build
cmake --build build
```

## License

MIT License，详见 [LICENSE](LICENSE)。
