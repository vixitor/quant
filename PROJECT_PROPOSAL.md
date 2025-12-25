# 项目书：低延迟量化交易系统（仿真版）
**Ultra-Low Latency Quantitative Trading System (Simulation)**

- 文档类型：个人独立开发项目书（可直接喂给 Codex / Cursor / Copilot 作为开发蓝图）
- 目标：做一个“工程上接近真实交易系统”的单机仿真平台，强调**正确性 + 低延迟 + 可压测**，用于量化开发简历项目

---

## 0. 版本与范围声明

### 0.1 本项目是什么
本项目实现一个**单机、事件驱动**的交易系统仿真闭环：

1. 历史行情回放（Market Data Replay）
2. 策略接收行情并下单（Strategy Layer）
3. 订单风控（Pre-trade Risk）
4. 撮合引擎撮合（Matching Engine / Order Book）
5. 产生成交（Fills）并更新持仓与 PnL
6. 输出性能指标（延迟/吞吐）

### 0.2 本项目不是什么
- 不连接真实交易所（无真实资金/真实下单）
- 不追求“策略收益率”，而追求**系统工程能力展示**
- 不做分布式（低延迟路径刻意避免网络/共识带来的抖动）

---

## 1. 项目背景与立项动机

量化开发（Quant Dev）岗位通常关注：
- 交易系统的端到端设计能力（行情→策略→风控→撮合→成交→PnL）
- 性能工程意识（低延迟、抖动、cache、内存分配、锁争用）
- 工程质量（模块解耦、可测试、可压测、可复现实验）

因此，本项目以“**撮合引擎 + 行情回放 + 风控 + 指标**”为核心，搭建一个可验证、可测量、可扩展的系统。

---

## 2. 项目目标

### 2.1 功能目标（MVP）
- 订单类型：Limit / Market
- 操作：New / Cancel（Modify 可选）
- 规则：Price-Time Priority（价格优先、时间优先）
- 订单簿：买卖双边 Order Book
- 回放：按时间戳回放历史行情，支持 1×/N×/无限速
- 策略接口：OnTick / OnTrade（或 OnBookUpdate）触发
- 风控：单笔限额、最大持仓、下单频率、Kill Switch
- 账户：持仓、现金、实时 PnL 计算（简化模型即可）
- 指标：延迟直方图（P50/P99）、吞吐、队列长度等

### 2.2 性能目标（可测量）
> 目标是“有数据可展示”，不强求达到顶级 HFT 数值。

| 指标 | 目标（建议） | 说明 |
|---|---:|---|
| 撮合延迟（单订单） | < 10 µs（单线程） | 以 benchmark 测得为准 |
| 撮合吞吐 | ≥ 500k orders/s | 在固定硬件上记录配置 |
| 延迟分位数 | P50 / P99 输出 | 必须可复现 |
| 回放吞吐 | ≥ 1M tick/s（无限速） | IO 与解析实现相关 |

### 2.3 工程目标
- 核心交易路径尽量无锁（或最小锁）
- 订单撤销达到 O(1) 或接近 O(1)（通过 OrderID→节点索引）
- 可单元测试（撮合正确性）
- 可压测、可 benchmark（性能数字可贴 README）
- 关键模块可替换（OrderBook 数据结构、队列实现、数据源）

---

## 3. 系统总体架构

### 3.1 架构图（逻辑）

```
┌──────────────────────────────┐
│   Market Data Replay (历史)  │
└───────────────┬──────────────┘
                │ Tick/BookUpdate
                ▼
┌──────────────────────────────┐
│        Strategy Layer         │
│ (C++ strategies; Python opt.) │
└───────────────┬──────────────┘
                │ Orders
                ▼
┌──────────────────────────────┐
│     Risk Management (预风控)  │
└───────────────┬──────────────┘
                │ Accepted Orders
                ▼
┌──────────────────────────────┐
│     Order Router / Queues     │
│ (lock-free ring buffer opt.)  │
└───────────────┬──────────────┘
                │
                ▼
┌──────────────────────────────┐
│   Matching Engine / OrderBook │
└───────────────┬──────────────┘
                │ Fills/Trades
                ▼
┌──────────────────────────────┐
│  Position / PnL / Metrics     │
└──────────────────────────────┘
```

### 3.2 关键设计原则
- **单机事件驱动**：减少网络不确定性
- **撮合单线程**：保证 determinism，降低锁复杂度
- **解耦策略与撮合**：策略慢不影响撮合性能（通过队列）
- **可观测性优先**：任何性能结论必须有指标与实验复现

---

## 4. 数据模型与事件定义

### 4.1 基本类型（建议）
- `Symbol`：字符串或整数 ID
- `Price`：整数（tick 表示）避免浮点误差（例如 1 tick = 0.01）
- `Qty`：整数
- `Timestamp`：纳秒级（int64）

### 4.2 事件类型
- `MarketEvent`
  - `Tick`：成交价/成交量/时间戳（最小可用）
  - `BookUpdate`（可选）：L2 更新（bid/ask 多档）
- `OrderEvent`
  - `NewOrder`：side/price/qty/type
  - `CancelOrder`：order_id
- `FillEvent`
  - `trade_id, order_id, symbol, price, qty, ts`

### 4.3 最小数据格式（推荐 CSV）
**Tick CSV 示例**
```
ts_ns,symbol,price,qty
1700000000000000000,TEST,10000,10
...
```

> price 使用整数 tick，例如 10000 = 100.00

---

## 5. 模块详细设计（可直接拆 Issue）

> 下面每个模块都给出：职责、关键接口、实现要点、测试点。

---

### 5.1 engine：撮合引擎（核心）

#### 5.1.1 职责
- 接收订单（New/Cancel）
- 维护订单簿（bid/ask）
- 按价格-时间优先撮合，产生成交
- 将剩余订单挂单入簿

#### 5.1.2 核心接口（建议）
```cpp
struct OrderRequest {
  OrderID id;
  SymbolId sym;
  Side side;            // Buy/Sell
  OrderType type;       // Limit/Market
  Price price;          // limit price (tick int)
  Qty qty;
  Timestamp ts;
};

struct CancelRequest {
  OrderID id;
  SymbolId sym;
  Timestamp ts;
};

struct Fill {
  TradeID trade_id;
  OrderID taker_id;
  OrderID maker_id;
  SymbolId sym;
  Side taker_side;
  Price price;
  Qty qty;
  Timestamp ts;
};

class MatchingEngine {
public:
  std::vector<Fill> on_order(const OrderRequest& req);
  bool on_cancel(const CancelRequest& req);
  const OrderBook& book(SymbolId sym) const;
};
```

#### 5.1.3 OrderBook 数据结构方案（MVP 推荐）
- `std::map<Price, PriceLevel, Desc>` 用于 bids
- `std::map<Price, PriceLevel, Asc>` 用于 asks
- `PriceLevel` 内部维护 FIFO（如 `std::deque<OrderNode*>` 或 intrusive list）
- 维护 `order_id -> OrderNode*` 的索引，以支持 O(1) 撤单

> 后续性能优化可替换为 `flat_map`、price ladder（数组）、自实现红黑树、内存池等。

#### 5.1.4 撮合规则（明确）
- 价格优先：买单从最高 bid 对最优 ask，卖单从最低 ask 对最优 bid
- 时间优先：同价位 FIFO
- Market Order：跨越价位吃单，直到 qty=0 或对手盘为空
- 价格约束：Limit Order 仅在满足价格条件时成交，否则挂单

#### 5.1.5 关键边界条件
- 部分成交（partial fill）
- 订单簿为空
- Cancel 不存在 / 已成交完 / 已撤单
- Market order 对手盘不足（剩余 qty 丢弃或记录“未成交”统计）

#### 5.1.6 测试点（必须）
- 正确性：不同价位与时间顺序的撮合结果完全符合预期
- 撤单：撤掉后不再参与撮合
- 部分成交：剩余挂单量正确
- 价格约束：limit price 不满足时不成交

---

### 5.2 market：行情回放与解析

#### 5.2.1 职责
- 读取历史数据文件（CSV/二进制可选）
- 按时间戳回放（sleep 或 busy-wait 可选）
- 推送 MarketEvent 给策略层

#### 5.2.2 接口建议
```cpp
class MarketDataReplay {
public:
  explicit MarketDataReplay(const ReplayConfig& cfg);
  void run(std::function<void(const MarketEvent&)> on_event);
};
```

#### 5.2.3 回放模式
- 实时模式：按 timestamp sleep
- 加速模式：按比例缩放 sleep
- 无限速：不 sleep，纯吞吐（用于压测）

#### 5.2.4 测试点
- 事件顺序严格递增（ts）
- 不同速度模式下，事件数量一致
- 解析正确（price/qty）

---

### 5.3 strategy：策略层（接口与示例策略）

#### 5.3.1 职责
- 接收行情事件（Tick/BookUpdate）
- 维护策略状态（例如移动平均、库存、订单状态）
- 生成订单请求

#### 5.3.2 接口建议
```cpp
class IStrategy {
public:
  virtual ~IStrategy() = default;
  virtual void on_market(const MarketEvent& e) = 0;
  virtual void on_fill(const Fill& f) = 0;
};
```

#### 5.3.3 最小示例策略（必须实现 1 个）
- `TWAP`：按时间分片下单
- 或 `Simple Market Maker`：围绕 mid price 挂双边单（简化）
- 或 `Mean Reversion`：基于短均线/长均线

> 注意：策略简单即可，重点是工程闭环。

#### 5.3.4 可选：Python 扩展
- 使用 pybind11 暴露 Strategy API
- 强制要求：Python 不进入撮合热点路径（只在策略侧）

---

### 5.4 risk：预交易风控模块

#### 5.4.1 职责
- 订单进入撮合前校验：
  - 单笔最大下单量
  - 最大持仓（净头寸）
  - 下单频率限制（每秒最大订单数）
  - Kill switch（手动/自动）

#### 5.4.2 接口建议
```cpp
enum class RiskDecision { Accept, Reject };

class RiskManager {
public:
  RiskDecision check(const OrderRequest& req, const AccountState& acct);
};
```

#### 5.4.3 测试点
- 超限订单必拒
- Kill switch 触发后全部拒
- 频率限制正确

---

### 5.5 router：订单路由与队列

#### 5.5.1 职责
- 将策略产生的订单异步传递给撮合引擎
- 支持不同队列实现：
  - MVP：`std::queue + mutex`（先跑通）
  - 优化：SPSC/MPSC lock-free ring buffer

#### 5.5.2 接口建议
```cpp
class OrderRouter {
public:
  void submit(const OrderRequest& req);
  bool try_pop(OrderRequest& out);
};
```

---

### 5.6 trade：成交、账户、持仓与 PnL

#### 5.6.1 职责
- 处理 fills，更新：
  - position（持仓）
  - cash（现金）
  - realized/unrealized PnL
- 记录交易与订单统计

#### 5.6.2 PnL 模型（简化）
- 使用最近成交价作为 mark price（unrealized）
- realized 以成交价格差计算
- 可只实现一个 symbol 的账户也可

#### 5.6.3 测试点
- 成交后 position 正确增减
- PnL 更新符合模型

---

### 5.7 infra：日志、配置与性能指标

#### 5.7.1 日志
- MVP：spdlog / 自实现 async logger（可选）
- 关键：日志不要污染热点路径（撮合中尽量禁用或采样）

#### 5.7.2 配置
- `config.yaml`：symbol、风控阈值、回放速度、benchmark 参数
- 推荐用 `yaml-cpp` 或 JSON

#### 5.7.3 性能指标（必须）
- 订单端到端延迟
- 撮合延迟
- 队列长度
- 吞吐（orders/sec）
- 分位数统计（P50/P95/P99）

> 分位数实现：histogram 或 t-digest（MVP 可用固定桶 histogram）

---

## 6. 代码结构（建议仓库目录）

```
.
├── CMakeLists.txt
├── config/
│   └── config.yaml
├── src/
│   ├── engine/
│   ├── market/
│   ├── strategy/
│   ├── risk/
│   ├── router/
│   ├── trade/
│   └── infra/
├── bench/
├── tests/
├── tools/
└── docs/
    ├── PROJECT_PROPOSAL.md
    └── DESIGN_NOTES.md
```

---

## 7. 技术选型

| 领域 | 选型 |
|---|---|
| 语言 | C++20 |
| 构建 | CMake |
| 测试 | GoogleTest / Catch2 |
| 性能分析 | perf / flamegraph |
| 配置 | yaml-cpp（或 nlohmann/json） |
| 可选 Python | pybind11 |
| 平台 | Linux（建议 Ubuntu 22.04+） |

---

## 8. 开发计划与里程碑（12 周，强可执行）

> 每周都有“可运行产物”，避免写到后面发现架构不通。

### Milestone 0：项目骨架（第 0-1 周）
- [ ] 建立目录结构 + CMake
- [ ] 定义核心类型（Price/Qty/Timestamp）
- [ ] 建立最小 main：读取配置、启动回放

**产物**：能编译、能跑一个空 pipeline。

### Milestone 1：OrderBook + Matching（第 2-3 周）
- [ ] 实现 Order / PriceLevel / OrderBook
- [ ] 实现 limit/market 撮合
- [ ] 支持 cancel
- [ ] 单元测试覆盖主要场景

**产物**：撮合引擎独立可测，测试通过。

### Milestone 2：行情回放 + 策略（第 4-5 周）
- [ ] CSV 解析 + 回放引擎
- [ ] Strategy 接口
- [ ] 1 个示例策略（TWAP 或 maker）

**产物**：能跑 end-to-end：回放→策略→下单→撮合→成交。

### Milestone 3：风控 + 账户（第 6-7 周）
- [ ] RiskManager 实现与测试
- [ ] Position/PnL 更新
- [ ] 订单拒单统计

**产物**：能输出 PnL 与风险拒单结果。

### Milestone 4：路由解耦 + 指标（第 8-9 周）
- [ ] Router 队列（MVP + 可选 lock-free）
- [ ] 端到端延迟统计（P50/P99）
- [ ] benchmark 程序

**产物**：跑压测能得到可复现性能数字。

### Milestone 5：性能优化与文档（第 10-12 周）
- [ ] 内存池/对象复用（可选）
- [ ] cache line 对齐（可选）
- [ ] perf 分析 + flamegraph 截图
- [ ] README + Bench 报告 + 架构图

**产物**：一个“能放到简历”的工程项目。

---

## 9. 测试与验收标准（Definition of Done）

### 9.1 Correctness DoD
- 单元测试覆盖：撮合顺序、部分成交、撤单、边界条件
- 回放 determinism：同一输入产生同样输出（成交序列一致）

### 9.2 Performance DoD
- benchmark 输出：吞吐、P50/P99 撮合延迟
- 提供一份 `bench/results.md`：记录硬件、编译参数、数据规模

### 9.3 工程 DoD
- README 可让他人 5 分钟内跑起来
- 代码可读：模块边界清晰、命名一致
- 文档：架构与关键设计点有说明

---

## 10. 风险与对策

| 风险 | 表现 | 对策 |
|---|---|---|
| 早期过度优化 | 代码复杂、跑不通 | 先做 MVP，再优化 |
| 数据结构选错 | 性能很差 | 保持可替换，先 map，后换 flat/ladder |
| 策略牵扯太多 | 变成“策略项目” | 策略只做示例，重心在系统 |
| 指标不可信 | 性能数字无法复现 | 固定输入、记录硬件/编译参数 |

---

## 11. 最终输出物清单（用于简历/面试）

- `README.md`：架构、特性、如何运行、性能数字
- `docs/PROJECT_PROPOSAL.md`：项目书
- `docs/DESIGN_NOTES.md`：关键设计取舍（OrderBook、cancel、队列）
- `bench/results.md`：benchmark 结果与复现实验说明
- `perf/`：perf / flamegraph 截图（可选但很加分）
- 一段可直接写入简历的 bullets（README 末尾提供）

---

## 12. 可扩展路线（完成后再做）
- 多 symbol 多 OrderBook（按 symbol 分片）
- Python 策略（pybind11）
- 更真实的 L2 Book 更新处理（多档）
- 更真实的手续费/滑点模型
- lock-free ring buffer + 线程绑核 + NUMA 优化
- 分布式回测系统（作为第二项目）

---

## 附录：建议的 Codex / Cursor Prompt（可选）

> 你可以把本项目书 + 下面 prompt 给 Codex，让它按模块生成代码。

**Prompt 模板：**
1) 读取 `PROJECT_PROPOSAL.md`，实现 `src/engine` 模块：Order、PriceLevel、OrderBook、MatchingEngine。  
2) 要求：C++20、无异常（可用返回值）、可单元测试、撤单通过 order_id 索引达到 O(1) 或近似 O(1)。  
3) 输出：完整头文件/源文件 + 关键单元测试（GoogleTest）。  
4) 不要实现其它模块，保持接口与项目书一致。  
