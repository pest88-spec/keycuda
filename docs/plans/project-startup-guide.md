# 超级比特币谜题碰撞器 - 项目启动指南

**文档版本**: v1.0  
**创建日期**: 2025-10-12  
**适用对象**: 项目维护者、开发者  

---

## 🎯 项目启动方案选择

### 方案对比

| 方案 | 优势 | 劣势 | 推荐度 |
|------|------|------|--------|
| **方案1: 在当前PuzzleKeyhunt基础上演进** | 1. 保留现有代码和历史<br>2. 渐进式重构，风险低<br>3. 可复用现有CI/CD<br>4. 保持Git历史连续性 | 1. 需要重构现有代码<br>2. 可能有技术债务 | ⭐⭐⭐⭐⭐ |
| **方案2: 新建SuperBitcoinPuzzleSolver独立项目** | 1. 全新架构，无历史包袱<br>2. 严格遵循铁笼协议<br>3. 清晰的项目边界 | 1. 从零开始，工作量大<br>2. 丢失现有代码<br>3. 需要重新配置CI/CD | ⭐⭐⭐ |

**推荐方案**: **方案1 - 在当前PuzzleKeyhunt基础上演进**

---

## 📋 方案1: 在当前PuzzleKeyhunt基础上演进（推荐）

### 阶段1: 准备工作（1周）

**步骤1.1: 创建演进分支**
```bash
cd d:/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt

# 创建演进分支
git checkout -b feature/super-solver-fusion

# 创建备份分支（以防需要回滚）
git checkout -b backup/pre-fusion-$(date +%Y%m%d)
git checkout feature/super-solver-fusion
```

**步骤1.2: 创建融合计划目录结构**
```bash
# 在当前项目中创建新目录
mkdir -p external/{VanitySearch,BitCrack,Keyhunt,secp256k1,nccl}
mkdir -p src/Core/{ECC,Hash,Memory}
mkdir -p src/Algorithm/{BruteForce,BSGS,Kangaroo}
mkdir -p src/GPU/{Kernels,Scheduler,Monitor}
mkdir -p src/Storage/{Checkpoint,Result,Telemetry}
mkdir -p src/Interface/{CLI,Config,API}
mkdir -p tests/{unit,integration,validation,performance,fuzz}
mkdir -p benchmarks/{baselines,telemetry}
mkdir -p audit
mkdir -p scripts/{build,test,ci,replay,digest}
mkdir -p docs/{architecture,api,validation,plans}

# 创建.gitkeep文件保持空目录
find . -type d -empty -exec touch {}/.gitkeep \;
```

**步骤1.3: 更新.gitignore**
```bash
cat >> .gitignore << 'EOF'

# 融合项目特定忽略
external/*/build/
external/*/.git/
audit/*.jsonl
checkpoints/*.dat
checkpoints/*.json
results/*.txt
benchmarks/telemetry/*.jsonl
build/
*.o
*.so
*.a
*.exe
EOF
```

**步骤1.4: 创建项目文档**
```bash
# 复制融合计划到项目
cp docs/plans/super-bitcoin-puzzle-solver-fusion-plan.md \
   docs/plans/FUSION_PLAN.md

# 复制合规性检查清单
cp docs/plans/fusion-plan-compliance-checklist.md \
   docs/plans/COMPLIANCE_CHECKLIST.md

# 创建README更新
cat > docs/plans/FUSION_README.md << 'EOF'
# SuperBitcoinPuzzleSolver 融合项目

本项目是PuzzleKeyhunt的演进版本，融合了VanitySearch、BitCrack、Keyhunt的优势。

## 项目状态

- **当前阶段**: 阶段1 - 基础设施搭建
- **完成度**: 0%
- **预计完成时间**: 2026-05-12 (7.5个月)

## 快速开始

详见 [FUSION_PLAN.md](FUSION_PLAN.md)

## 合规性

详见 [COMPLIANCE_CHECKLIST.md](COMPLIANCE_CHECKLIST.md)
EOF
```

**工作量**: 8小时  

---

### 阶段2: 第三方库克隆（2天）

**步骤2.1: 克隆VanitySearch**
```bash
cd external/
git clone https://github.com/JeanLucPons/VanitySearch.git
cd VanitySearch
git checkout v1.19  # 锁定版本
git log -1 --format="%H" > ../vanitysearch.commit
cd ../..

# 记录版本信息
echo "VanitySearch v1.19 ($(cat external/vanitysearch.commit))" >> docs/reference-sources.md
```

**步骤2.2: 克隆BitCrack**
```bash
cd external/
git clone https://github.com/brichard19/BitCrack.git
cd BitCrack
git checkout v0.31  # 锁定版本
git log -1 --format="%H" > ../bitcrack.commit
cd ../..

# 记录版本信息
echo "BitCrack v0.31 ($(cat external/bitcrack.commit))" >> docs/reference-sources.md
```

**步骤2.3: 克隆Keyhunt**
```bash
cd external/
git clone https://github.com/albertobsd/keyhunt.git
cd keyhunt
git checkout main  # 锁定到最新稳定版
git log -1 --format="%H" > ../keyhunt.commit
cd ../..

# 记录版本信息
echo "Keyhunt main ($(cat external/keyhunt.commit))" >> docs/reference-sources.md
```

**步骤2.4: 克隆bitcoin-core/secp256k1**
```bash
cd external/
git clone https://github.com/bitcoin-core/secp256k1.git
cd secp256k1
git checkout v0.4.0  # 锁定版本
git log -1 --format="%H" > ../secp256k1.commit

# 编译安装
./autogen.sh
./configure --enable-module-recovery --enable-module-ecdh
make -j$(nproc)
sudo make install

cd ../..

# 记录版本信息
echo "bitcoin-core/secp256k1 v0.4.0 ($(cat external/secp256k1.commit))" >> docs/reference-sources.md
```

**步骤2.5: 克隆NCCL**
```bash
cd external/
git clone https://github.com/NVIDIA/nccl.git
cd nccl
git checkout v2.20.5  # 锁定版本
git log -1 --format="%H" > ../nccl.commit

# 编译安装
make -j$(nproc) src.build
sudo make install

cd ../..

# 记录版本信息
echo "NVIDIA/nccl v2.20.5 ($(cat external/nccl.commit))" >> docs/reference-sources.md
```

**步骤2.6: 生成SHA-256摘要**
```bash
# 为每个库生成SHA-256摘要
cd external/
for dir in VanitySearch BitCrack keyhunt secp256k1 nccl; do
    cd $dir
    git archive HEAD | sha256sum > ../$dir.sha256
    cd ..
done
cd ..

# 记录到文档
cat external/*.sha256 >> docs/reference-sources.md
```

**工作量**: 16小时  

---

### 阶段3: CMake集成（3天）

**步骤3.1: 创建主CMakeLists.txt**
```bash
# 备份现有CMakeLists.txt
cp CMakeLists.txt CMakeLists.txt.backup

# 创建新的CMakeLists.txt（基于融合计划）
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(SuperBitcoinPuzzleSolver LANGUAGES CXX CUDA)

# 铁笼协议：C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 铁笼协议：CUDA架构支持
set(CMAKE_CUDA_ARCHITECTURES "75;86;89;90")

# 铁笼协议：编译器警告（零容忍）
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# 铁笼协议：优化标志
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CUDA_FLAGS_RELEASE "-O3 --use_fast_math -DNDEBUG")

# 铁笼协议：Sanitizers（调试模式）
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_compile_options(-fsanitize=address,undefined,thread,memory)
    add_link_options(-fsanitize=address,undefined,thread,memory)
endif()

# 依赖管理（铁笼协议：SHA256校验）
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)

FetchContent_MakeAvailable(nlohmann_json googletest)

# 引用源（铁笼协议：不造轮子）
add_subdirectory(external/VanitySearch)
add_subdirectory(external/BitCrack)
add_subdirectory(external/Keyhunt)

# 核心库
add_library(sbps_core
    src/Core/ECC/glv_endomorphism.cpp
    src/Core/ECC/batch_inverse.cpp
    src/Core/ECC/unified_ecc_engine.cpp
    src/Core/Hash/sha256.cu
    src/Core/Hash/ripemd160.cu
    src/Core/Hash/unified_hash_engine.cpp
    src/Core/Memory/gpu_memory_pool.cpp
)

target_link_libraries(sbps_core
    PRIVATE
        VanitySearch::secp256k1
        BitCrack::cudaMath
        Keyhunt::util
        CUDA::cudart
)

# 主程序
add_executable(sbps
    src/Interface/cli_interface.cpp
    src/Interface/config_manager.cpp
)

target_link_libraries(sbps
    PRIVATE
        sbps_core
        nlohmann_json::nlohmann_json
)

# 测试（铁笼协议：测试优先）
enable_testing()

add_executable(sbps_unit_tests
    tests/unit/core/test_glv_endomorphism.cu
    tests/unit/core/test_batch_inverse.cpp
)

target_link_libraries(sbps_unit_tests
    PRIVATE
        sbps_core
        GTest::gtest_main
)

add_test(NAME UnitTests COMMAND sbps_unit_tests)

# 安装
install(TARGETS sbps DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/sbps)
install(DIRECTORY docs/ DESTINATION share/doc/sbps)
EOF
```

**步骤3.2: 创建external/*/CMakeLists.txt**
```bash
# 为每个第三方库创建CMakeLists.txt
# （详见融合计划第1401-1641行）

# VanitySearch
cat > external/VanitySearch/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(VanitySearch LANGUAGES CXX CUDA)

add_library(VanitySearch_secp256k1 STATIC
    SECP256k1.cpp
    SECP256k1.h
    Int.cpp
    Int.h
    IntGroup.cpp
    IntGroup.h
    Point.cpp
    Point.h
)

target_include_directories(VanitySearch_secp256k1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(VanitySearch::secp256k1 ALIAS VanitySearch_secp256k1)
EOF

# BitCrack
cat > external/BitCrack/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(BitCrack LANGUAGES CXX CUDA)

add_library(BitCrack_cudaMath STATIC
    cudaMath/secp256k1.cu
    cudaMath/secp256k1.cuh
    cudaMath/ptx_asm.cuh
)

target_include_directories(BitCrack_cudaMath PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(BitCrack::cudaMath ALIAS BitCrack_cudaMath)
EOF

# Keyhunt
cat > external/Keyhunt/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(Keyhunt LANGUAGES C CUDA)

add_library(Keyhunt_util STATIC
    util.c
)

target_include_directories(Keyhunt_util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(Keyhunt::util ALIAS Keyhunt_util)
EOF
```

**步骤3.3: 测试编译**
```bash
# 创建构建目录
mkdir -p build
cd build

# 配置
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译
make -j$(nproc)

# 检查编译结果
ls -lh sbps
```

**工作量**: 24小时  

---

### 阶段4: CI/CD配置（2天）

**步骤4.1: 创建GitHub Actions工作流**
```bash
mkdir -p .github/workflows

# 复制CI配置（详见融合计划第1073-1226行）
# 这里创建最小CI配置

cat > .github/workflows/ci-build-test.yml << 'EOF'
name: CI Build and Test

on:
  push:
    branches: [ feature/super-solver-fusion ]
  pull_request:
    branches: [ main ]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v3
      with:
        submodules: recursive
    
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake ninja-build libsecp256k1-dev
    
    - name: Build
      run: |
        mkdir build && cd build
        cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
        ninja -j$(nproc)
    
    - name: Run tests
      run: |
        cd build
        ctest --output-on-failure
EOF
```

**工作量**: 16小时  

---

## 📊 方案1总启动时间

| 阶段 | 工作量 | 说明 |
|------|--------|------|
| 准备工作 | 8h | 创建分支、目录结构、文档 |
| 第三方库克隆 | 16h | 克隆5个库、版本锁定、SHA-256 |
| CMake集成 | 24h | 主CMakeLists.txt、external集成 |
| CI/CD配置 | 16h | GitHub Actions工作流 |
| **总计** | **64h** | **8个工作日** |

---

## 🚀 启动后的下一步

完成上述启动步骤后，即可按照融合计划执行：

1. **阶段2: 核心层提取与适配**（6周）
2. **阶段3: 算法层提取与实现**（8周）
3. **阶段4: GPU层实现**（4周）
4. **阶段5: 存储层与接口层**（3周）
5. **阶段6: 集成测试与优化**（4周）

---

**建议**: 使用方案1，在当前PuzzleKeyhunt项目基础上演进，保留历史，渐进式重构，风险最低。

**启动命令**:
```bash
cd d:/mybitcoin/puzzlekeyhunt/PuzzleKeyhunt
git checkout -b feature/super-solver-fusion
# 然后按照本指南执行步骤1.2-4.1
```

---

**文档作者**: AI Agent (Augment Code)  
**文档版本**: v1.0  
**创建日期**: 2025-10-12

