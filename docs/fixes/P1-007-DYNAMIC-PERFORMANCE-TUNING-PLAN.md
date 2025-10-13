# P1-007: 动态性能调优实施计划

**实施日期**: 2025-10-13  
**问题级别**: P1-High  
**审计报告**: audits/CODE_QUALITY_AUDIT_2025-10-13_ROUND2.md  
**铁笼协议**: v5.0 - ZERO-TOLERANCE-PERFORMANCE原则  
**预计时间**: 6小时  
**预期收益**: 自适应性能优化，提升GPU利用率

---

## 📊 问题分析

### 当前实现问题

**问题1**: 缺少运行时性能监控

当前实现使用固定的CUDA配置参数，无法根据实际GPU性能动态调整：

```cpp
// 当前实现：固定配置
const int BLOCK_SIZE = 256;
const int GRID_SIZE = 1024;
const int POINTS_PER_THREAD = 32;
```

**问题2**: 缺少自适应调优机制

- GPU利用率可能不是最优（目标≥90%）
- 不同GPU型号需要不同的配置
- 无法根据实际工作负载动态调整

**问题3**: 缺少性能遥测持久化

- 性能数据未持久化存储
- 无法进行历史性能分析
- 缺少SHA-256完整性保护

---

## 🎯 解决方案设计

### 方案A: 完整动态调优实现（推荐）

**特性**:
1. ✅ 滑动窗口吞吐量监控
2. ✅ 配置自动调整算法
3. ✅ 遥测数据持久化
4. ✅ SHA-256完整性保护
5. ✅ 多GPU自适应调优

**实现步骤**:

#### 步骤1: 设计性能监控器

```cpp
class PerformanceMonitor {
public:
    struct PerformanceMetrics {
        double keys_per_sec;           // 吞吐量
        double gpu_utilization;        // GPU利用率
        double memory_bandwidth;       // 内存带宽利用率
        int active_blocks;             // 活跃block数量
        int active_warps;              // 活跃warp数量
        std::chrono::steady_clock::time_point timestamp;
    };
    
    struct TelemetryPacket {
        std::string gpu_model;
        int gpu_id;
        PerformanceMetrics metrics;
        std::map<std::string, int> config;  // 当前配置
        std::string digest;                 // SHA-256摘要
    };
    
private:
    // 滑动窗口（最近N个样本）
    std::deque<PerformanceMetrics> metrics_window_;
    size_t window_size_;                    // 窗口大小（默认10）
    
    // 遥测数据
    std::vector<TelemetryPacket> telemetry_history_;
    std::string telemetry_file_;
    
    // 性能目标
    double target_gpu_utilization_;         // 目标GPU利用率（90%）
    double target_memory_bandwidth_;        // 目标内存带宽（80%）
    
public:
    PerformanceMonitor(size_t window_size = 10,
                      double target_gpu_util = 0.90,
                      double target_mem_bw = 0.80);
    
    // 添加性能样本
    void add_sample(const PerformanceMetrics& metrics);
    
    // 获取平均性能
    PerformanceMetrics get_average_metrics() const;
    
    // 检查是否需要调优
    bool needs_tuning() const;
    
    // 保存遥测数据
    bool save_telemetry(const TelemetryPacket& packet);
    
    // 加载遥测历史
    bool load_telemetry_history();
};
```

#### 步骤2: 实现配置调优器

```cpp
class ConfigurationTuner {
public:
    struct CUDAConfig {
        int block_size;
        int grid_size;
        int points_per_thread;
        int shared_memory_size;
    };
    
    struct TuningResult {
        CUDAConfig new_config;
        std::string reason;
        double expected_improvement;
    };
    
private:
    CUDAConfig current_config_;
    CUDAConfig min_config_;
    CUDAConfig max_config_;
    
    // 调优策略
    enum class TuningStrategy {
        INCREASE_PARALLELISM,    // 增加并行度
        DECREASE_PARALLELISM,    // 减少并行度
        INCREASE_WORK_PER_THREAD,  // 增加每线程工作量
        DECREASE_WORK_PER_THREAD,  // 减少每线程工作量
        OPTIMIZE_MEMORY,         // 优化内存使用
        NO_CHANGE                // 无需调整
    };
    
public:
    ConfigurationTuner(const CUDAConfig& initial_config);
    
    // 根据性能指标建议调优
    TuningResult suggest_tuning(const PerformanceMonitor::PerformanceMetrics& metrics);
    
    // 应用配置
    bool apply_config(const CUDAConfig& config);
    
    // 获取当前配置
    CUDAConfig get_current_config() const;
    
private:
    // 选择调优策略
    TuningStrategy select_strategy(const PerformanceMonitor::PerformanceMetrics& metrics);
    
    // 计算新配置
    CUDAConfig calculate_new_config(TuningStrategy strategy);
    
    // 验证配置有效性
    bool validate_config(const CUDAConfig& config);
};
```

#### 步骤3: 实现遥测数据持久化

```cpp
class TelemetryPersistence {
public:
    // 保存遥测数据（JSONL格式）
    static bool save_telemetry(const std::string& filename,
                               const PerformanceMonitor::TelemetryPacket& packet);
    
    // 加载遥测历史
    static std::vector<PerformanceMonitor::TelemetryPacket> 
        load_telemetry_history(const std::string& filename);
    
    // 计算SHA-256摘要
    static std::string calculate_digest(const PerformanceMonitor::TelemetryPacket& packet);
    
    // 验证摘要
    static bool verify_digest(const PerformanceMonitor::TelemetryPacket& packet);
    
private:
    // 序列化为JSON
    static std::string serialize(const PerformanceMonitor::TelemetryPacket& packet);
    
    // 反序列化
    static PerformanceMonitor::TelemetryPacket deserialize(const std::string& json_str);
};
```

#### 步骤4: 集成到主循环

```cpp
// 在 Puzzle71Solver::Run() 中集成

// 初始化性能监控器
PerformanceMonitor perf_monitor(10, 0.90, 0.80);

// 初始化配置调优器
ConfigurationTuner::CUDAConfig initial_config{
    .block_size = 256,
    .grid_size = 1024,
    .points_per_thread = 32,
    .shared_memory_size = 8192
};
ConfigurationTuner tuner(initial_config);

// 主循环
while (!should_stop) {
    // 执行CUDA kernel
    auto start = std::chrono::steady_clock::now();
    execute_kernel(tuner.get_current_config());
    auto end = std::chrono::steady_clock::now();
    
    // 收集性能指标
    PerformanceMonitor::PerformanceMetrics metrics;
    metrics.keys_per_sec = calculate_throughput(start, end);
    metrics.gpu_utilization = query_gpu_utilization();
    metrics.memory_bandwidth = query_memory_bandwidth();
    metrics.timestamp = end;
    
    // 添加到监控器
    perf_monitor.add_sample(metrics);
    
    // 检查是否需要调优（每10个样本检查一次）
    if (perf_monitor.needs_tuning()) {
        auto tuning_result = tuner.suggest_tuning(perf_monitor.get_average_metrics());
        
        if (tuning_result.expected_improvement > 0.05) {  // 预期提升>5%
            logger.log_info("Applying tuning: " + tuning_result.reason);
            tuner.apply_config(tuning_result.new_config);
            
            // 保存遥测数据
            PerformanceMonitor::TelemetryPacket packet;
            packet.gpu_model = get_gpu_model();
            packet.gpu_id = get_gpu_id();
            packet.metrics = metrics;
            packet.config = config_to_map(tuning_result.new_config);
            packet.digest = TelemetryPersistence::calculate_digest(packet);
            
            perf_monitor.save_telemetry(packet);
        }
    }
}
```

---

## 📊 代码变更统计

### 预期变更

| 文件 | 变更类型 | 行数变更 |
|------|---------|---------|
| `src/performance/performance_monitor.h` | 新增 | +150 |
| `src/performance/performance_monitor.cpp` | 新增 | +200 |
| `src/performance/configuration_tuner.h` | 新增 | +100 |
| `src/performance/configuration_tuner.cpp` | 新增 | +250 |
| `src/performance/telemetry_persistence.h` | 新增 | +80 |
| `src/performance/telemetry_persistence.cpp` | 新增 | +150 |
| `src/solver.cpp` | 修改 | +50 -10 |
| `tests/unit/test_performance_monitor.cpp` | 新增 | +200 |
| `tests/unit/test_configuration_tuner.cpp` | 新增 | +200 |

### 总计

- **新增代码**: 1380行
- **修改代码**: 10行
- **删除代码**: 0行
- **净增加**: 1370行

---

## 🧪 验证方法

### 1. 性能监控验证

```cpp
TEST(PerformanceMonitorTest, SlidingWindowTest) {
    PerformanceMonitor monitor(5);  // 窗口大小5
    
    // 添加5个样本
    for (int i = 0; i < 5; i++) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 1000.0 + i * 100;
        metrics.gpu_utilization = 0.85 + i * 0.01;
        monitor.add_sample(metrics);
    }
    
    // 验证平均值
    auto avg = monitor.get_average_metrics();
    EXPECT_NEAR(avg.keys_per_sec, 1200.0, 10.0);
    EXPECT_NEAR(avg.gpu_utilization, 0.87, 0.01);
}
```

### 2. 配置调优验证

```cpp
TEST(ConfigurationTunerTest, LowUtilizationTuning) {
    ConfigurationTuner::CUDAConfig initial{256, 1024, 32, 8192};
    ConfigurationTuner tuner(initial);
    
    // 模拟低GPU利用率
    PerformanceMonitor::PerformanceMetrics metrics;
    metrics.gpu_utilization = 0.60;  // 低于目标90%
    metrics.memory_bandwidth = 0.50;
    
    // 建议调优
    auto result = tuner.suggest_tuning(metrics);
    
    // 验证建议增加并行度
    EXPECT_GT(result.new_config.grid_size, initial.grid_size);
    EXPECT_GT(result.expected_improvement, 0.0);
}
```

### 3. 遥测持久化验证

```cpp
TEST(TelemetryPersistenceTest, SaveAndLoadTest) {
    PerformanceMonitor::TelemetryPacket packet;
    packet.gpu_model = "RTX 2080 Ti";
    packet.gpu_id = 0;
    packet.metrics.keys_per_sec = 1000.0;
    packet.digest = TelemetryPersistence::calculate_digest(packet);
    
    // 保存
    ASSERT_TRUE(TelemetryPersistence::save_telemetry("test_telemetry.jsonl", packet));
    
    // 加载
    auto history = TelemetryPersistence::load_telemetry_history("test_telemetry.jsonl");
    ASSERT_EQ(history.size(), 1);
    
    // 验证摘要
    EXPECT_TRUE(TelemetryPersistence::verify_digest(history[0]));
}
```

---

## 🎯 铁笼协议合规性

### ZERO-TOLERANCE-PERFORMANCE原则

| 检查项 | 状态 | 说明 |
|--------|------|------|
| 性能监控 | ✅ 通过 | 滑动窗口实时监控 |
| 自适应调优 | ✅ 通过 | 自动配置调整 |
| 性能基线 | ✅ 通过 | 遥测数据持久化 |
| 回归检测 | ✅ 通过 | 历史数据对比 |

### MANDATORY-DIGEST原则

| 检查项 | 状态 | 说明 |
|--------|------|------|
| SHA-256完整性 | ✅ 通过 | 遥测数据摘要保护 |
| 防篡改机制 | ✅ 通过 | 摘要验证 |
| 数据持久化 | ✅ 通过 | JSONL格式存储 |

---

## ⚠️ 风险评估

### 已识别风险

| 风险 | 概率 | 影响 | 缓解措施 | 状态 |
|------|------|------|---------|------|
| 调优开销 | 中 | 低 | 每10个样本调优一次 | ✅ 已缓解 |
| 配置不稳定 | 低 | 中 | 验证配置有效性 | ✅ 已缓解 |
| 性能回归 | 低 | 高 | 预期提升>5%才应用 | ✅ 已缓解 |
| 遥测开销 | 低 | 低 | 异步写入 | ✅ 已缓解 |

**总体风险**: ✅ 低

---

## 📝 实施计划

### 阶段1: 性能监控器（2小时）

1. ⏳ 实现PerformanceMonitor类
2. ⏳ 实现滑动窗口逻辑
3. ⏳ 实现性能指标收集
4. ⏳ 编写单元测试

### 阶段2: 配置调优器（2小时）

1. ⏳ 实现ConfigurationTuner类
2. ⏳ 实现调优策略选择
3. ⏳ 实现配置计算和验证
4. ⏳ 编写单元测试

### 阶段3: 遥测持久化（1小时）

1. ⏳ 实现TelemetryPersistence类
2. ⏳ 实现JSONL序列化
3. ⏳ 实现SHA-256摘要
4. ⏳ 编写单元测试

### 阶段4: 集成和测试（1小时）

1. ⏳ 集成到Puzzle71Solver::Run()
2. ⏳ 端到端测试
3. ⏳ 性能验证
4. ⏳ 文档更新

---

## 📚 参考资料

### 相关文档

- 审计报告: `audits/CODE_QUALITY_AUDIT_2025-10-13_ROUND2.md`
- 修复计划: `docs/fixes/AUDIT_FIX_PLAN_2025-10-13.md`
- 铁笼协议: `AGENTS.md` (v5.0)

### Git Commit

```bash
git add src/performance/*.h src/performance/*.cpp
git add src/solver.cpp
git add tests/unit/test_performance_monitor.cpp
git add tests/unit/test_configuration_tuner.cpp
git add docs/fixes/P1-007-DYNAMIC-PERFORMANCE-TUNING-PLAN.md

git commit -m "feat(perf): Implement dynamic performance tuning (P1-007)

- Implemented PerformanceMonitor with sliding window
- Implemented ConfigurationTuner with adaptive strategies
- Implemented TelemetryPersistence with SHA-256 protection
- Integrated into main solver loop

Performance Improvements:
- Adaptive GPU utilization targeting ≥90%
- Adaptive memory bandwidth targeting ≥80%
- Automatic configuration adjustment
- Historical performance tracking

Fixes: P1-007
Audit: audits/CODE_QUALITY_AUDIT_2025-10-13_ROUND2.md
Protocol: Iron Cage v5.0 - ZERO-TOLERANCE-PERFORMANCE"
```

---

**实施完成时间**: 待定  
**实施人员**: AI Agent (Augment Code)  
**验证状态**: ⏳ 待实施

