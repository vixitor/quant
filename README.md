# Ultra-Low Latency Quantitative Trading System (Simulation)
**低延迟量化交易系统（仿真版） | C++20 | Matching Engine + Replay + Risk + PnL + Bench**

This is a self-built, single-node, event-driven trading system simulation written in **C++20**.
The focus is **systems engineering** (correctness, low latency, observability) rather than alpha strategies.

> Educational / research purpose only. No real exchange connectivity. No real trades.

---

## Highlights (What this project demonstrates)
- A **price-time priority** matching engine with an order book (limit/market/cancel)
- Deterministic **historical market data replay**
- Pluggable **strategy interface** (C++; optional Python via pybind11 later)
- Pre-trade **risk management**
- Real-time **position & PnL**
- **Benchmarking**: latency percentiles (P50/P99) and throughput

---

## System Architecture

```
Market Data Replay
        ↓
Strategy Layer
        ↓
Risk Management
        ↓
Order Router / Queue
        ↓
Matching Engine / OrderBook
        ↓
Trades / Position / PnL / Metrics
```

Design choices:
- Single-node, event-driven architecture (avoid network jitter in the hot path)
- Single-threaded matching for determinism and minimal locking
- Strategy and matching decoupled via a queue

---

## Features

### Trading / Matching
- Order types: **Limit / Market**
- Operations: **New / Cancel** (Modify optional)
- Rule: **Price-Time Priority**
- Order book: Bid/Ask with FIFO queues per price level
- Partial fills supported

### Market Data
- Historical tick replay from CSV
- Replay speed: **1× / N× / Unlimited** (throughput mode)

### Risk & Accounting
- Pre-trade risk checks: size limits, max position, rate limit, kill switch
- Real-time position & PnL (simplified mark-to-last)

### Observability / Performance
- End-to-end latency measurement
- Matching latency measurement
- Throughput (orders/sec)
- Percentiles: P50/P95/P99 via histogram

---

## Tech Stack
- **C++20**
- **CMake**
- Unit tests: GoogleTest / Catch2 (choose one)
- Profiling: `perf` + flamegraph
- Config: YAML/JSON (optional)

---

## Repository Layout

```
.
├── src/
│   ├── engine/        # matching engine + order book
│   ├── market/        # market data replay
│   ├── strategy/      # strategy interfaces + example strategies
│   ├── risk/          # pre-trade risk management
│   ├── router/        # order router + queues
│   ├── trade/         # fills, position, PnL
│   └── infra/         # logging, config, metrics utilities
├── tests/
├── bench/
├── config/
└── docs/
```

---

## Build

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

---

## Run (Example)

### 1) Prepare market data (CSV)
Example format:
```csv
ts_ns,symbol,price,qty
1700000000000000000,TEST,10000,10
1700000000000001000,TEST,10001,5
```

### 2) Run replay + strategy
```bash
./build/bin/sim --config config/config.yaml
```

### 3) Run benchmark
```bash
./build/bin/bench_match --orders 5000000 --seed 42
```

---

## Benchmark Results (Fill in with your machine)
> Record CPU model, OS, compiler, build flags, dataset size.

| Metric | Value |
|---|---:|
| Throughput | TBD orders/sec |
| Matching latency P50 | TBD µs |
| Matching latency P99 | TBD µs |

---

## Safety / Disclaimer
This project is for educational and research purposes only.
It does not connect to any real exchange and does not place real trades.

---

## Resume Bullet Points (Template)
- Designed and implemented a **price-time priority matching engine** in C++20 with limit/market/cancel support.
- Built a deterministic **historical market data replay** pipeline driving strategy evaluation and execution simulation.
- Implemented pre-trade **risk controls** and real-time **position & PnL** tracking.
- Developed a benchmarking suite reporting **throughput and latency percentiles (P50/P99)** with reproducible configs.
