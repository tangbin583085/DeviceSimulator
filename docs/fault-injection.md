# 故障注入

故障会按下面的顺序处理：

1. 根据概率决定是否丢弃。
2. 等待 `delayMs`。
3. 编码正常协议帧。
4. 修改长度或 CRC。
5. 加上前置噪声。
6. 分片发送，或者和相邻响应合并发送。
7. 如果配置了 `disconnectAfterSend`，发送完成后断开。

## Behavior

- `NoResponse`：不返回任何字节。
- `DropResponse`：模拟响应被丢掉。
- `Disconnect`：不响应，直接断开。
- `Respond`：正常生成响应，再应用 faults。

## Fault 字段

| Field | 作用 |
| --- | --- |
| `delayMs` | 响应前延迟 |
| `dropProbability` | 按固定随机种子决定是否丢弃 |
| `corruptChecksum` | 故意破坏 CRC |
| `invalidLengthAdjustment` | 修改长度字段，但不重新计算 CRC |
| `noisePrefixHex` | 在协议帧前加入固定噪声 |
| `fragmentSizes` | 按指定大小执行多次 TCP Write |
| `fragmentDelayMs` | 两片之间的延迟 |
| `coalesceCount` | 最多合并多少条响应 |
| `coalesceWindowMs` | 等待相邻响应的最长时间 |
| `disconnectAfterSend` | 发完后关闭连接 |

所有长度、延迟和数量都受 `limits` 限制。一次 TCP Write 不等于客户端的一次 Read，客户端还是要按字节流解析。

## 可重复随机

每个 Server 都有自己的 `std::mt19937`，每次连接会按连接顺序派生 seed。相同 `randomSeed`、相同请求顺序和相同配置，会得到相同的概率丢包结果。
