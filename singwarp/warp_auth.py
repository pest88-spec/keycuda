#!/usr/bin/env python3
"""
Warp Anonymous Authentication Script

This script implements the three-step authentication flow for obtaining
anonymous access tokens for the Warp API.

Flow:
1. Create anonymous user via GraphQL → get idToken
2. Exchange idToken for refresh_token via Google Identity Toolkit
3. Exchange refresh_token for access_token via Warp proxy endpoint

Usage:
    python warp_auth.py                    # Get new token
    python warp_auth.py --refresh          # Refresh existing token
    python warp_auth.py --validate         # Validate current token
"""

import os
import json
import base64
import asyncio
import aiohttp
import argparse
import random
import time
from datetime import datetime, timedelta
from typing import Optional, Dict, Any, List
import sys

# Configuration - matching successful implementation
CLIENT_VERSION = "v0.2025.08.06.08.12.stable_02"
OS_CATEGORY = "Windows"
OS_NAME = "Windows"
OS_VERSION = "11 (26100)"

# Default refresh URL
DEFAULT_REFRESH_URL = "https://app.warp.dev/proxy/token?key=AIzaSyBdy3O3S9hrdayLJxJ7mriBR4qgUaUygAs"

# Fallback refresh token (base64 encoded)
FALLBACK_REFRESH_TOKEN_B64 = "Z3JhbnRfdHlwZT1yZWZyZXNoX3Rva2VuJnJlZnJlc2hfdG9rZW49QU1mLXZCeFNSbWRodmVHR0JZTTY5cDA1a0RoSW4xaTd3c2NBTEVtQzlmWURScEh6akVSOWRMN2trLWtIUFl3dlk5Uk9rbXk1MHFHVGNJaUpaNEFtODZoUFhrcFZQTDkwSEptQWY1Zlo3UGVqeXBkYmNLNHdzbzhLZjNheGlTV3RJUk9oT2NuOU56R2FTdmw3V3FSTU5PcEhHZ0JyWW40SThrclc1N1I4X3dzOHU3WGNTdzh1MERpTDlIcnBNbTBMdHdzQ2g4MWtfNmJiMkNXT0ViMWxLeDNIV1NCVGVQRldzUQ=="

# GraphQL mutation for creating anonymous user - matching successful implementation
CREATE_ANONYMOUS_USER_MUTATION = """
mutation CreateAnonymousUser($input: CreateAnonymousUserInput!, $requestContext: RequestContext!) {
  createAnonymousUser(input: $input, requestContext: $requestContext) {
    __typename
    ... on CreateAnonymousUserOutput {
      expiresAt
      anonymousUserType
      firebaseUid
      idToken
      isInviteValid
      responseContext { serverVersion }
    }
    ... on UserFacingError {
      error { __typename message }
      responseContext { serverVersion }
    }
  }
}
"""

class WarpAuthenticator:
    def __init__(self, max_retries=3, base_delay=1.0, max_delay=60, use_proxy=False, proxy_host="127.0.0.1", proxy_port="10809", proxy_type="http"):
        self.session = None
        self.env_file = self._get_env_file_path()
        self.max_retries = max_retries
        self.base_delay = base_delay
        self.max_delay = max_delay
        self.use_proxy = use_proxy
        self.proxy_host = proxy_host
        self.proxy_port = proxy_port
        self.proxy_type = proxy_type

    def _get_env_file_path(self) -> str:
        """Determine where to store the .env file"""
        if os.path.exists("/tmp/.env"):
            return "/tmp/.env"
        return ".env"

    async def __aenter__(self):
        # Create session with proxy if enabled
        if self.use_proxy:
            proxy_url = f"{self.proxy_type}://{self.proxy_host}:{self.proxy_port}"
            connector = aiohttp.TCPConnector()
            self.session = aiohttp.ClientSession(
                connector=connector,
                trust_env=True  # Respect HTTP_PROXY, HTTPS_PROXY environment variables
            )
            # Set proxy environment variables for aiohttp
            os.environ['HTTP_PROXY'] = proxy_url
            os.environ['HTTPS_PROXY'] = proxy_url
            print(f"🔗 Using proxy: {proxy_url}")
        else:
            self.session = aiohttp.ClientSession()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            await self.session.close()
        # Clean up proxy environment variables
        if self.use_proxy:
            os.environ.pop('HTTP_PROXY', None)
            os.environ.pop('HTTPS_PROXY', None)

    def _get_headers(self, content_type: str = "application/json") -> Dict[str, str]:
        """Get standard headers for Warp API requests"""
        headers = {
            "accept-encoding": "gzip, br",
            "x-warp-client-version": CLIENT_VERSION,
            "x-warp-os-category": OS_CATEGORY,
            "x-warp-os-name": OS_NAME,
            "x-warp-os-version": OS_VERSION,
        }

        if content_type:
            headers["content-type"] = content_type

        return headers

    def _extract_google_api_key_from_refresh_url(self, refresh_url: str) -> str:
        """Extract API key from refresh URL"""
        try:
            from urllib.parse import urlparse, parse_qs
            parsed = urlparse(refresh_url)
            query_params = parse_qs(parsed.query)
            return query_params.get("key", [""])[0]
        except:
            return ""

    def _decode_jwt(self, token: str) -> Dict[str, Any]:
        """Decode JWT token without validation"""
        try:
            parts = token.split('.')
            if len(parts) != 3:
                return {}

            # Add padding if needed
            payload_b64 = parts[1]
            padding = len(payload_b64) % 4
            if padding:
                payload_b64 += '=' * (4 - padding)

            payload = base64.b64decode(payload_b64).decode('utf-8')
            return json.loads(payload)
        except:
            return {}

    def is_token_expired(self, token: str, buffer_minutes: int = 5) -> bool:
        """Check if JWT token is expired or about to expire"""
        if not token:
            return True

        payload = self._decode_jwt(token)
        if not payload or 'exp' not in payload:
            return True

        expiry_time = datetime.fromtimestamp(payload['exp'])
        buffer_time = timedelta(minutes=buffer_minutes)

        return datetime.now() + buffer_time >= expiry_time

    async def _retry_request(self, request_func, *args, **kwargs):
        """Retry logic with exponential backoff"""
        for attempt in range(self.max_retries):
            try:
                return await request_func(*args, **kwargs)
            except Exception as e:
                if attempt == self.max_retries - 1:
                    raise e

                # Calculate delay with exponential backoff and jitter
                delay = min(self.base_delay * (2 ** attempt), self.max_delay)
                jitter = random.uniform(0.5, 1.5)
                actual_delay = delay * jitter

                print(f"⚠️  Attempt {attempt + 1} failed: {e}")
                print(f"⏳ Retrying in {actual_delay:.1f} seconds...")

                await asyncio.sleep(actual_delay)

    async def load_env_tokens(self) -> Dict[str, str]:
        """Load tokens from .env file"""
        tokens = {}

        if os.path.exists(self.env_file):
            with open(self.env_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line and '=' in line and not line.startswith('#'):
                        key, value = line.split('=', 1)
                        tokens[key.strip()] = value.strip().strip('"\'')

        return tokens

    async def save_env_tokens(self, tokens: Dict[str, str]):
        """Save tokens to .env file"""
        # Read existing content
        existing_content = ""
        if os.path.exists(self.env_file):
            with open(self.env_file, 'r') as f:
                existing_content = f.read()

        # Update tokens
        lines = existing_content.split('\n')
        updated_lines = []

        # Track which tokens we've updated
        updated_tokens = set()

        for line in lines:
            line = line.strip()
            if line and '=' in line and not line.startswith('#'):
                key = line.split('=', 1)[0].strip()
                if key in tokens:
                    updated_lines.append(f'{key}="{tokens[key]}"')
                    updated_tokens.add(key)
                else:
                    updated_lines.append(line)
            else:
                updated_lines.append(line)

        # Add new tokens
        for key, value in tokens.items():
            if key not in updated_tokens:
                updated_lines.append(f'{key}="{value}"')

        # Write back to file
        with open(self.env_file, 'w') as f:
            f.write('\n'.join(updated_lines) + '\n')

        # Also save to structured JSON file
        await self.save_account_to_json(tokens)

    async def save_account_to_json(self, tokens: Dict[str, str]) -> bool:
        """Save account information to structured JSON file"""
        try:
            jwt = tokens.get("WARP_JWT", "")
            refresh_token = tokens.get("WARP_REFRESH_TOKEN", "")

            if not jwt or not refresh_token:
                return False

            # Parse JWT to get account information
            payload = self._decode_jwt(jwt)
            if not payload:
                return False

            # Generate email and other account info
            user_id = payload.get("user_id", "")
            local_id = self._generate_local_id()

            # Create email in format: anonymous_random@warp.dev
            import random
            import string
            random_suffix = ''.join(random.choices(string.ascii_lowercase + string.digits, k=6))
            email = f"anonymous_{random_suffix}@warp.dev"

            # Set expiration time (30 days from now by default)
            from datetime import datetime, timedelta
            end_time = datetime.now() + timedelta(days=30)

            # Create experiment ID
            experiment_id = f"{user_id[:8]}-{random_suffix}"

            # Create structured account data
            account_data = {
                "email": email,
                "plan": "Free",
                "used_limit": 0,
                "total_limit": 150,
                "end_time": end_time.isoformat() + "Z",
                "local_id": local_id,
                "id_token": jwt,
                "refresh_token": refresh_token,
                "expiration_time": datetime.fromtimestamp(payload['exp']).isoformat() + "+08:00",
                "experiment_id": experiment_id,
                "user_id": user_id,
                "created_at": datetime.now().isoformat() + "Z",
                "source": "warp_auth_script"
            }

            # Save to accounts.json file
            accounts_file = "accounts.json"
            accounts = []

            # Load existing accounts if file exists
            if os.path.exists(accounts_file):
                try:
                    with open(accounts_file, 'r', encoding='utf-8') as f:
                        accounts = json.load(f)
                except:
                    accounts = []

            # Add new account
            accounts.append(account_data)

            # Save back to file
            with open(accounts_file, 'w', encoding='utf-8') as f:
                json.dump(accounts, f, indent=2, ensure_ascii=False)

            print(f"✅ Account saved to {accounts_file}")
            print(f"   Email: {email}")
            print(f"   User ID: {user_id}")
            print(f"   Local ID: {local_id}")

            return True

        except Exception as e:
            print(f"❌ Error saving account to JSON: {e}")
            return False

    def _generate_local_id(self) -> str:
        """Generate a random local ID"""
        import random
        import string
        return ''.join(random.choices(string.ascii_lowercase + string.digits, k=26))

    async def create_anonymous_user(self) -> str:
        """Step 1: Create anonymous user and return idToken"""
        return await self._retry_request(self._create_anonymous_user_request)

    async def _create_anonymous_user_request(self) -> str:
        """Actual implementation of creating anonymous user - matching successful implementation"""
        url = "https://app.warp.dev/graphql/v2?op=CreateAnonymousUser"

        # Complete payload with requestContext
        payload = {
            "query": CREATE_ANONYMOUS_USER_MUTATION,
            "variables": {
                "input": {
                    "anonymousUserType": "NATIVE_CLIENT_ANONYMOUS_USER_FEATURE_GATED",
                    "expirationType": "NO_EXPIRATION",
                    "referralCode": None,
                },
                "requestContext": {
                    "clientContext": {"version": CLIENT_VERSION},
                    "osContext": {
                        "category": OS_CATEGORY,
                        "linuxKernelVersion": None,
                        "name": OS_NAME,
                        "version": OS_VERSION,
                    },
                },
            },
            "operationName": "CreateAnonymousUser"
        }

        headers = self._get_headers()

        print("🔑 Creating anonymous user...")

        async with self.session.post(url, json=payload, headers=headers) as response:
            # Handle rate limiting specifically
            if response.status == 429:
                retry_after = response.headers.get('Retry-After')
                error_msg = f"Rate limited (429)"
                if retry_after:
                    error_msg += f", Retry-After: {retry_after}"
                raise Exception(error_msg)
            elif response.status != 200:
                error_text = await response.text()
                raise Exception(f"Failed to create anonymous user: {response.status} - {error_text}")

            data = await response.json()

            # Check for GraphQL errors
            if 'errors' in data:
                raise Exception(f"GraphQL error: {data['errors']}")

            # Extract idToken from the response structure
            try:
                id_token = data["data"]["createAnonymousUser"].get("idToken")
            except (KeyError, TypeError):
                id_token = None

            if not id_token:
                raise Exception(f"CreateAnonymousUser did not return idToken: {data}")

            print("✅ Anonymous user created successfully")
            return id_token

    async def exchange_for_refresh_token(self, id_token: str, refresh_url: str) -> str:
        """Step 2: Exchange idToken for refresh_token"""
        return await self._retry_request(self._exchange_for_refresh_token_request, id_token, refresh_url)

    async def _exchange_for_refresh_token_request(self, id_token: str, refresh_url: str) -> str:
        """Actual implementation of exchanging for refresh_token"""
        api_key = self._extract_google_api_key_from_refresh_url(refresh_url)
        if not api_key:
            api_key = "AIzaSyB1Rp8f_A9sxd5wB5JcG7P8Q7Y7Q6Y2Q7Y"  # fallback

        url = f"https://identitytoolkit.googleapis.com/v1/accounts:signInWithCustomToken?key={api_key}"

        form_data = {
            "returnSecureToken": "true",
            "token": id_token
        }

        headers = self._get_headers("application/x-www-form-urlencoded")

        print("🔄 Exchanging idToken for refresh_token...")

        async with self.session.post(url, data=form_data, headers=headers) as response:
            # Handle rate limiting
            if response.status == 429:
                retry_after = response.headers.get('Retry-After')
                error_msg = f"Rate limited (429)"
                if retry_after:
                    error_msg += f", Retry-After: {retry_after}"
                raise Exception(error_msg)
            elif response.status != 200:
                error_text = await response.text()
                raise Exception(f"Failed to exchange for refresh_token: {response.status} - {error_text}")

            data = await response.json()

            if 'error' in data:
                raise Exception(f"Identity Toolkit error: {data['error']['message']}")

            refresh_token = data.get('refreshToken')
            if not refresh_token:
                raise Exception("No refreshToken in response")

            print("✅ refresh_token obtained successfully")
            return refresh_token

    async def exchange_for_access_token(self, refresh_token: str, refresh_url: str) -> str:
        """Step 3: Exchange refresh_token for access_token"""
        return await self._retry_request(self._exchange_for_access_token_request, refresh_token, refresh_url)

    async def _exchange_for_access_token_request(self, refresh_token: str, refresh_url: str) -> str:
        """Actual implementation of exchanging for access_token"""
        headers = self._get_headers("application/x-www-form-urlencoded")
        headers["accept"] = "*/*"

        form_data = {
            "grant_type": "refresh_token",
            "refresh_token": refresh_token
        }

        # Calculate content length
        form_body = "&".join([f"{k}={v}" for k, v in form_data.items()])
        headers["content-length"] = str(len(form_body))

        print("🔄 Exchanging refresh_token for access_token...")

        async with self.session.post(refresh_url, data=form_data, headers=headers) as response:
            # Handle rate limiting
            if response.status == 429:
                retry_after = response.headers.get('Retry-After')
                error_msg = f"Rate limited (429)"
                if retry_after:
                    error_msg += f", Retry-After: {retry_after}"
                raise Exception(error_msg)
            elif response.status != 200:
                error_text = await response.text()
                raise Exception(f"Failed to exchange for access_token: {response.status} - {error_text}")

            data = await response.json()

            if 'error' in data:
                raise Exception(f"Warp token error: {data.get('error_description', data['error'])}")

            access_token = data.get('access_token')
            if not access_token:
                raise Exception("No access_token in response")

            print("✅ access_token obtained successfully")
            return access_token

    async def acquire_anonymous_access_token(self) -> str:
        """Complete three-step authentication flow"""
        try:
            # Step 1: Create anonymous user
            id_token = await self.create_anonymous_user()

            # Add delay to prevent rate limiting
            await asyncio.sleep(random.uniform(1, 3))

            # Get refresh URL from environment or use default
            refresh_url = os.getenv('WARP_REFRESH_URL', DEFAULT_REFRESH_URL)

            # Step 2: Exchange for refresh_token
            refresh_token = await self.exchange_for_refresh_token(id_token, refresh_url)

            # Save refresh_token
            await self.save_env_tokens({"WARP_REFRESH_TOKEN": refresh_token})

            # Add delay to prevent rate limiting
            await asyncio.sleep(random.uniform(1, 3))

            # Step 3: Exchange for access_token
            access_token = await self.exchange_for_access_token(refresh_token, refresh_url)

            # Save both tokens
            await self.save_env_tokens({
                "WARP_JWT": access_token,
                "WARP_REFRESH_TOKEN": refresh_token
            })

            print("✅ Both access_token and refresh_token saved successfully")
            return access_token

        except Exception as e:
            print(f"❌ Authentication failed: {e}")
            raise

    async def refresh_jwt_token(self) -> Optional[str]:
        """Refresh JWT using existing refresh_token with fallback mechanism"""
        # Try environment refresh token first
        tokens = await self.load_env_tokens()
        env_refresh_token = tokens.get('WARP_REFRESH_TOKEN')

        # Prepare payload and headers
        if env_refresh_token:
            payload = f"grant_type=refresh_token&refresh_token={env_refresh_token}".encode("utf-8")
        else:
            # Use fallback refresh token
            payload = base64.b64decode(FALLBACK_REFRESH_TOKEN_B64)

        refresh_url = os.getenv('WARP_REFRESH_URL', DEFAULT_REFRESH_URL)

        headers = {
            "x-warp-client-version": CLIENT_VERSION,
            "x-warp-os-category": OS_CATEGORY,
            "x-warp-os-name": OS_NAME,
            "x-warp-os-version": OS_VERSION,
            "content-type": "application/x-www-form-urlencoded",
            "accept": "*/*",
            "accept-encoding": "gzip, br",
            "content-length": str(len(payload)),
        }

        try:
            print("🔄 Refreshing JWT token...")

            async with self.session.post(refresh_url, headers=headers, data=payload) as response:
                if response.status == 200:
                    token_data = await response.json()
                    access_token = token_data.get("access_token")

                    if access_token:
                        await self.save_env_tokens({"WARP_JWT": access_token})
                        print("✅ JWT refreshed successfully")
                        return access_token
                    else:
                        print("❌ No access_token in refresh response")
                        return None
                else:
                    print(f"❌ Token refresh failed: {response.status}")
                    print(f"Response: {await response.text()}")
                    return None

        except Exception as e:
            print(f"❌ Error refreshing JWT: {e}")
            return None

    async def get_valid_jwt(self) -> Optional[str]:
        """Get valid JWT, refreshing if necessary - matching successful implementation strategy"""
        # Load current JWT
        tokens = await self.load_env_tokens()
        current_jwt = tokens.get('WARP_JWT')

        if not current_jwt:
            print("⚠️  No JWT token found, attempting to refresh...")
            refreshed = await self.refresh_jwt_token()
            if refreshed:
                return refreshed
            else:
                print("⚠️  Refresh failed, getting new anonymous token...")
                return await self.acquire_anonymous_access_token()

        # Check if current JWT is expired (with 2 minute buffer)
        if self.is_token_expired(current_jwt, buffer_minutes=2):
            print("⚠️  JWT token is expired or expiring soon, attempting to refresh...")
            refreshed = await self.refresh_jwt_token()
            if refreshed:
                return refreshed
            else:
                print("⚠️  JWT token refresh failed, getting new anonymous token...")
                return await self.acquire_anonymous_access_token()

        return current_jwt

    async def check_and_refresh_token(self) -> bool:
        """Check and refresh token if needed - matching successful implementation"""
        current_jwt = os.getenv("WARP_JWT")
        if not current_jwt:
            print("⚠️  No JWT token found in environment")
            token_data = await self._refresh_jwt_token_internal()
            if token_data and "access_token" in token_data:
                return self._update_env_file(token_data["access_token"])
            return False

        print("🔍 Checking current JWT token expiration...")
        if self.is_token_expired(current_jwt, buffer_minutes=15):
            print("⏰ JWT token is expired or expiring soon, refreshing...")
            token_data = await self._refresh_jwt_token_internal()
            if token_data and "access_token" in token_data:
                new_jwt = token_data["access_token"]
                if not self.is_token_expired(new_jwt, buffer_minutes=0):
                    print("✅ New token is valid")
                    return self._update_env_file(new_jwt)
                else:
                    print("⚠️  New token appears to be invalid or expired")
                    return False
            else:
                print("❌ Failed to get new token from refresh")
                return False
        else:
            # Show token info
            payload = self._decode_jwt(current_jwt)
            if payload and "exp" in payload:
                expiry_time = payload["exp"]
                time_left = expiry_time - datetime.now().timestamp()
                hours_left = time_left / 3600
                print(f"✅ Current token is still valid ({hours_left:.1f} hours remaining)")
            else:
                print("✅ Current token appears valid")
            return True

    async def _refresh_jwt_token_internal(self) -> dict:
        """Internal refresh method matching successful implementation"""
        # Try environment refresh token first, fallback to built-in
        env_refresh = os.getenv("WARP_REFRESH_TOKEN")
        if env_refresh:
            payload = f"grant_type=refresh_token&refresh_token={env_refresh}".encode("utf-8")
        else:
            payload = base64.b64decode(FALLBACK_REFRESH_TOKEN_B64)

        refresh_url = os.getenv('WARP_REFRESH_URL', DEFAULT_REFRESH_URL)

        headers = {
            "x-warp-client-version": CLIENT_VERSION,
            "x-warp-os-category": OS_CATEGORY,
            "x-warp-os-name": OS_NAME,
            "x-warp-os-version": OS_VERSION,
            "content-type": "application/x-www-form-urlencoded",
            "accept": "*/*",
            "accept-encoding": "gzip, br",
            "content-length": str(len(payload)),
        }

        try:
            async with self.session.post(refresh_url, headers=headers, data=payload) as response:
                if response.status == 200:
                    token_data = await response.json()
                    print("✅ Token refresh successful")
                    return token_data
                else:
                    print(f"❌ Token refresh failed: {response.status}")
                    print(f"Response: {await response.text()}")
                    return {}
        except Exception as e:
            print(f"❌ Error refreshing token: {e}")
            return {}

    async def _update_env_file(self, new_jwt: str) -> bool:
        """Update .env file with new JWT"""
        env_file_path = "/tmp/.env" if os.path.exists("/tmp/.env") else ".env"
        try:
            await self.save_env_tokens({"WARP_JWT": new_jwt})
            print("✅ Updated .env file with new JWT token")
            return True
        except Exception as e:
            print(f"❌ Error updating .env file: {e}")
            return False

    async def get_valid_jwt_with_fallback(self, max_attempts=2) -> Optional[str]:
        """Get valid JWT with multiple fallback attempts"""
        for attempt in range(max_attempts):
            print(f"🔄 Attempt {attempt + 1}/{max_attempts} to get valid JWT...")

            jwt = await self.get_valid_jwt()
            if jwt:
                return jwt

            if attempt < max_attempts - 1:
                # Longer delay between attempts
                delay = random.uniform(5, 10)
                print(f"⏳ Waiting {delay:.1f} seconds before next attempt...")
                await asyncio.sleep(delay)

        print(f"❌ Failed to get valid JWT after {max_attempts} attempts")
        return None

    async def create_new_anonymous_account(self) -> Dict[str, str]:
        """Create a completely new anonymous account for batch registration"""
        print("🆕 Creating new anonymous account for batch registration...")

        try:
            # Step 1: Create new anonymous user
            id_token = await self.create_anonymous_user()

            # Add delay to prevent rate limiting
            await asyncio.sleep(random.uniform(2, 5))

            # Step 2: Exchange for refresh_token
            refresh_url = os.getenv('WARP_REFRESH_URL', DEFAULT_REFRESH_URL)
            refresh_token = await self.exchange_for_refresh_token(id_token, refresh_url)

            # Add delay to prevent rate limiting
            await asyncio.sleep(random.uniform(2, 5))

            # Step 3: Exchange for access_token
            access_token = await self.exchange_for_access_token(refresh_token, refresh_url)

            # Save both tokens
            await self.save_env_tokens({
                "WARP_JWT": access_token,
                "WARP_REFRESH_TOKEN": refresh_token
            })

            print("✅ New anonymous account created successfully!")

            # Return complete account information
            return {
                "jwt_token": access_token,
                "refresh_token": refresh_token,
                "id_token": id_token
            }

        except Exception as e:
            print(f"❌ Failed to create new anonymous account: {e}")
            raise

    async def batch_create_accounts(self, count: int, delay_between: float = 3.0) -> List[Dict[str, str]]:
        """Create multiple anonymous accounts for batch registration"""
        print(f"🚀 Starting batch creation of {count} anonymous accounts...")
        print(f"⏱️  Delay between accounts: {delay_between} seconds")

        accounts = []
        success_count = 0
        failure_count = 0

        for i in range(count):
            print(f"\n📝 Creating account {i + 1}/{count}...")
            print("=" * 60)

            try:
                account = await self.create_new_anonymous_account()
                accounts.append(account)
                success_count += 1

                print(f"✅ Account {i + 1} created successfully!")
                print(f"   JWT: {account['jwt_token'][:50]}...")
                print(f"   Refresh: {account['refresh_token'][:30]}...")

                # Show account details
                try:
                    payload = self._decode_jwt(account['jwt_token'])
                    if payload and 'user_id' in payload:
                        print(f"   User ID: {payload['user_id']}")
                    if payload and 'exp' in payload:
                        import datetime
                        expiry = datetime.datetime.fromtimestamp(payload['exp'])
                        print(f"   Expires: {expiry}")
                except:
                    pass

            except Exception as e:
                failure_count += 1
                print(f"❌ Failed to create account {i + 1}: {e}")

            # Add delay between creations (except for the last one)
            if i < count - 1:
                print(f"⏳ Waiting {delay_between} seconds before next account...")
                await asyncio.sleep(delay_between)

        print(f"\n🎯 Batch Creation Summary:")
        print(f"   ✅ Successful: {success_count}")
        print(f"   ❌ Failed: {failure_count}")
        print(f"   📊 Success Rate: {success_count/count*100:.1f}%")

        return accounts

    def load_all_accounts(self) -> List[Dict[str, str]]:
        """Load all accounts from accounts.json file"""
        accounts = []
        accounts_file = "accounts.json"

        if not os.path.exists(accounts_file):
            print("📁 accounts.json file not found")
            return accounts

        try:
            with open(accounts_file, 'r', encoding='utf-8') as f:
                accounts_data = json.load(f)

            print(f"📁 Loaded {len(accounts_data)} accounts from {accounts_file}")

            for account in accounts_data:
                # Convert to the expected format for backward compatibility
                formatted_account = {
                    'user_id': account.get('user_id', account.get('local_id', 'unknown')),
                    'jwt_token': account.get('id_token', ''),
                    'refresh_token': account.get('refresh_token', ''),
                    'source_file': accounts_file,
                    'email': account.get('email', ''),
                    'plan': account.get('plan', 'Free'),
                    'local_id': account.get('local_id', ''),
                    'used_limit': account.get('used_limit', 0),
                    'total_limit': account.get('total_limit', 150),
                    'end_time': account.get('end_time', ''),
                    'experiment_id': account.get('experiment_id', ''),
                    'created_at': account.get('created_at', 0),
                    'expires_at': account.get('expiration_time', ''),
                    'full_data': account  # Store the complete structured data
                }
                accounts.append(formatted_account)

        except Exception as e:
            print(f"❌ Failed to load accounts from {accounts_file}: {e}")

        return accounts

    def export_accounts_to_file(self, accounts: List[Dict[str, str]], filename: str, format_type: str = 'json'):
        """Export accounts to a file in various formats"""
        try:
            if format_type.lower() == 'json':
                with open(filename, 'w', encoding='utf-8') as f:
                    json.dump(accounts, f, indent=2, ensure_ascii=False)
            elif format_type.lower() == 'csv':
                import csv
                with open(filename, 'w', newline='', encoding='utf-8') as f:
                    if accounts:
                        writer = csv.DictWriter(f, fieldnames=accounts[0].keys())
                        writer.writeheader()
                        writer.writerows(accounts)
            elif format_type.lower() == 'txt':
                with open(filename, 'w', encoding='utf-8') as f:
                    for i, account in enumerate(accounts, 1):
                        f.write(f"Account {i}:\n")
                        f.write(f"  User ID: {account['user_id']}\n")
                        f.write(f"  JWT Token: {account['jwt_token']}\n")
                        f.write(f"  Refresh Token: {account['refresh_token']}\n")
                        f.write(f"  Source: {account['source_file']}\n")
                        f.write(f"  Created: {account['created_at']}\n")
                        f.write(f"  Expires: {account['expires_at']}\n")
                        f.write("=" * 80 + "\n")
            else:
                raise ValueError(f"Unsupported format: {format_type}")

            print(f"✅ Exported {len(accounts)} accounts to {filename} ({format_type})")
            return True

        except Exception as e:
            print(f"❌ Failed to export accounts: {e}")
            return False

async def main():
    parser = argparse.ArgumentParser(description="Warp Anonymous Authentication")
    parser.add_argument("--refresh", action="store_true", help="Refresh existing token")
    parser.add_argument("--validate", action="store_true", help="Validate current token")
    parser.add_argument("--new", action="store_true", help="Get new token")
    parser.add_argument("--status", action="store_true", help="Show current token status")
    parser.add_argument("--new-account", action="store_true", help="Create completely new anonymous account")
    parser.add_argument("--batch", type=int, help="Create multiple anonymous accounts (batch registration)")
    parser.add_argument("--batch-delay", type=float, default=3.0, help="Delay between batch account creation in seconds (default: 3.0)")
    parser.add_argument("--attempts", type=int, default=2, help="Maximum attempts (default: 2)")
    parser.add_argument("--retry-delay", type=float, default=1.0, help="Base retry delay in seconds (default: 1.0)")
    parser.add_argument("--max-delay", type=float, default=60, help="Maximum retry delay in seconds (default: 60)")

    # Proxy arguments
    parser.add_argument("--proxy", action="store_true", help="Enable proxy for all requests")
    parser.add_argument("--proxy-host", default="127.0.0.1", help="Proxy host (default: 127.0.0.1)")
    parser.add_argument("--proxy-port", default="10809", help="Proxy port (default: 10809)")
    parser.add_argument("--proxy-type", choices=["http", "https", "socks5", "socks5h"], default="http", help="Proxy type (default: http)")
    parser.add_argument("--list-accounts", action="store_true", help="List all saved accounts from .env files")
    parser.add_argument("--export-accounts", type=str, help="Export accounts to a file (format: json/csv/txt)")

    args = parser.parse_args()

    async with WarpAuthenticator(
        max_retries=3,
        base_delay=args.retry_delay,
        max_delay=args.max_delay,
        use_proxy=args.proxy,
        proxy_host=args.proxy_host,
        proxy_port=args.proxy_port,
        proxy_type=args.proxy_type
    ) as auth:
        # Show proxy status
        if args.proxy:
            print(f"🔗 Proxy Enabled: {args.proxy_type}://{args.proxy_host}:{args.proxy_port}")
        else:
            print("🔗 Proxy: Disabled")

        # Test proxy connection if enabled
        if args.proxy:
            print("🧪 Testing proxy connection...")
            try:
                import requests
                proxies = {
                    'http': f"{args.proxy_type}://{args.proxy_host}:{args.proxy_port}",
                    'https': f"{args.proxy_type}://{args.proxy_host}:{args.proxy_port}"
                }
                response = requests.get('http://httpbin.org/ip', proxies=proxies, timeout=10)
                if response.status_code == 200:
                    print("✅ Proxy connection test successful")
                    print(f"   Proxy IP: {response.json().get('origin', 'Unknown')}")
                else:
                    print(f"⚠️  Proxy test returned status: {response.status_code}")
            except Exception as e:
                print(f"❌ Proxy connection test failed: {e}")

        if args.status:
            tokens = await auth.load_env_tokens()
            jwt = tokens.get('WARP_JWT')
            refresh_token = tokens.get('WARP_REFRESH_TOKEN')

            print("📋 Token Status:")
            print(f"   JWT: {'✅ Present' if jwt else '❌ Missing'}")
            print(f"   Refresh Token: {'✅ Present' if refresh_token else '❌ Missing'}")

            if jwt:
                if auth.is_token_expired(jwt):
                    print(f"   JWT Status: ⚠️  Expired")
                else:
                    print(f"   JWT Status: ✅ Valid")
                    payload = auth._decode_jwt(jwt)
                    if payload and 'exp' in payload:
                        from datetime import datetime
                        expiry = datetime.fromtimestamp(payload['exp'])
                        print(f"   JWT Expires: {expiry}")

            if refresh_token:
                print(f"   Refresh Token: {refresh_token[:30]}...")

        elif args.validate:
            tokens = await auth.load_env_tokens()
            jwt = tokens.get('WARP_JWT')

            if not jwt:
                print("❌ No JWT found")
                return

            if auth.is_token_expired(jwt):
                print("⚠️  JWT is expired")
            else:
                print("✅ JWT is valid")

                # Show token info
                payload = auth._decode_jwt(jwt)
                if payload:
                    expiry = datetime.fromtimestamp(payload['exp'])
                    print(f"   Expires: {expiry}")

        elif args.refresh:
            jwt = await auth.refresh_jwt_token()
            if jwt:
                print("✅ Token refreshed successfully")
                print("=" * 80)
                print("🔄 刷新后的Token信息:")
                print(f"新JWT Token: {jwt}")

                # Show refresh token
                tokens = await auth.load_env_tokens()
                refresh_token = tokens.get('WARP_REFRESH_TOKEN')
                if refresh_token:
                    print(f"Refresh Token: {refresh_token}")

                # Show token payload
                payload = auth._decode_jwt(jwt)
                if payload:
                    print("📦 新JWT Payload:")
                    for key, value in payload.items():
                        print(f"   {key}: {value}")

                    if 'exp' in payload:
                        expiry = datetime.fromtimestamp(payload['exp'])
                        print(f"⏰ 新Token过期时间: {expiry}")

                print("=" * 80)
            else:
                print("❌ Failed to refresh token")

        elif args.new_account:
            print("🆕 Creating new anonymous account...")
            try:
                account = await auth.create_new_anonymous_account()
                print("=" * 80)
                print("🎯 New Anonymous Account Created:")
                print(f"JWT Token: {account['jwt_token']}")
                print(f"Refresh Token: {account['refresh_token']}")
                print(f"ID Token: {account['id_token']}")

                # Show account details
                payload = auth._decode_jwt(account['jwt_token'])
                if payload:
                    print("📦 Account Payload:")
                    for key, value in payload.items():
                        print(f"   {key}: {value}")

                    if 'exp' in payload:
                        import datetime
                        expiry = datetime.datetime.fromtimestamp(payload['exp'])
                        print(f"⏰ Account Expires: {expiry}")

                print("=" * 80)
            except Exception as e:
                print(f"❌ Failed to create new account: {e}")

        elif args.batch:
            if args.batch <= 0:
                print("❌ Batch count must be greater than 0")
                return

            print(f"🚀 Starting batch creation of {args.batch} accounts...")
            accounts = await auth.batch_create_accounts(args.batch, args.batch_delay)

            if accounts:
                print(f"\n📋 Created {len(accounts)} accounts:")
                for i, account in enumerate(accounts, 1):
                    print(f"\n📝 Account {i}:")
                    print(f"   JWT: {account['jwt_token'][:50]}...")
                    print(f"   Refresh: {account['refresh_token'][:30]}...")

                    # Show unique user ID
                    payload = auth._decode_jwt(account['jwt_token'])
                    if payload and 'user_id' in payload:
                        print(f"   User ID: {payload['user_id']}")

            print("\n✅ Batch creation completed!")

        elif args.list_accounts:
            print("📋 Loading all saved accounts...")
            accounts = auth.load_all_accounts()

            if accounts:
                print(f"\n🎯 Found {len(accounts)} saved accounts:")
                print("=" * 80)
                for i, account in enumerate(accounts, 1):
                    print(f"📝 Account {i}:")

                    # Show structured information if available
                    if 'full_data' in account:
                        full_data = account['full_data']
                        print(f"   Email: {full_data.get('email', 'N/A')}")
                        print(f"   Plan: {full_data.get('plan', 'N/A')}")
                        print(f"   Used Limit: {full_data.get('used_limit', 0)}/{full_data.get('total_limit', 150)}")
                        print(f"   End Time: {full_data.get('end_time', 'N/A')}")
                        print(f"   Local ID: {full_data.get('local_id', 'N/A')}")
                        print(f"   Experiment ID: {full_data.get('experiment_id', 'N/A')}")

                    print(f"   User ID: {account['user_id']}")
                    print(f"   Source: {account['source_file']}")

                    if account['expires_at']:
                        if isinstance(account['expires_at'], str):
                            # It's already a date string
                            print(f"   Expires: {account['expires_at']}")
                        else:
                            # It's a timestamp
                            from datetime import datetime
                            expiry = datetime.fromtimestamp(account['expires_at'])
                            print(f"   Expires: {expiry}")

                    print(f"   JWT: {account['jwt_token'][:50]}...")
                    print(f"   Refresh: {account['refresh_token'][:30]}...")
                    print("-" * 40)
            else:
                print("❌ No saved accounts found")

        elif args.export_accounts:
            print("📤 Exporting accounts...")
            accounts = auth.load_all_accounts()

            if not accounts:
                print("❌ No accounts to export")
                return

            # Determine format from file extension
            filename = args.export_accounts
            if filename.endswith('.json'):
                format_type = 'json'
            elif filename.endswith('.csv'):
                format_type = 'csv'
            elif filename.endswith('.txt'):
                format_type = 'txt'
            else:
                print("❌ Unsupported file format. Use .json, .csv, or .txt")
                return

            success = auth.export_accounts_to_file(accounts, filename, format_type)

        elif args.new or not args.refresh:
            print(f"🚀 Starting authentication with {args.attempts} attempts...")
            jwt = await auth.get_valid_jwt_with_fallback(max_attempts=args.attempts)
            if jwt:
                print("✅ Authentication successful!")
                print("=" * 80)
                print("📋 完整Token信息:")
                print(f"JWT Token: {jwt}")

                # Show refresh token
                tokens = await auth.load_env_tokens()
                refresh_token = tokens.get('WARP_REFRESH_TOKEN')
                if refresh_token:
                    print(f"Refresh Token: {refresh_token}")

                # Show token payload
                payload = auth._decode_jwt(jwt)
                if payload:
                    print("📦 JWT Payload:")
                    for key, value in payload.items():
                        print(f"   {key}: {value}")

                    if 'exp' in payload:
                        expiry = datetime.fromtimestamp(payload['exp'])
                        print(f"⏰ Token过期时间: {expiry}")

                print("=" * 80)
            else:
                print("❌ Authentication failed after all attempts")

if __name__ == "__main__":
    asyncio.run(main())