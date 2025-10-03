#!/usr/bin/env python3
"""
Warp Authentication GUI with V2RAY Proxy Support

A graphical interface for Warp anonymous authentication with proxy configuration.
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import asyncio
import os
import sys
import threading
import json
from datetime import datetime
from typing import Optional, Dict, Any
import requests
from urllib.parse import urlparse

# Import the authentication logic
from warp_auth import WarpAuthenticator

class WarpAuthGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Warp Authentication Tool with V2RAY Proxy")
        self.root.geometry("800x700")
        self.root.resizable(True, True)

        # Set style
        self.style = ttk.Style()
        self.style.theme_use('clam')

        # Initialize variables
        self.proxy_host = tk.StringVar(value="127.0.0.1")
        self.proxy_port = tk.StringVar(value="10809")
        self.proxy_type = tk.StringVar(value="http")
        self.use_proxy = tk.BooleanVar(value=True)
        self.auth_status = tk.StringVar(value="未认证")
        self.jwt_token = tk.StringVar(value="")
        self.retry_attempts = tk.IntVar(value=2)
        self.retry_delay = tk.DoubleVar(value=1.0)
        self.max_delay = tk.DoubleVar(value=60.0)

        # Authentication instance
        self.auth_instance = None
        self.is_authenticating = False

        # Create GUI
        self.create_widgets()
        self.load_settings()
        self.update_status_display()

    def create_widgets(self):
        """Create all GUI widgets"""
        # Main container
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))

        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)

        # Create notebook for tabs
        notebook = ttk.Notebook(main_frame)
        notebook.grid(row=0, column=0, columnspan=2, sticky=(tk.W, tk.E, tk.N, tk.S), pady=(0, 10))
        main_frame.rowconfigure(0, weight=1)

        # Tab 1: Proxy Configuration
        proxy_frame = ttk.Frame(notebook, padding="10")
        notebook.add(proxy_frame, text="代理配置")
        self.create_proxy_tab(proxy_frame)

        # Tab 2: Authentication
        auth_frame = ttk.Frame(notebook, padding="10")
        notebook.add(auth_frame, text="认证")
        self.create_auth_tab(auth_frame)

        # Tab 3: Settings
        settings_frame = ttk.Frame(notebook, padding="10")
        notebook.add(settings_frame, text="设置")
        self.create_settings_tab(settings_frame)

        # Status bar
        self.create_status_bar(main_frame)

    def create_proxy_tab(self, parent):
        """Create proxy configuration tab"""
        # Proxy Settings Group
        proxy_group = ttk.LabelFrame(parent, text="V2RAY 代理设置", padding="10")
        proxy_group.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        parent.columnconfigure(0, weight=1)

        # Use proxy checkbox
        use_proxy_cb = ttk.Checkbutton(
            proxy_group,
            text="启用代理",
            variable=self.use_proxy,
            command=self.toggle_proxy_settings
        )
        use_proxy_cb.grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 10))

        # Proxy type
        ttk.Label(proxy_group, text="代理类型:").grid(row=1, column=0, sticky=tk.W, pady=2)
        proxy_type_combo = ttk.Combobox(
            proxy_group,
            textvariable=self.proxy_type,
            values=["http", "https", "socks5", "socks5h"],
            state="readonly",
            width=15
        )
        proxy_type_combo.grid(row=1, column=1, sticky=(tk.W, tk.E), pady=2)

        # Proxy host
        ttk.Label(proxy_group, text="代理地址:").grid(row=2, column=0, sticky=tk.W, pady=2)
        proxy_host_entry = ttk.Entry(proxy_group, textvariable=self.proxy_host, width=20)
        proxy_host_entry.grid(row=2, column=1, sticky=(tk.W, tk.E), pady=2)

        # Proxy port
        ttk.Label(proxy_group, text="代理端口:").grid(row=3, column=0, sticky=tk.W, pady=2)
        proxy_port_entry = ttk.Entry(proxy_group, textvariable=self.proxy_port, width=20)
        proxy_port_entry.grid(row=3, column=1, sticky=(tk.W, tk.E), pady=2)

        # Proxy display
        ttk.Label(proxy_group, text="当前代理:").grid(row=4, column=0, sticky=tk.W, pady=(10, 2))
        self.proxy_display = ttk.Label(proxy_group, text="", foreground="blue")
        self.proxy_display.grid(row=4, column=1, sticky=tk.W, pady=(10, 2))

        # Configure column weights
        proxy_group.columnconfigure(1, weight=1)

        # Test proxy button
        test_proxy_btn = ttk.Button(
            proxy_group,
            text="测试代理连接",
            command=self.test_proxy_connection
        )
        test_proxy_btn.grid(row=5, column=0, columnspan=2, pady=(10, 0))

        # System proxy detection
        system_proxy_frame = ttk.LabelFrame(parent, text="系统代理检测", padding="10")
        system_proxy_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        system_proxy_frame.columnconfigure(1, weight=1)

        ttk.Button(
            system_proxy_frame,
            text="检测系统代理",
            command=self.detect_system_proxy
        ).grid(row=0, column=0, pady=(0, 10))

        self.system_proxy_label = ttk.Label(system_proxy_frame, text="未检测")
        self.system_proxy_label.grid(row=0, column=1, sticky=tk.W, pady=(0, 10))

    def create_auth_tab(self, parent):
        """Create authentication tab"""
        # Authentication Status
        status_frame = ttk.LabelFrame(parent, text="认证状态", padding="10")
        status_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        parent.columnconfigure(0, weight=1)

        # Status display
        self.status_canvas = tk.Canvas(status_frame, height=30, bg="white", highlightthickness=1)
        self.status_canvas.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        status_frame.columnconfigure(0, weight=1)

        # Token info
        token_frame = ttk.Frame(status_frame)
        token_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=(0, 10))

        ttk.Label(token_frame, text="JWT Token:").grid(row=0, column=0, sticky=tk.W)
        self.token_display = ttk.Label(token_frame, text="未获取", foreground="gray")
        self.token_display.grid(row=0, column=1, sticky=(tk.W, tk.E), padx=(10, 0))
        token_frame.columnconfigure(1, weight=1)

        # Authentication buttons
        button_frame = ttk.Frame(status_frame)
        button_frame.grid(row=2, column=0, sticky=(tk.W, tk.E))

        self.auth_btn = ttk.Button(
            button_frame,
            text="开始认证",
            command=self.start_authentication
        )
        self.auth_btn.grid(row=0, column=0, padx=(0, 10))

        ttk.Button(
            button_frame,
            text="刷新Token",
            command=self.refresh_token
        ).grid(row=0, column=1, padx=(0, 10))

        ttk.Button(
            button_frame,
            text="验证Token",
            command=self.validate_token
        ).grid(row=0, column=2)

        # Log display
        log_frame = ttk.LabelFrame(parent, text="认证日志", padding="10")
        log_frame.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), pady=(0, 10))
        parent.rowconfigure(1, weight=1)

        self.log_text = scrolledtext.ScrolledText(log_frame, height=15, width=70)
        self.log_text.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)

        # Clear log button
        ttk.Button(
            log_frame,
            text="清空日志",
            command=self.clear_log
        ).grid(row=1, column=0, pady=(10, 0))

    def create_settings_tab(self, parent):
        """Create settings tab"""
        # Retry Settings
        retry_frame = ttk.LabelFrame(parent, text="重试设置", padding="10")
        retry_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        parent.columnconfigure(0, weight=1)
        retry_frame.columnconfigure(1, weight=1)

        # Max attempts
        ttk.Label(retry_frame, text="最大尝试次数:").grid(row=0, column=0, sticky=tk.W, pady=2)
        attempts_spinbox = ttk.Spinbox(
            retry_frame,
            from_=1,
            to=10,
            textvariable=self.retry_attempts,
            width=10
        )
        attempts_spinbox.grid(row=0, column=1, sticky=tk.W, pady=2)

        # Base delay
        ttk.Label(retry_frame, text="基础延迟(秒):").grid(row=1, column=0, sticky=tk.W, pady=2)
        delay_spinbox = ttk.Spinbox(
            retry_frame,
            from_=0.5,
            to=10.0,
            increment=0.5,
            textvariable=self.retry_delay,
            width=10
        )
        delay_spinbox.grid(row=1, column=1, sticky=tk.W, pady=2)

        # Max delay
        ttk.Label(retry_frame, text="最大延迟(秒):").grid(row=2, column=0, sticky=tk.W, pady=2)
        max_delay_spinbox = ttk.Spinbox(
            retry_frame,
            from_=10,
            to=300,
            increment=10,
            textvariable=self.max_delay,
            width=10
        )
        max_delay_spinbox.grid(row=2, column=1, sticky=tk.W, pady=2)

        # Save/Load settings
        settings_btn_frame = ttk.Frame(retry_frame)
        settings_btn_frame.grid(row=3, column=0, columnspan=2, pady=(10, 0))

        ttk.Button(
            settings_btn_frame,
            text="保存设置",
            command=self.save_settings
        ).grid(row=0, column=0, padx=(0, 10))

        ttk.Button(
            settings_btn_frame,
            text="恢复默认",
            command=self.reset_settings
        ).grid(row=0, column=1)

        # Environment Info
        env_frame = ttk.LabelFrame(parent, text="环境信息", padding="10")
        env_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        env_frame.columnconfigure(1, weight=1)

        # Environment variables
        ttk.Label(env_frame, text="WARP_REFRESH_URL:").grid(row=0, column=0, sticky=tk.W, pady=2)
        self.refresh_url_label = ttk.Label(env_frame, text="", foreground="blue")
        self.refresh_url_label.grid(row=0, column=1, sticky=tk.W, pady=2)

        ttk.Label(env_frame, text="WARP_JWT:").grid(row=1, column=0, sticky=tk.W, pady=2)
        self.jwt_env_label = ttk.Label(env_frame, text="", foreground="blue")
        self.jwt_env_label.grid(row=1, column=1, sticky=tk.W, pady=2)

        ttk.Button(
            env_frame,
            text="刷新环境信息",
            command=self.update_env_info
        ).grid(row=2, column=0, columnspan=2, pady=(10, 0))

    def create_status_bar(self, parent):
        """Create status bar"""
        status_frame = ttk.Frame(parent)
        status_frame.grid(row=1, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=(10, 0))
        parent.columnconfigure(0, weight=1)

        ttk.Label(status_frame, text="状态:").grid(row=0, column=0, sticky=tk.W)
        self.status_label = ttk.Label(status_frame, text="就绪", foreground="green")
        self.status_label.grid(row=0, column=1, sticky=tk.W, padx=(10, 0))

    def toggle_proxy_settings(self):
        """Enable/disable proxy settings"""
        enabled = self.use_proxy.get()
        # You can add logic here to enable/disable proxy entry fields
        self.update_proxy_display()

    def update_proxy_display(self):
        """Update proxy display"""
        if self.use_proxy.get():
            proxy_url = f"{self.proxy_type.get()}://{self.proxy_host.get()}:{self.proxy_port.get()}"
            self.proxy_display.config(text=proxy_url)
        else:
            self.proxy_display.config(text="未启用代理")

    def detect_system_proxy(self):
        """Detect system proxy settings"""
        try:
            # Check environment variables
            http_proxy = os.getenv('HTTP_PROXY') or os.getenv('http_proxy')
            https_proxy = os.getenv('HTTPS_PROXY') or os.getenv('https_proxy')

            proxy_info = []
            if http_proxy:
                proxy_info.append(f"HTTP: {http_proxy}")
            if https_proxy:
                proxy_info.append(f"HTTPS: {https_proxy}")

            if proxy_info:
                proxy_text = " | ".join(proxy_info)
                self.system_proxy_label.config(text=proxy_text, foreground="green")

                # Auto-fill proxy settings
                if http_proxy:
                    parsed = urlparse(http_proxy)
                    self.proxy_type.set(parsed.scheme)
                    self.proxy_host.set(parsed.hostname or "127.0.0.1")
                    self.proxy_port.set(str(parsed.port or "10809"))
                    self.use_proxy.set(True)

                self.update_proxy_display()
                self.log_message(f"检测到系统代理: {proxy_text}")
            else:
                self.system_proxy_label.config(text="未检测到系统代理", foreground="orange")
                self.log_message("未检测到系统代理设置")

        except Exception as e:
            self.system_proxy_label.config(text=f"检测失败: {str(e)}", foreground="red")
            self.log_message(f"检测系统代理失败: {str(e)}")

    def test_proxy_connection(self):
        """Test proxy connection"""
        if not self.use_proxy.get():
            messagebox.showwarning("警告", "请先启用代理")
            return

        proxy_url = f"{self.proxy_type.get()}://{self.proxy_host.get()}:{self.proxy_port.get()}"

        def test_in_thread():
            try:
                proxies = {
                    'http': proxy_url,
                    'https': proxy_url
                }

                # Test with a simple request
                response = requests.get(
                    'http://httpbin.org/ip',
                    proxies=proxies,
                    timeout=10
                )

                if response.status_code == 200:
                    self.root.after(0, lambda: self.log_message(f"代理连接成功: {response.json()}"))
                    self.root.after(0, lambda: messagebox.showinfo("成功", "代理连接测试成功"))
                else:
                    raise Exception(f"HTTP {response.status_code}")

            except Exception as e:
                error_msg = f"代理连接失败: {str(e)}"
                self.root.after(0, lambda: self.log_message(error_msg))
                self.root.after(0, lambda: messagebox.showerror("失败", error_msg))

        threading.Thread(target=test_in_thread, daemon=True).start()

    def start_authentication(self):
        """Start authentication process"""
        if self.is_authenticating:
            messagebox.showwarning("警告", "认证正在进行中")
            return

        self.is_authenticating = True
        self.auth_btn.config(state='disabled', text="认证中...")
        self.status_label.config(text="认证中...", foreground="orange")

        def auth_thread():
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            try:
                result = loop.run_until_complete(self.authenticate_with_proxy())
                self.root.after(0, lambda: self.on_auth_complete(result))
            except Exception as e:
                error_msg = f"认证失败: {str(e)}"
                self.root.after(0, lambda: self.log_message(error_msg))
                self.root.after(0, lambda: self.on_auth_error(error_msg))
            finally:
                loop.close()
                self.is_authenticating = False
                self.root.after(0, lambda: self.auth_btn.config(state='normal', text="开始认证"))

        threading.Thread(target=auth_thread, daemon=True).start()

    async def authenticate_with_proxy(self):
        """Authenticate with proxy configuration"""
        # Configure proxy for aiohttp
        proxy = None
        if self.use_proxy.get():
            proxy = f"{self.proxy_type.get()}://{self.proxy_host.get()}:{self.proxy_port.get()}"
            self.log_message(f"使用代理: {proxy}")

            # Set environment variables for aiohttp
            os.environ['HTTP_PROXY'] = proxy
            os.environ['HTTPS_PROXY'] = proxy

        # Create authenticator with retry settings
        async with WarpAuthenticator(
            max_retries=3,
            base_delay=self.retry_delay.get(),
            max_delay=self.max_delay.get()
        ) as auth:

            self.auth_instance = auth
            self.log_message("开始认证流程...")

            # Get JWT with fallback
            jwt = await auth.get_valid_jwt_with_fallback(max_attempts=self.retry_attempts.get())

            if jwt:
                self.jwt_token.set(jwt)
                return jwt
            else:
                raise Exception("认证失败，无法获取JWT")

    def on_auth_complete(self, jwt):
        """Handle successful authentication"""
        self.auth_status.set("已认证")
        self.status_label.config(text="认证成功", foreground="green")
        self.token_display.config(text=f"{jwt[:50]}...", foreground="green")
        self.update_status_display()
        self.log_message("✅ 认证成功!")

        # Show complete token info in log
        self.log_message("=" * 80)
        self.log_message("📋 完整Token信息:")
        self.log_message(f"JWT Token: {jwt}")

        # Show refresh token if available
        try:
            import os
            from dotenv import load_dotenv
            load_dotenv(override=True)
            refresh_token = os.getenv('WARP_REFRESH_TOKEN')
            if refresh_token:
                self.log_message(f"Refresh Token: {refresh_token}")
        except Exception as e:
            self.log_message(f"⚠️ 获取Refresh Token失败: {e}")

        # Show token payload
        try:
            import datetime
            payload = self.auth_instance._decode_jwt(jwt)
            if payload:
                self.log_message("📦 JWT Payload:")
                for key, value in payload.items():
                    self.log_message(f"   {key}: {value}")

                if 'exp' in payload:
                    expiry = datetime.datetime.fromtimestamp(payload['exp'])
                    self.log_message(f"⏰ Token过期时间: {expiry}")

        except Exception as e:
            self.log_message(f"⚠️ 解析JWT失败: {e}")

        self.log_message("=" * 80)

    def on_auth_error(self, error_msg):
        """Handle authentication error"""
        self.auth_status.set("认证失败")
        self.status_label.config(text="认证失败", foreground="red")
        self.log_message(f"❌ {error_msg}")

    def refresh_token(self):
        """Refresh existing token"""
        if not self.auth_instance:
            messagebox.showwarning("警告", "请先进行认证")
            return

        def refresh_thread():
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            try:
                result = loop.run_until_complete(self.auth_instance.refresh_jwt_token())
                if result:
                    def show_refresh_success():
                        self.log_message("✅ Token刷新成功")
                        self.log_message("=" * 80)
                        self.log_message("🔄 刷新后的Token信息:")
                        self.log_message(f"新JWT Token: {result}")

                        # Show refresh token
                        try:
                            import os
                            from dotenv import load_dotenv
                            load_dotenv(override=True)
                            refresh_token = os.getenv('WARP_REFRESH_TOKEN')
                            if refresh_token:
                                self.log_message(f"Refresh Token: {refresh_token}")
                        except Exception as e:
                            self.log_message(f"⚠️ 获取Refresh Token失败: {e}")

                        # Show token payload
                        try:
                            import datetime
                            payload = self.auth_instance._decode_jwt(result)
                            if payload:
                                self.log_message("📦 新JWT Payload:")
                                for key, value in payload.items():
                                    self.log_message(f"   {key}: {value}")

                                if 'exp' in payload:
                                    expiry = datetime.datetime.fromtimestamp(payload['exp'])
                                    self.log_message(f"⏰ 新Token过期时间: {expiry}")
                        except Exception as e:
                            self.log_message(f"⚠️ 解析新JWT失败: {e}")

                        self.log_message("=" * 80)
                        messagebox.showinfo("成功", "Token刷新成功")

                    self.root.after(0, show_refresh_success)
                else:
                    self.root.after(0, lambda: self.log_message("❌ Token刷新失败"))
                    self.root.after(0, lambda: messagebox.showerror("失败", "Token刷新失败"))
            except Exception as e:
                error_msg = f"刷新失败: {str(e)}"
                self.root.after(0, lambda: self.log_message(error_msg))
                self.root.after(0, lambda: messagebox.showerror("失败", error_msg))
            finally:
                loop.close()

        threading.Thread(target=refresh_thread, daemon=True).start()

    def validate_token(self):
        """Validate current token"""
        jwt = self.jwt_token.get()
        if not jwt:
            messagebox.showwarning("警告", "没有可验证的Token")
            return

        if not self.auth_instance:
            messagebox.showwarning("警告", "认证实例不可用")
            return

        is_expired = self.auth_instance.is_token_expired(jwt)
        if is_expired:
            messagebox.showinfo("Token状态", "Token已过期")
        else:
            messagebox.showinfo("Token状态", "Token有效")

    def update_status_display(self):
        """Update status display on canvas"""
        self.status_canvas.delete("all")

        status = self.auth_status.get()
        if status == "已认证":
            color = "green"
            text = "● 已认证"
        elif status == "认证失败":
            color = "red"
            text = "● 认证失败"
        else:
            color = "orange"
            text = "● 未认证"

        self.status_canvas.create_text(10, 15, text=text, anchor="w", font=("Arial", 12, "bold"), fill=color)

    def update_env_info(self):
        """Update environment information"""
        refresh_url = os.getenv('WARP_REFRESH_URL', '未设置')
        jwt_env = os.getenv('WARP_JWT', '未设置')

        self.refresh_url_label.config(text=refresh_url[:50] + "..." if len(refresh_url) > 50 else refresh_url)
        self.jwt_env_label.config(text=jwt_env[:30] + "..." if len(jwt_env) > 30 and jwt_env != '未设置' else jwt_env)

    def save_settings(self):
        """Save current settings"""
        settings = {
            'proxy_host': self.proxy_host.get(),
            'proxy_port': self.proxy_port.get(),
            'proxy_type': self.proxy_type.get(),
            'use_proxy': self.use_proxy.get(),
            'retry_attempts': self.retry_attempts.get(),
            'retry_delay': self.retry_delay.get(),
            'max_delay': self.max_delay.get()
        }

        try:
            with open('gui_settings.json', 'w', encoding='utf-8') as f:
                json.dump(settings, f, indent=2, ensure_ascii=False)
            self.log_message("✅ 设置已保存")
            messagebox.showinfo("成功", "设置已保存")
        except Exception as e:
            self.log_message(f"❌ 保存设置失败: {str(e)}")
            messagebox.showerror("失败", f"保存设置失败: {str(e)}")

    def load_settings(self):
        """Load saved settings"""
        try:
            if os.path.exists('gui_settings.json'):
                with open('gui_settings.json', 'r', encoding='utf-8') as f:
                    settings = json.load(f)

                self.proxy_host.set(settings.get('proxy_host', '127.0.0.1'))
                self.proxy_port.set(settings.get('proxy_port', '10809'))
                self.proxy_type.set(settings.get('proxy_type', 'http'))
                self.use_proxy.set(settings.get('use_proxy', True))
                self.retry_attempts.set(settings.get('retry_attempts', 2))
                self.retry_delay.set(settings.get('retry_delay', 1.0))
                self.max_delay.set(settings.get('max_delay', 60.0))

                self.log_message("✅ 设置已加载")
        except Exception as e:
            self.log_message(f"❌ 加载设置失败: {str(e)}")

    def reset_settings(self):
        """Reset to default settings"""
        if messagebox.askyesno("确认", "确定要恢复默认设置吗？"):
            self.proxy_host.set('127.0.0.1')
            self.proxy_port.set('10809')
            self.proxy_type.set('http')
            self.use_proxy.set(True)
            self.retry_attempts.set(2)
            self.retry_delay.set(1.0)
            self.max_delay.set(60.0)
            self.update_proxy_display()
            self.log_message("✅ 设置已重置为默认值")

    def clear_log(self):
        """Clear log text"""
        self.log_text.delete(1.0, tk.END)

    def log_message(self, message):
        """Add message to log"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        log_entry = f"[{timestamp}] {message}\n"
        self.log_text.insert(tk.END, log_entry)
        self.log_text.see(tk.END)

def main():
    """Main function"""
    root = tk.Tk()
    app = WarpAuthGUI(root)

    # Initialize
    app.update_proxy_display()
    app.update_env_info()
    app.log_message("🚀 Warp Authentication GUI 已启动")
    app.log_message("💡 提示: 可以使用系统代理检测功能自动配置代理")

    root.mainloop()

if __name__ == "__main__":
    main()