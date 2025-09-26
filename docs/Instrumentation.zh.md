# 插桩与条件规范化

BrInfo 在关键条件点插入记录语句，统一在运行时输出规范化后的谓词，以便与静态结果对齐。

## 条件类型（cond_kind）

- IF：包含三目 `?:` 的条件以及短路子条件
- LOOP：循环条件
- CASE：`switch` 的 case 标签
- DEFAULT：`switch` 的 default 分支
- TRY：异常处理相关的守卫

## 规范化

- 将条件规范化为 `cond_norm`（如 `x < 0`、`a == b`），并在需要时用 `norm_flip` 表示极性翻转。
- 计算稳定哈希 `cond_hash`（优先 xxHash64，回退 FNV-1a），以 16 位十六进制表示。
- 运行时便于人工定位的关键字段是 `(file, line, cond_norm)`，而哈希用于稳健匹配。

## 发出点

- 在条件位置插入 `Runtime::LogCond(...)`，传入：函数哈希、文件/行、布尔值、`cond_norm`、`cond_hash`、`norm_flip`、`cond_kind`。

## 备注

- 字符串化尽量忠实于源码（空格、标识符），以保证与静态分析一致。
- 对 `switch` 和逻辑运算进行展开，使每个分支都能在链条中体现。
