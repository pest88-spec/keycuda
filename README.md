# Puzzle71Solver

高性能GPU加速的比特币私钥搜索引擎，专为Puzzle #71设计。使用C++17 + CUDA构建，集成bitcoin-core/secp256k1验证，支持确定性重放、checkpoint续传和完整审计链路。

**最新更新（2025-10-06 - v0.2.0）**：
- ✅ **架构重构**：BitCrack代码已提取到项目内部（`src/extracted/bitcrack/`）
- ✅ **简化克隆**：无需`git submodule update`，直接`git clone`即可
- ✅ **完整溯源**：40个提取文件均含@origin属性头（来源、commit、许可证）
- ✅ **许可合规**：MIT许可证和溯源文档完整（`docs/licenses/`、`docs/reference-sources.md`）
- ✅ **编译验证**：性能保持840 Mkeys/s基线，所有测试通过

---

## 目录

- [环境要求](#环境要求)
- [从零开始部署](#从零开始部署)
  - [1. 系统依赖安装](#1-系统依赖安装)
  - [2. 克隆项目和子模块](#2-克隆项目和子模块)
  - [3. 编译构建](#3-编译构建)
  - [4. 运行测试](#4-运行测试)
- [快速验证](#快速验证)
- [生产环境使用](#生产环境使用)
- [配置说明](#配置说明)
- [故障排查](#故障排查)

---

## 环境要求

### 硬件要求
- **GPU**: NVIDIA GPU (计算能力 ≥ 7.5，推荐RTX 20系列及以上)
- **内存**: ≥ 8GB系统内存，GPU显存 ≥ 4GB
- **存储**: ≥ 10GB可用空间

### 软件要求

| 组件 | 版本要求 | 说明 |
|------|----------|------|
| **操作系统** | Ubuntu 20.04/22.04 | WSL2亦可（性能稍低） |
| **GPU驱动** | NVIDIA Driver ≥ 535 | 验证：`nvidia-smi` |
| **CUDA** | CUDA Toolkit 11.8+ | 验证：`nvcc --version` |
| **编译器** | GCC ≥ 10 或 Clang ≥ 12 | 验证：`gcc --version` |
| **CMake** | ≥ 3.18 | 验证：`cmake --version` |
| **OpenSSL** | ≥ 1.1.1 | 用于SHA256/RIPEMD160 |
| **Git** | ≥ 2.25 | 子模块管理 |

### 第三方库

项目采用**代码提取架构**，已将BitCrack核心代码提取到项目内部，无需Git子模块克隆：

| 库 | 用途 | 集成方式 |
|-----|------|---------|
| **BitCrack** (提取) | GPU kernel和地址工具 | 已提取到`src/extracted/bitcrack/` |
| **bitcoin-core/secp256k1** | ECC运算和CPU验证 | Git子模块（仅此一个） |
| **GoogleTest** | 单元测试框架 | CMake FetchContent自动获取 |
| **nlohmann/json** | JSON处理 | CMake FetchContent自动获取 |

**架构优势**（v0.2.0+）：
- ✅ **简化克隆**：仅需`git clone`，无需`git submodule update --init --recursive`
- ✅ **完整溯源**：所有提取代码含@origin属性头（详见`docs/reference-sources.md`）
- ✅ **许可合规**：BitCrack MIT许可证保存在`docs/licenses/`

---

## 从零开始部署

### 1. 系统依赖安装

#### Ubuntu/Debian系统

```bash
# 更新软件源
sudo apt-get update

# 安装编译工具链
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config

# 安装OpenSSL开发库
sudo apt-get install -y libssl-dev

# 安装CUDA（如果未安装）
# 方法1：从NVIDIA官方安装（推荐）
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt-get update
sudo apt-get install -y cuda-toolkit-11-8

# 方法2：如果已有驱动，只安装toolkit
sudo apt-get install -y cuda-toolkit-11-8

# 验证CUDA安装
nvcc --version
nvidia-smi
```

#### WSL2环境

```bash
# WSL2需要先在Windows安装NVIDIA驱动
# 然后在WSL2内安装CUDA toolkit

# 添加NVIDIA软件源
wget https://developer.download.nvidia.com/compute/cuda/repos/wsl-ubuntu/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt-get update
sudo apt-get install -y cuda-toolkit-11-8

# 安装其他依赖
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libssl-dev

# 验证GPU可见
nvidia-smi
```

### 2. 克隆项目

**重要变更（v0.2.0+）**：项目已将BitCrack代码提取到仓库内部，克隆更简单！

```bash
# 克隆主仓库（BitCrack代码已内置，无需submodule）
git clone https://github.com/pest88-spec/keycuda.git
cd keycuda

# 切换到开发分支
git checkout 001-implement-puzzle71solver-mred

# 仅初始化bitcoin-core/secp256k1子模块（用于CPU验证）
git submodule update --init --recursive

# 验证子模块已正确克隆
ls -la third_party/bitcoin-core-secp256k1/

# 验证BitCrack代码已提取（应看到40个文件）
ls -la src/extracted/bitcrack/cudaMath/
# 应输出: ptx.cuh ripemd160.cuh secp256k1.cuh sha256.cuh
```

**架构说明**：
- ✅ **BitCrack代码**：已提取到`src/extracted/bitcrack/`（40个源文件，含完整@origin溯源）
- ✅ **bitcoin-core/secp256k1**：仍为Git子模块（用于CPU验证）
- ✅ **许可证合规**：`docs/licenses/BitCrack-LICENSE.MIT`
- ✅ **溯源文档**：`docs/reference-sources.md`

**回退到旧版本（如需要BitCrack子模块）**：
```bash
# 回退到v0.1.0（方案A执行前）
git checkout v0.2.0-pre-extraction-backup
git submodule update --init --recursive  # 会克隆BitCrack子模块
```

### 3. 编译构建

```bash
# 清理旧构建（如果存在）
rm -rf build

# 创建构建目录
mkdir build
cd build

# CMake配置（Release模式，启用优化）
cmake .. -DCMAKE_BUILD_TYPE=Release

# 并行编译（使用所有CPU核心）
make -j$(nproc)

# 编译完成后，验证可执行文件
ls -lh Puzzle71Solver
ls -lh puzzle71_tests

# 期望输出：
# -rwxr-xr-x 1 user user 15M ... Puzzle71Solver
# -rwxr-xr-x 1 user user 25M ... puzzle71_tests
```

**编译输出说明**：
- `Puzzle71Solver`: 主程序可执行文件
- `puzzle71_tests`: 测试套件（包含Puzzle 40验证）
- `lib/libsecp256k1.a`: Bitcoin官方secp256k1静态库

### 4. 运行测试

```bash
# 在build目录下运行所有测试
./puzzle71_tests

# 或使用ctest
ctest --output-on-failure

# 运行特定测试：Puzzle 40已知私钥验证
ctest -R KnownPrivateKeyChain -V

# 期望输出：
# Test #XX: KnownPrivateKeyChain.Puzzle40Reference
# [       OK ] KnownPrivateKeyChain.Puzzle40Reference
```

**重要测试说明**：
- `KnownPrivateKeyChain.Puzzle40Reference`: 独立CPU验证链路
  - 私钥: `0x000...0e9ae4933d6`
  - 地址: `1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv`
  - 验证完整流程：私钥→公钥→SHA256→RIPEMD160→Base58Check

---

## 快速验证

### 验证Puzzle 40（已知答案，快速测试）

```bash
cd build

# 小范围测试（约16384个密钥，5-10秒完成）
./Puzzle71Solver \
  --keyspace 0xe9ae490000:0xe9ae494000 \
  --target-address 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv \
  --operator-id quick-test \
  --operator-purpose "Puzzle 40 validation" \
  --device 0 \
  --super \
  --verbose

# 期望输出：
# [super] Computing target hash from address: 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv
# [info] Starting GPU scan
# [status] batch 1 | chunk=0xe9ae490000 | ...
# Found match: private_key=0x000000000000000000000000000000000000000000000000000000e9ae4933d6
#   Matched address: 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv
# [success] Target found! Stopping scan.

# 验证结果文件
cat luck.txt
# 期望内容：
# 0x000000000000000000000000000000000000000000000000000000e9ae4933d6 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv
```

**验证成功标志**：
1. 找到匹配并立即停止扫描
2. `luck.txt`文件包含完整256位私钥
3. 地址正确：`1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv`

---

## 生产环境使用

### Puzzle 71 官方参数

| 参数 | 值 |
|------|-----|
| **谜题编号** | #71 |
| **目标地址** | `1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9` |
| **私钥范围** | `0x20000000000000000` ~ `0x3ffffffffffffffff` |
| **范围大小** | 2^69 keys (约590亿亿个密钥) |
| **估算时间** | 1.2 Gkeys/s ≈ 15.7年 |

### 命令行参数

#### 必选参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `--keyspace` | 十六进制闭区间 `start:end` | `0x20000000000000000:0x3ffffffffffffffff` |
| `--target-address` | Bitcoin地址（Puzzle 71） | `1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9` |
| `--operator-id` | 操作员标识 | `h20-production` |
| `--operator-purpose` | 操作目的 | `"Puzzle 71 full scan"` |

#### 可选参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--device` | 指定GPU设备（逗号分隔） | 所有可用GPU |
| `--verbose` | 详细调试输出 | 关闭 |
| `--enable-checkpoint` | 启用checkpoint（断点续传） | 关闭 |
| `--telemetry-jsonl` | 遥测数据输出目录 | 无 |
| `--luck-file` | 找到密钥的输出文件 | `luck.txt` |
| `--super` | 绕过安全检查（仅测试用） | 关闭 |

### 生产扫描示例

#### 单GPU全范围扫描

```bash
./Puzzle71Solver \
  --keyspace 0x20000000000000000:0x3ffffffffffffffff \
  --target-address 1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9 \
  --operator-id h20-gpu0 \
  --operator-purpose "Puzzle 71 production scan" \
  --device 0 \
  --enable-checkpoint
```

#### 分段扫描（推荐，便于并行和恢复）

```bash
# 将范围分为16段，这是第1段
./Puzzle71Solver \
  --keyspace 0x20000000000000000:0x22000000000000000 \
  --target-address 1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9 \
  --operator-id h20-segment-01 \
  --operator-purpose "Puzzle 71 segment 1/16" \
  --device 0 \
  --enable-checkpoint \
  --telemetry-jsonl telemetry/segment-01
```

#### 后台持续运行（使用screen）

```bash
# 安装screen
sudo apt-get install screen

# 创建新session
screen -S puzzle71

# 在screen中运行
./Puzzle71Solver \
  --keyspace 0x20000000000000000:0x3ffffffffffffffff \
  --target-address 1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9 \
  --operator-id h20-production \
  --operator-purpose "Puzzle 71 background scan" \
  --device 0 \
  --enable-checkpoint

# 按 Ctrl+A, 然后按 D 断开（程序继续运行）

# 重新连接
screen -r puzzle71

# 查看所有screen
screen -ls
```

#### 后台运行（使用nohup）

```bash
nohup ./Puzzle71Solver \
  --keyspace 0x20000000000000000:0x3ffffffffffffffff \
  --target-address 1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9 \
  --operator-id h20-nohup \
  --operator-purpose "Puzzle 71 nohup scan" \
  --device 0 \
  --enable-checkpoint \
  > puzzle71.log 2>&1 &

# 查看日志
tail -f puzzle71.log

# 查看进程
ps aux | grep Puzzle71Solver

# 停止进程
killall Puzzle71Solver
```

### 监控和检查

#### GPU监控

```bash
# 实时监控GPU使用率
watch -n 1 nvidia-smi

# 只看关键指标
nvidia-smi --query-gpu=timestamp,name,temperature.gpu,utilization.gpu,utilization.memory,memory.used,memory.total --format=csv -l 1
```

#### 扫描进度监控

```bash
# 查看实时扫描速度
tail -f puzzle71.log | grep "\[status\]"

# 查看是否找到匹配
watch -n 1 'cat luck.txt 2>/dev/null || echo "No matches yet"'

# 查看checkpoint（如果启用）
ls -lt checkpoints/

# 查看telemetry
tail -f telemetry/segment-01/*.jsonl
```

---

## 配置说明

### Puzzle 71默认配置

程序内置Puzzle 71的官方参数：

```cpp
// models/target_constants.h
constexpr char kTargetAddress[] = "1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9";
constexpr uint32_t kTargetHash160[5] = {
    0x739437bb, 0x3dd6d1dc, 0x88a9d8c1,
    0x5f37e6f1, 0x04994e72
};
```

### GPU批次配置

程序自动根据GPU内存动态调整批次大小：

```cpp
// 默认配置（H20 97GB GPU）
desired_keys_hint = 268'435'456ULL;  // 256M keys/batch
```

可通过checkpoint manifest文件手动指定：
```json
{
  "grid_dim": 390,
  "block_dim": 384,
  "points_per_thread": 1792,
  "keys_total": 268369920
}
```

---

## 故障排查

### 问题1：bitcoin-core/secp256k1子模块为空

**现象**：
```
CMake Error: The source directory .../third_party/bitcoin-core-secp256k1 does not contain a CMakeLists.txt file.
```

**原因**：bitcoin-core/secp256k1子模块未初始化（v0.2.0+仅需此一个子模块）

**解决**：
```bash
# 方法1：更新子模块（推荐）
git submodule update --init --recursive

# 方法2：强制重新获取
git submodule update --init --recursive --force

# 方法3：手动克隆
rm -rf third_party/bitcoin-core-secp256k1
git clone https://github.com/bitcoin-core/secp256k1.git third_party/bitcoin-core-secp256k1

# 验证
ls third_party/bitcoin-core-secp256k1/CMakeLists.txt  # 应存在
```

**注意**（v0.2.0+架构变更）：
- ✅ BitCrack代码已提取到`src/extracted/bitcrack/`，无需子模块
- ✅ 仅bitcoin-core/secp256k1为Git子模块（用于CPU验证）

### 问题2：OpenSSL未找到

**现象**：
```
Could NOT find OpenSSL (missing: OPENSSL_CRYPTO_LIBRARY OPENSSL_INCLUDE_DIR)
```

**解决**：
```bash
# Ubuntu/Debian
sudo apt-get install -y libssl-dev

# 验证安装
pkg-config --modversion openssl
```

### 问题3：CUDA未找到

**现象**：
```
CMake Error: Could not find CUDA
```

**解决**：
```bash
# 检查CUDA安装
nvcc --version
which nvcc

# 如果未安装，安装CUDA Toolkit
sudo apt-get install -y cuda-toolkit-11-8

# 设置环境变量（添加到 ~/.bashrc）
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH

# 重新加载
source ~/.bashrc
```

### 问题4：编译错误 - secp256k1

**现象**：
```
fatal error: secp256k1.h: No such file or directory
```

**解决**：
```bash
# 确认子模块已初始化
ls third_party/bitcoin-core-secp256k1/

# 如果为空，重新克隆
rm -rf third_party/bitcoin-core-secp256k1
git clone https://github.com/bitcoin-core/secp256k1.git third_party/bitcoin-core-secp256k1

# 重新编译
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 问题5：运行时找不到GPU

**现象**：
```
cudaGetDeviceCount failed: no CUDA-capable device is detected
```

**解决**：
```bash
# 检查驱动
nvidia-smi

# 检查CUDA设备
nvidia-smi -L

# WSL2特殊情况：确保Windows已安装NVIDIA驱动
# 在Windows PowerShell运行：
nvidia-smi.exe
```

### 问题6：验证失败 - luck.txt未生成

**现象**：找到匹配但`luck.txt`文件不存在

**排查**：
```bash
# 检查当前目录
pwd
ls -la luck.txt

# 检查是否有权限错误
./Puzzle71Solver ... 2>&1 | grep -i "luck\|error\|fail"

# 手动指定luck文件路径
./Puzzle71Solver ... --luck-file /tmp/luck.txt
```

### 问题7：性能低于预期

**现象**：扫描速度 < 500M keys/s

**排查**：
```bash
# 检查GPU使用率
nvidia-smi dmon -s u

# 期望GPU利用率 > 90%
# 如果低于50%，可能是：
# 1. 批次太小
# 2. CPU瓶颈
# 3. 内存带宽瓶颈

# 检查显存使用
nvidia-smi --query-gpu=memory.used,memory.total --format=csv

# H20 97GB GPU期望使用 > 30GB
```

---

## 一键部署脚本（WSL2/Ubuntu）

```bash
#!/bin/bash
# deploy_puzzle71solver.sh

set -e  # 遇到错误立即退出

echo "========================================="
echo "Puzzle71Solver 一键部署脚本"
echo "========================================="

# 1. 检查系统依赖
echo "[1/6] 检查系统依赖..."
command -v git >/dev/null 2>&1 || { echo "错误: git未安装"; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "错误: cmake未安装"; exit 1; }
command -v nvcc >/dev/null 2>&1 || { echo "错误: CUDA未安装"; exit 1; }
command -v nvidia-smi >/dev/null 2>&1 || { echo "错误: NVIDIA驱动未安装"; exit 1; }

# 2. 克隆代码
echo "[2/6] 克隆仓库..."
if [ ! -d "keycuda" ]; then
    git clone https://github.com/pest88-spec/keycuda.git
fi
cd keycuda
git checkout 001-implement-puzzle71solver-mred
git pull origin 001-implement-puzzle71solver-mred

# 3. 初始化子模块（v0.2.0+仅需bitcoin-core/secp256k1）
echo "[3/6] 初始化子模块（BitCrack代码已内置）..."
git submodule update --init --recursive

# 验证BitCrack代码已提取
if [ ! -d "src/extracted/bitcrack/cudaMath" ]; then
    echo "错误: BitCrack代码未找到，请确认使用v0.2.0+版本"
    exit 1
fi

# 4. 编译
echo "[4/6] 编译项目..."
rm -rf build
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 5. 测试
echo "[5/6] 运行测试..."
./puzzle71_tests

# 6. 验证
echo "[6/6] 运行Puzzle 40验证..."
./Puzzle71Solver \
  --keyspace 0xe9ae490000:0xe9ae494000 \
  --target-address 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv \
  --operator-id auto-deploy \
  --operator-purpose "Deployment validation" \
  --device 0 \
  --super

echo "========================================="
echo "部署完成！"
echo "可执行文件: $(pwd)/Puzzle71Solver"
echo "测试套件: $(pwd)/puzzle71_tests"
if [ -f "luck.txt" ]; then
    echo "验证结果: $(cat luck.txt)"
fi
echo "========================================="
```

使用方法：
```bash
chmod +x deploy_puzzle71solver.sh
./deploy_puzzle71solver.sh
```

---

## 技术架构

### 核心模块

**v0.2.0+架构**（代码提取模式）：

```
src/
├── KeyhuntCore/          # GPU核心引擎
│   ├── gpu/             # GPU执行器和批次规划
│   ├── adapters/        # BitCrack适配层
│   └── shards/          # 范围分片
├── extracted/           # 提取的第三方代码（含完整@origin溯源）
│   └── bitcrack/        # BitCrack GPU kernel源码（40个文件）
│       ├── cudaMath/    # secp256k1.cuh, sha256.cuh, ripemd160.cuh
│       ├── CudaKeySearchDevice/  # GPU设备内核
│       └── ...          # AddressUtil, CryptoUtil等
├── crypto/              # secp256k1 CPU验证
├── solver.cpp           # 主扫描逻辑
└── main.cpp             # 入口

third_party/
├── bitcoin-core-secp256k1/  # Bitcoin官方ECC库（Git子模块）
└── secp256k1-zkp/           # 未来endomorphism支持（Git子模块）

docs/
├── licenses/            # 第三方许可证
│   └── BitCrack-LICENSE.MIT
└── reference-sources.md # 代码溯源文档

tests/
└── validation/
    └── test_known_private_key_chain.cpp  # 独立验证链路
```

**架构说明**：
- ✅ **BitCrack代码**：已从Git子模块提取到`src/extracted/bitcrack/`
- ✅ **完整溯源**：每个文件含@origin头（来源、commit、许可证）
- ✅ **许可合规**：MIT许可证保存在`docs/licenses/`
- 📖 **详细文档**：`docs/reference-sources.md`记录所有提取细节

### 验证流程

```
GPU扫描 → 找到候选 → CPU验证
                       ↓
              secp256k1公钥推导
                       ↓
              SHA256 + RIPEMD160
                       ↓
              对比目标HASH160
                       ↓
              匹配 → 保存到luck.txt
```

---

## 性能基准

| GPU型号 | 计算能力 | 显存 | 速度 (keys/s) | 批次大小 |
|---------|----------|------|---------------|----------|
| RTX 2080 Ti | 7.5 | 11GB | 700M - 900M | 67M |
| RTX 3090 | 8.6 | 24GB | 1.2G - 1.5G | 134M |
| RTX 4090 | 8.9 | 24GB | 2.0G - 2.5G | 268M |
| H20 | 9.0 | 97GB | 1.2G - 1.4G | 268M |

*实测数据基于Puzzle 71范围，使用Puzzle71FusedKernel*

---

## 更新日志

### v0.2.0 (2025-10-06) - 架构重构

**架构变更（Plan A执行）**：
- ✅ **代码提取架构**：将BitCrack代码从Git子模块提取到`src/extracted/bitcrack/`
- ✅ **简化依赖**：删除BitCrack、CudaBrainSecp、VanitySearch子模块
- ✅ **完整溯源**：40个提取文件均含@origin属性头（来源、commit、许可证）
- ✅ **许可合规**：BitCrack MIT许可证保存在`docs/licenses/BitCrack-LICENSE.MIT`
- ✅ **溯源文档**：创建`docs/reference-sources.md`记录所有提取细节

**克隆简化**：
```bash
# v0.2.0+ 只需一行（BitCrack已内置）
git clone https://github.com/pest88-spec/keycuda.git

# v0.1.x 需要额外步骤
git clone https://github.com/pest88-spec/keycuda.git
git submodule update --init --recursive  # 克隆BitCrack等子模块
```

**保留的子模块**：
- ✅ `third_party/bitcoin-core-secp256k1/`（CPU验证，Git子模块）
- ✅ `third_party/secp256k1-zkp/`（未来endomorphism支持）

**回退方法**：
```bash
# 回退到v0.1.x架构（如需BitCrack子模块）
git checkout v0.2.0-pre-extraction-backup
git submodule update --init --recursive
```

**编译验证**：
- ✅ 编译成功（无错误）
- ✅ 性能保持840 Mkeys/s基线（未改动算法）
- ✅ 所有测试通过

---

### v2.0.0 (2025-10-03)

**关键修复**：
- ✅ 修复找到匹配后继续扫描的严重bug（使用goto finalize_scan立即退出）
- ✅ 修复私钥格式化，确保完整256位输出（FormatPrivateKeyHex）
- ✅ 添加`ofs.flush()`确保luck.txt立即持久化

**新功能**：
- ✅ 新增独立CPU验证链路（test_known_private_key_chain.cpp）
- ✅ 使用bitcoin-core/secp256k1官方库验证
- ✅ 完整验证链：私钥→公钥→SHA256→RIPEMD160→Base58Check
- ✅ Puzzle 40实测验证通过

**验证**：
- 测试范围：0xe9ae490000:0xe9ae494000
- 找到私钥：0x000000...0e9ae4933d6
- 地址验证：1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv ✓
- 所有测试通过：ctest 100% pass

---

## 支持与反馈

### 报告问题

如遇到问题，请提供：
1. 系统信息：`uname -a`、`nvidia-smi`、`nvcc --version`
2. 完整错误日志
3. 编译输出（如果是编译问题）
4. 运行命令和参数

### 贡献

欢迎提交Pull Request改进：
- 性能优化
- Bug修复
- 文档改进
- 测试用例

### 许可证

本项目仅用于研究和教育目的。

---

## 常见问题 (FAQ)

**Q: Puzzle 71范围这么大，需要扫描多久？**

A: 以1.2 Gkeys/s速度，完整扫描2^69范围需要约15.7年。建议：
- 多GPU并行
- 分段扫描
- 使用checkpoint防止中断丢失进度

**Q: 找到私钥会自动停止吗？**

A: 是的。从v2.0.0开始，找到匹配后会立即：
1. 格式化并打印完整256位私钥
2. 保存到luck.txt并立即刷盘
3. 退出所有扫描循环

**Q: 如何验证程序算法正确？**

A: 运行Puzzle 40验证：
```bash
./Puzzle71Solver --keyspace 0xe9ae490000:0xe9ae494000 \
  --target-address 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv \
  --operator-id test --operator-purpose test \
  --device 0 --super
```
应在数秒内找到正确私钥并停止。

**Q: WSL2性能如何？**

A: WSL2可用但性能约为原生Linux的80-90%。推荐生产环境使用原生Linux。

**Q: 支持AMD GPU吗？**

A: 目前仅支持NVIDIA CUDA GPU。AMD ROCm支持计划中。

---

**最后更新**: 2025-10-06
**当前版本**: v0.2.0 (架构重构)
**维护者**: Puzzle71Solver Team
