# BrInfo Report CLI

按断言输出三元组 <prefix, oracle, cond_chain>，并可选结合静态 meta 做链路匹配与富化。

## 快速使用

```bash
brinfo_report.py \
	--logs examples/runtime.ndjson \
	--meta examples/branch \
	--out examples/triples.jsonl \
	--dedupe-conds \
	--approx-match --approx-topk 3 --approx-threshold 0.6
```

- 输入：NDJSON 行，事件类型包括 `test_start`、`assertion`、`invocation_start/end`、`cond`。
- 输出：JSONL，每个断言一行记录。
- 过滤：用 `--suite` 或 `--test` 做子串过滤。

## 输出结构（每条记录）

- `test`: { suite, name, full, file, line }
- `assertion`: { assert_id, macro, file, line, raw }
- `prefix`: 调用列表（断言前缀窗口，in_oracle=0）。每项：{ invocation_id, call_file, call_line, call_expr }
- `oracle_calls`: 调用列表（断言内部，in_oracle=1）。结构同上。
- `cond_chains`: 字典，键为字符串化的 invocation_id，值为该调用的条件事件数组；每个事件包含：
	- { file, line, cond_norm, cond_kind, cond_hash, val, flip }
	- 说明：`flip` 即运行时的 `norm_flip`，用于在匹配时与 `val` 做异或（val ^ flip）。
	- 循环压缩：运行日志会在每次迭代记录一次循环头（`cond_kind = LOOP`）并穿插循环体条件。为与静态链路对齐，报告会将多次迭代压缩为一次：
		- 当“进入循环”（以原始 `val` 为 True 判定）时：保留循环头的第一次 True 与“第一轮迭代”的循环体（对其内部再递归压缩），并丢弃最终的循环退出 False；跳过其余迭代。
		- 若未进入循环（循环头原始 `val` 为 False），则保留该 False 不变。
- `invocations`: 字典，键为字符串化的 invocation_id；值包含：
	- func_hash（来自运行时 cond 事件）
	- signature（来自 functions.meta.json）
	- matched_static（如成功匹配到静态链）：数组，元素为 { source, chain_id, cond_hashes }
		- cond_hashes 是静态侧的条件序列，形如 [(cond_hash, value), ...]，其中 value 来自 chains.meta.json 的 sequence.value。
	- approx_static（当精确匹配为空且启用近似匹配且分数通过阈值时存在）：数组，元素为 { source, chain_id, score, lcp, lcs, diffs }。
		- score ∈ [0,1]；lcp/lcs 为辅助指标；diffs 为对齐编辑轨迹（op ∈ keep|flip|subst|ins|del），便于解释。

备注：当指定 `--dedupe-conds` 时，仅影响 `cond_chains` 展示（同一 cond_hash 在一次调用中去重），匹配仍基于原始全量顺序事件，不受去重影响。
（注意：实际用于匹配的序列会先进行“循环压缩”，然后才按 `(cond_hash, val ^ flip)` 二元组与静态链做精确比较；`--dedupe-conds` 仅影响展示层，不影响匹配。）

## 与静态 meta 的集成

工具会在 `--meta` 目录按固定文件名顺序加载并校验：

1) `conditions.meta.json`
- 期望结构：
	- { analysis_version, conditions: [ { id, hash, cond_norm, kind, file, line, ... }, ... ] }
- 用途：
	- 建立 id → 条件条目 的映射（用于把链路里的 cond_id 解析为 cond_hash / cond_norm / kind）。
	- 同时构建 hash → 条目 的辅助映射（展示/调试）。

2) `chains.meta.json`
- 期望结构：
	- { analysis_version, chains: [ { func_hash, sequence: [ { cond_id, value }, ... ], ... }, ... ] }
- 用途：
	- 通过 `cond_id` → `conditions.meta.json` 的条件，生成静态条件序列：[(cond_hash, value), ...]。
	- 以 func_hash 为键，汇总到 `static_chains_by_func` 中。

3) `functions.meta.json`
- 期望结构：
	- { analysis_version, functions: [ { hash, name?, signature, ... }, ... ] }
- 用途：
	- 建立 func_hash → 函数信息 的映射，用于在输出中补充 signature（或名称）。

一致性检查：
- 若三份文件的 `analysis_version` 同时存在且不一致，工具会打印 warn 提示（不终止）。

## 匹配规则（运行时 ↔ 静态）

- 先取函数：从运行时 cond 事件中读取 `func` 字段作为 func_hash（若缺失，则该调用无法参与静态匹配）。
- 运行时序列：对该调用的 cond 事件，构造 `rseq = [(cond_hash, val ^ flip), ...]`。
	- 其中 `flip` 即事件内的 `norm_flip`，用于与 `val` 做异或，统一与静态定义的真值方向。
- 静态序列：由 `chains.meta.json` 的 `sequence` 字段解析并映射为 `[(cond_hash, value), ...]`。
- 精确匹配：当且仅当二者序列完全相同（长度/顺序/二元组一致）即认为匹配成功；匹配结果写入 `invocations[*].matched_static`。

### 近似匹配（可选）

- 通过 `--approx-match` 开启。参数：
	- `--approx-topk`（默认 3）
	- `--approx-threshold`（默认 0.6）
− 当精确匹配为空时，仅在“同一个 func_hash”下计算 Top-K 最相近路径：
	- 使用路径无关 sid（`cond_kind + '\t' + cond_norm`）
	- 用 sid 集合的 Jaccard 相似做预筛选
	- 对 `(sid, val ^ flip)` 做加权全局对齐，LOOP 权重更高
	- 辅以 LCP/LCS 做排序与解释
- 若缺少 func_hash 或该 func_hash 不在 meta 中，则不产生近似结果。可用结果写入 `invocations[*].approx_static`。

注意：`cond_hash` 对路径策略敏感（例如拼写位点/展开位点、绝对/相对、realpath、#line 重映射等），请确保静态与运行时采用一致的键生成规则，否则匹配可能失败。后续可考虑引入“路径无关”的备用匹配（基于 cond_norm+cond_kind 的规范化 ID）。

## 常见问题排查

- triples 里只有函数没有 matched_static：
	- 检查 meta 的 `analysis_version` 是否一致；
	- 检查 `chains.meta.json` 是否包含非空的 `sequence`；
	- 检查 `conditions.meta.json` 是否能通过 `id` 找到对应条件；
	- 核对运行时与静态的 `cond_hash` 生成策略是否一致；
	- 运行时 cond 事件是否携带 `func` 字段（用于锚定 func_hash）。

- cond_id 从 0 开始：
	- 解析时不要把 0 当作假值丢弃，应按“键存在性”而非 or 短路来取整型 ID。

- `--dedupe-conds` 后匹配失败：
	- 匹配并不受去重影响；若失败，多半是 cond_hash 不一致或静态序列缺失。

## 示例

```bash
brinfo_report.py \
	--logs examples/runtime.ndjson \
	--meta examples/branch \
	--out examples/triples.jsonl
```

生成的每条记录中，若 `matched_static` 存在，你将看到对应的静态来源文件以及匹配到的链 id（chain_id）与静态 cond 序列（包含静态真值）。
