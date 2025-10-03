# Warp 账号存储指南

## 📁 账号保存位置

注册完成的账号会同时保存在两个位置：

### 1. 结构化JSON文件 (主要)
- **文件位置**: `./accounts.json`
- **格式**: 完整的结构化账号信息

### 2. 环境变量文件 (兼容性)
- **当前目录**: `./.env`
- **warp2xyz子目录**: `./warp2xyz/.env`

### JSON 文件格式
```json
[
  {
    "email": "anonymous_qzi6ss@warp.dev",
    "plan": "Free",
    "used_limit": 0,
    "total_limit": 150,
    "end_time": "2025-10-30T03:08:11.292146Z",
    "local_id": "ekcfjhXVHLYi0lkn3a66YQ68Dp23",
    "id_token": "eyJhbGciOiJSUzI1NiIsImtpZCI6...",
    "refresh_token": "AMf-vByv-3_oxy940undaqpgVUGxhHKIT...",
    "expiration_time": "2025-09-29T12:08:10.522857+08:00",
    "experiment_id": "962c2633-d88a-4337-a93d-a54e431dccf4",
    "user_id": "ekcfjhXVHLYi0lkn3a66YQ68Dp23",
    "created_at": "2025-09-29T13:15:00.000000Z",
    "source": "warp_auth_script"
  }
]
```

### .env 文件格式 (备用)
```bash
WARP_JWT="eyJhbGciOiJSUzI1NiIsImtpZCI6..."
WARP_REFRESH_TOKEN="AMf-vBwIL1q0secaj_pgmZKtAooTG9..."
```

## 🔍 账号管理功能

### 1. 查看所有保存的账号
```bash
python warp_auth.py --list-accounts
```

**输出示例:**
```
🎯 Found 2 saved accounts:
================================================================================
📝 Account 1:
   User ID: FpAb0rA460gf8u8uZCCQHOFWz5z1
   Source: ./.env
   Expires: 2025-09-29 14:48:10
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOT...
   Refresh: AMf-vBy3B6B3aIMCnWAD4_eUopo4rU...
----------------------------------------
📝 Account 2:
   User ID: iTuI3syO9HQbxSFxApkwiIZbeAy2
   Source: ./warp2xyz/.env
   Expires: 2025-09-28 23:46:23
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6IjA1NTc3MjZmYWIxMjMxZm...
   Refresh: AMf-vBwIL1q0secaj_pgmZKtAooTG9...
----------------------------------------
```

### 2. 导出账号数据

#### 导出为 JSON 格式
```bash
python warp_auth.py --export-accounts accounts.json
```

#### 导出为 CSV 格式
```bash
python warp_auth.py --export-accounts accounts.csv
```

#### 导出为 TXT 格式
```bash
python warp_auth.py --export-accounts accounts.txt
```

**JSON 输出示例:**
```json
[
  {
    "user_id": "FpAb0rA460gf8u8uZCCQHOFWz5z1",
    "jwt_token": "eyJhbGciOiJSUzI1NiIsImtpZCI6...",
    "refresh_token": "AMf-vBwIL1q0secaj_pgmZKtAooTG9...",
    "source_file": "./.env",
    "created_at": 1759124890,
    "expires_at": 1759128490
  }
]
```

## 📊 账号信息说明

每个账号包含以下信息：

| 字段 | 说明 |
|------|------|
| `user_id` | 唯一用户标识符 |
| `jwt_token` | JWT 访问令牌 |
| `refresh_token` | 刷新令牌 |
| `source_file` | 来源文件路径 |
| `created_at` | 创建时间戳 |
| `expires_at` | 过期时间戳 |

## 💾 备份建议

### 1. 定期备份
```bash
# 导出所有账号作为备份
python warp_auth.py --export-accounts backup_$(date +%Y%m%d).json
```

### 2. 分环境存储
```bash
# 开发环境账号
python warp_auth.py --new-account --proxy

# 生产环境账号（保存到不同文件）
export WARP_ENV_FILE=".env.production"
python warp_auth.py --new-account --proxy
```

### 3. 加密存储
对于敏感的账号信息，建议：
- 使用加密工具加密 `.env` 文件
- 将加密后的文件存储在安全位置
- 定期轮换访问令牌

## 🔧 账号文件管理

### 查看具体账号信息
```bash
# 查看 .env 文件内容
cat .env

# 查看特定账号的详细信息
python warp_auth.py --status
```

### 手动备份账号
```bash
# 复制 .env 文件
cp .env .env.backup.$(date +%Y%m%d)

# 或者使用导出功能
python warp_auth.py --export-accounts manual_backup_$(date +%Y%m%d_%H%M%S).json
```

### 清理过期账号
```bash
# 列出所有账号，检查过期时间
python warp_auth.py --list-accounts

# 手动删除过期的 .env 文件
rm .env.expired
```

## 🚨 安全注意事项

1. **不要分享 `.env` 文件** - 包含敏感的访问令牌
2. **定期更新令牌** - JWT 令牌会过期，需要定期刷新
3. **使用版本控制** - 在 `.gitignore` 中添加 `.env*`
4. **限制文件权限** - 设置适当的文件访问权限
5. **监控使用情况** - 定期检查账号的使用日志

## 📝 .gitignore 配置

确保在你的版本控制系统中忽略 `.env` 文件：

```
# 环境变量文件
.env
.env.*
!.env.example

# 账号备份文件
accounts_*.json
accounts_*.csv
backup_*.env
```

这样你的 Warp 匿名账号就会被安全地保存和管理了！