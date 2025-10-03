# Warp Authentication 工具使用说明

## 🚀 命令行工具

### 基本用法
```bash
# 查看token状态
python warp_auth.py --status

# 获取新token
python warp_auth.py --new

# 刷新现有token
python warp_auth.py --refresh

# 验证当前token
python warp_auth.py --validate
```

### 高级用法
```bash
# 设置重试参数
python warp_auth.py --new --attempts 3 --retry-delay 2.0

# 增加重试次数和延迟
python warp_auth.py --new --attempts 5 --retry-delay 3.0 --max-delay 60
```

### 输出示例

#### Token状态检查
```bash
python warp_auth.py --status
```
```
📋 Token Status:
   JWT: ✅ Present
   Refresh Token: ✅ Present
   JWT Status: ✅ Valid
   JWT Expires: 2025-09-29 13:20:53
   Refresh Token: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW...
```

#### 完整Token信息显示
```bash
python warp_auth.py --new
```

#### 批量匿名注册
```bash
# 创建单个新匿名账户
python warp_auth.py --new-account

# 批量创建多个匿名账户（推荐使用代理）
python warp_auth.py --batch 5 --batch-delay 10.0 --proxy --proxy-host 127.0.0.1 --proxy-port 10809

# 使用SOCKS5代理批量注册
python warp_auth.py --batch 3 --batch-delay 15.0 --proxy --proxy-type socks5 --proxy-host 127.0.0.1 --proxy-port 10808
```

**重要说明:**
- `--refresh`: 使用现有的refresh_token获取新的access_token（会得到相同的用户身份）
- `--new-account`: 创建全新的匿名用户（每次都会获得不同的用户身份）
- `--batch`: 批量创建多个独立的匿名账户
- `--proxy`: 启用代理（强烈建议用于批量注册以避免429错误）
- `--batch-delay`: 增加延迟时间以避免速率限制（网络不好时建议15-30秒）

## 📦 账号存储管理

### 查看保存的账号
```bash
python warp_auth.py --list-accounts
```

### 导出账号数据
```bash
# 导出为JSON格式
python warp_auth.py --export-accounts accounts.json

# 导出为CSV格式
python warp_auth.py --export-accounts accounts.csv

# 导出为TXT格式
python warp_auth.py --export-accounts accounts.txt
```

**账号保存位置:**
- 默认: `./.env`
- warp2xyz项目: `./warp2xyz/.env`

**代理配置参数:**
- `--proxy`: 启用代理
- `--proxy-host`: 代理地址（默认: 127.0.0.1）
- `--proxy-port`: 代理端口（默认: 10809）
- `--proxy-type`: 代理类型（http/https/socks5/socks5h，默认: http）
```
================================================================================
📋 完整Token信息:
JWT Token: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMiLCJ0eXAiOiJKV1QifQ...
Refresh Token: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCPumbZr2gYtBjNHpkFPKkOsROtwgep0Z71YZ2eIZsXWJdFKxgJ9GzmwKRvB9Rp21axIazg8ZILpq4EehSmr-5BSh6CHwGPOigaPoyKiMbJgM52v2hqVfZo44WE_mQ88bdvmd6PZN
📦 JWT Payload:
   iss: https://securetoken.google.com/astral-field-294621
   aud: astral-field-294621
   auth_time: 1759119106
   user_id: jU5Y7j9SlpNeawVskqWGbccPHwA3
   sub: jU5Y7j9SlpNeawVskqWGbccPHwA3
   iat: 1759119653
   exp: 1759123253
   firebase: {'identities': {}, 'sign_in_provider': 'custom'}
⏰ Token过期时间: 2025-09-29 13:20:53
================================================================================
```

## 🖥️ GUI界面使用说明

### 🖥️ 界面介绍

这是一个基于 Tkinter 的图形界面，用于 Warp 匿名认证，支持 V2RAY 代理配置。

### 主要功能

- **代理配置**: 支持 HTTP/HTTPS/SOCKS5 代理，默认本地 10809 端口
- **系统代理检测**: 自动检测系统代理设置并填充配置
- **认证管理**: 获取、刷新、验证 JWT Token
- **重试设置**: 可配置重试次数和延迟时间
- **实时日志**: 显示认证过程的详细日志，**完整显示所有Token信息**
- **设置持久化**: 自动保存和加载配置

## 🚀 快速开始

### 1. 安装依赖
```bash
pip install -r requirements.txt
```

### 2. 启动 GUI
```bash
# 方法1: 直接运行GUI
python warp_auth_gui.py

# 方法2: 使用启动器
python launch_gui.py
```

### 3. 配置代理
- 系统默认代理: `http://127.0.0.1:10809`
- 点击 "检测系统代理" 自动填充
- 或手动填写代理信息

### 4. 开始认证
- 切换到 "认证" 标签页
- 点击 "开始认证" 按钮
- 观察日志输出认证进度

## 📋 界面说明

### 📌 代理配置标签页
- **启用代理**: 是否使用代理
- **代理类型**: HTTP/HTTPS/SOCKS5/SOCKS5H
- **代理地址**: 代理服务器地址
- **代理端口**: 代理服务器端口
- **测试代理连接**: 测试代理是否可用
- **检测系统代理**: 自动检测系统代理设置

### 🔐 认证标签页
- **认证状态**: 显示当前认证状态
- **JWT Token**: 显示获取到的Token
- **开始认证**: 启动认证流程
- **刷新Token**: 刷新现有的Token
- **验证Token**: 检查Token是否有效
- **认证日志**: 显示详细的认证过程日志

### ⚙️ 设置标签页
- **重试设置**: 配置最大尝试次数、基础延迟、最大延迟
- **环境信息**: 显示环境变量信息
- **保存设置**: 保存当前配置到文件
- **恢复默认**: 恢复默认设置

## 🔧 配置说明

### 代理设置
```
类型: http/https/socks5/socks5h
地址: 127.0.0.1 (默认)
端口: 10809 (默认)
```

### 重试设置
- **最大尝试次数**: 1-10 次
- **基础延迟**: 0.5-10.0 秒
- **最大延迟**: 10-300 秒

### 环境变量
- `WARP_REFRESH_URL`: Warp 刷新Token的URL
- `WARP_JWT`: 已获取的JWT Token
- `HTTP_PROXY`/`HTTPS_PROXY`: 系统代理设置

## 🎯 使用建议

1. **首次使用**: 点击 "检测系统代理" 自动配置
2. **认证失败**: 检查代理设置，增加重试次数
3. **Token过期**: 使用 "刷新Token" 功能
4. **网络问题**: 调整代理类型和端口设置

## 📁 文件说明

- `warp_auth_gui.py`: 主GUI程序
- `warp_auth.py`: 认证核心逻辑
- `launch_gui.py`: GUI启动器
- `gui_settings.json`: GUI配置文件
- `.env`: Token存储文件
- `requirements.txt`: 依赖列表

## 🐛 常见问题

### Q: 代理连接失败
A: 检查代理软件是否正常运行，确认端口设置正确

### Q: 认证一直失败
A: 尝试不同的代理类型，增加重试次数和延迟

### Q: Token无法保存
A: 检查当前目录是否有写入权限

### Q: 界面无响应
A: 认证过程中请耐心等待，查看日志了解进度

## 🌟 特色功能

1. **智能代理检测**: 自动识别系统代理设置
2. **实时状态显示**: 认证过程实时可视化
3. **配置持久化**: 设置自动保存和加载
4. **多协议支持**: 支持多种代理协议
5. **重试机制**: 智能重试和错误处理
6. **详细日志**: 完整的操作日志记录

## 📝 更新日志

- v1.0: 基础GUI界面和代理支持
- v1.1: 添加系统代理检测和配置持久化
- v1.2: 优化认证流程和错误处理