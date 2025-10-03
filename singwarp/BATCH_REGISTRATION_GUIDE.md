# Warp 批量匿名注册指南

## 🔍 问题解答

### Q: 为什么刷新token得到的是相同的token？

**A: 这是因为两种不同的操作机制：**

1. **`--refresh` (刷新Token)**:
   - 使用现有的 `refresh_token` 获取新的 `access_token`
   - 保持相同的用户身份和user_id
   - 只是延长了会话时间

2. **`--new-account` (新建账户)**:
   - 每次都创建全新的匿名用户
   - 获得不同的user_id和身份
   - 用于批量注册不同的账户

## 🚀 批量注册方法

### 方法1: 单个创建新账户
```bash
# 每次运行都会创建全新的匿名账户
python warp_auth.py --new-account
```

### 方法2: 批量创建多个账户（推荐使用代理）
```bash
# 批量创建5个账户，使用代理，间隔10秒
python warp_auth.py --batch 5 --batch-delay 10.0 --proxy --proxy-host 127.0.0.1 --proxy-port 10809

# 创建10个账户，使用SOCKS5代理，间隔15秒
python warp_auth.py --batch 10 --batch-delay 15.0 --proxy --proxy-type socks5 --proxy-host 127.0.0.1 --proxy-port 10808

# 网络较差时，增加间隔到20-30秒
python warp_auth.py --batch 5 --batch-delay 25.0 --proxy --proxy-host 127.0.0.1 --proxy-port 10809
```

### 方法3: 循环批量创建
```bash
# 创建循环脚本来批量注册
for i in {1..10}; do
    echo "Creating account $i..."
    python warp_auth.py --new-account
    echo "Waiting 15 seconds..."
    sleep 15
done
```

## 📊 输出示例

### 单个新账户创建
```
🆕 Creating new anonymous account...
================================================================================
🎯 New Anonymous Account Created:
JWT Token: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMiLCJ0eXAiOiJKV1QifQ...
Refresh Token: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCPumbZr2gYtBjNHpkFPKkOsROtwgep0Z71YZ2eIZsXWJdFKxgJ9GzmwKRvB9Rp21axIazg8ZILpq4EehSmr-5BSh6CHwGPOigaPoyKiMbJgM52v2hqVfZo44WE_mQ88bdvmd6PZN
ID Token: eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...

📦 Account Payload:
   iss: https://securetoken.google.com/astral-field-294621
   aud: astral-field-294621
   user_id: jU5Y7j9SlpNeawVskqWGbccPHwA3
   sub: jU5Y7j9SlpNeawVskqWGbccPHwA3
   iat: 1759119653
   exp: 1759123253
   firebase: {'identities': {}, 'sign_in_provider': 'custom'}
⏰ Account Expires: 2025-09-29 13:20:53
================================================================================
```

### 批量创建输出
```
🚀 Starting batch creation of 3 accounts...
⏱️  Delay between accounts: 5.0 seconds

📝 Creating account 1/1...
============================================================
✅ Account 1 created successfully!
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMi...
   Refresh: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCP...
   User ID: jU5Y7j9SlpNeawVskqWGbccPHwA3

⏳ Waiting 5.0 seconds before next account...

📝 Creating account 2/3...
============================================================
✅ Account 2 created successfully!
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMi...
   Refresh: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCP...
   User ID: aB3x8k9TlpNfbwVtkqXGdccQHwB4

⏳ Waiting 5.0 seconds before next account...

📝 Creating account 3/3...
============================================================
✅ Account 3 created successfully!
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMi...
   Refresh: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCP...
   User ID: cC4y9l9UmpOfcwXtlqYGdddRHwB5

🎯 Batch Creation Summary:
   ✅ Successful: 3
   ❌ Failed: 0
   📊 Success Rate: 100.0%

✅ Batch creation completed!

📋 Created 3 accounts:
📝 Account 1:
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMi...
   Refresh: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCP...
   User ID: jU5Y7j9SlpNeawVskqWGbccPHwA3

📝 Account 2:
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMi...
   Refresh: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCP...
   User ID: aB3x8k9TlpNfbwVtkqXGdccQHwB4

📝 Account 3:
   JWT: eyJhbGciOiJSUzI1NiIsImtpZCI6ImU4MWYwNTJhZWYwNDBhOTdjMzlkMjY1MzgxZGU2Y2I0MzRiYzM1ZjMi...
   Refresh: AMf-vBwudNF4S1slwJ9O2qQjBJwEKW5vmmbjH8OXGJ34LzQ9Sd1_CkCxFokUCPsWTt6CCP...
   User ID: cC4y9l9UmpOfcwXtlqYGdddRHwB5
```

## ⚠️ 注意事项

1. **强烈建议使用代理**: 批量注册时必须使用代理以避免429错误
   ```bash
   python warp_auth.py --batch 5 --batch-delay 10.0 --proxy --proxy-host 127.0.0.1 --proxy-port 10809
   ```

2. **代理测试**: 脚本会自动测试代理连接，确保代理可用

3. **429错误处理**: 如果仍然遇到429错误，请：
   - 增加延迟时间：`--batch-delay 20.0`
   - 检查代理是否正常工作
   - 尝试不同的代理类型或端口

4. **网络环境**: 根据网络环境调整参数：
   - 良好网络：间隔10-15秒
   - 一般网络：间隔15-25秒
   - 较差网络：间隔25-40秒

5. **Token保存**: 所有创建的账户信息都会保存到 `.env` 文件中

6. **代理类型支持**:
   - HTTP/HTTPS代理：`--proxy-type http`
   - SOCKS5代理：`--proxy-type socks5`

## 🎯 验证不同账户

每个新创建的账户都会有不同的 `user_id`，这证明了它们确实是不同的匿名账户：

- 账户1: `user_id: jU5Y7j9SlpNeawVskqWGbccPHwA3`
- 账户2: `user_id: aB3x8k9TlpNfbwVtkqXGdccQHwB4`
- 账户3: `user_id: cC4y9l9UmpOfcwXtlqYGdddRHwB5`

这样就实现了真正的批量匿名注册！