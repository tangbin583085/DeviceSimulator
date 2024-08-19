# v0.1.0 发布检查

## 仓库

- [x] Qt/C++ 是唯一实现，没有遗留 C# 工程文件。
- [x] 作者和版权信息统一为 `tangbin`。
- [x] 保留 MIT License，许可证顶部不带日期。
- [x] 默认监听 `127.0.0.1`，文档说明了 `0.0.0.0` 风险。
- [x] 已加入贡献说明、安全说明和 Qt CI 配置。

## 实际验证

- [ ] 在可用环境准备 Qt 6.2+、CMake 3.21+ 和 C++20 编译器。
- [ ] 运行 CMake configure 和 Release build。
- [ ] 运行全部 Qt Test。
- [ ] 手动执行 `validate`、`sample-config`、`run`、`version` 和 `help`。
- [ ] 用独立 TCP Client 验证请求、Telemetry、分片、合并和主动断线。
- [ ] 在干净仓库运行 GitHub Actions。

## 发布

- [ ] 确认正式仓库地址和 `v0.1.0` tag。
- [ ] 检查 README、Changelog、LICENSE、样例配置和 Release Notes。
- [ ] 决定是否增加 CMake install/export 和平台安装包；当前版本不强制提供。
