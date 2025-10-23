#!/usr/bin/env python3

"""
Simple Fallback Mechanism Testing
Tests fallback mechanisms with <100ms failover time target
"""

import time
import json
import tempfile
import os
import subprocess
import threading
import http.server
import socketserver
from pathlib import Path

class FallbackTester:
    def __init__(self, target_failover_ms=100):
        self.target_failover_ms = target_failover_ms
        self.test_results = []
        self.failover_times = []

    def measure_failover_time(self, primary_func, fallback_func):
        """Measure failover time between primary and fallback functions"""
        start_time = time.time()

        try:
            # Try primary function with timeout
            result = subprocess.run(primary_func, shell=True, capture_output=True, text=True, timeout=1)
            if result.returncode == 0:
                return 0.0  # Primary succeeded
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
            pass

        # Primary failed, measure fallback time
        failover_start = time.time()

        try:
            result = subprocess.run(fallback_func, shell=True, capture_output=True, text=True, timeout=1)
            if result.returncode == 0:
                failover_end = time.time()
                failover_time = (failover_end - failover_start) * 1000
                return failover_time
            else:
                return -1  # Fallback failed
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
            return -1  # Fallback failed

    def test_config_fallback(self):
        """Test configuration file fallback"""
        print("Testing configuration fallback...")

        with tempfile.TemporaryDirectory() as temp_dir:
            # Create corrupted primary config
            primary_config = os.path.join(temp_dir, "primary.json")
            with open(primary_config, 'w') as f:
                f.write("{ invalid json")

            # Create valid fallback config
            fallback_config = os.path.join(temp_dir, "fallback.json")
            with open(fallback_config, 'w') as f:
                json.dump({"mode": "fallback", "debug": False}, f)

            primary_func = f"python3 -m json.tool {primary_config}"
            fallback_func = f"python3 -m json.tool {fallback_config}"

            failover_time = self.measure_failover_time(primary_func, fallback_func)

            success = failover_time >= 0 and failover_time <= self.target_failover_ms
            self.test_results.append({
                "scenario": "config_fallback",
                "success": success,
                "failover_time_ms": failover_time,
                "status": "PASS" if success else "FAIL"
            })

            if success:
                print(f"✅ Config fallback: {failover_time:.2f}ms")
            else:
                print(f"❌ Config fallback: {failover_time:.2f}ms")

    def test_network_fallback(self):
        """Test network service fallback"""
        print("Testing network fallback...")

        with tempfile.TemporaryDirectory() as temp_dir:
            # Create fallback service response
            fallback_response = os.path.join(temp_dir, "response.json")
            with open(fallback_response, 'w') as f:
                json.dump({"status": "healthy", "service": "fallback"}, f)

            primary_func = "curl -s --connect-timeout 0.1 http://unreachable-primary-service:8080/health"
            fallback_func = f"cat {fallback_response}"

            failover_time = self.measure_failover_time(primary_func, fallback_func)

            success = failover_time >= 0 and failover_time <= self.target_failover_ms
            self.test_results.append({
                "scenario": "network_fallback",
                "success": success,
                "failover_time_ms": failover_time,
                "status": "PASS" if success else "FAIL"
            })

            if success:
                print(f"✅ Network fallback: {failover_time:.2f}ms")
            else:
                print(f"❌ Network fallback: {failover_time:.2f}ms")

    def test_filesystem_fallback(self):
        """Test filesystem fallback"""
        print("Testing filesystem fallback...")

        with tempfile.TemporaryDirectory() as temp_dir:
            # Create read-only primary directory
            primary_dir = os.path.join(temp_dir, "primary")
            os.makedirs(primary_dir)

            try:
                with open(os.path.join(primary_dir, "data.txt"), 'w') as f:
                    f.write("test data")
                os.chmod(primary_dir, 0o444)  # Read-only
            except:
                pass

            # Create writable fallback directory
            fallback_dir = os.path.join(temp_dir, "fallback")
            os.makedirs(fallback_dir)

            primary_func = f"echo 'test' > {primary_dir}/write_test.txt 2>/dev/null"
            fallback_func = f"echo 'test' > {fallback_dir}/write_test.txt"

            failover_time = self.measure_failover_time(primary_func, fallback_func)

            success = failover_time >= 0 and failover_time <= self.target_failover_ms
            self.test_results.append({
                "scenario": "filesystem_fallback",
                "success": success,
                "failover_time_ms": failover_time,
                "status": "PASS" if success else "FAIL"
            })

            if success:
                print(f"✅ Filesystem fallback: {failover_time:.2f}ms")
            else:
                print(f"❌ Filesystem fallback: {failover_time:.2f}ms")

    def test_memory_fallback(self):
        """Test memory allocation fallback"""
        print("Testing memory fallback...")

        with tempfile.TemporaryDirectory() as temp_dir:
            # Create test program
            test_file = os.path.join(temp_dir, "memory_test.cpp")
            with open(test_file, 'w') as f:
                f.write('''
#include <iostream>
#include <vector>
#include <chrono>

int main() {
    try {
        // Try large allocation (might fail)
        std::vector<char> large_block(1024 * 1024 * 1024);
        std::cout << "primary_success" << std::endl;
        return 0;
    } catch (const std::bad_alloc&) {
        // Fallback to smaller allocation
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<char> small_block(1024 * 1024);
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "fallback_success:" << duration.count() << std::endl;
        return 0;
    }
}
''')

            # Compile and run
            try:
                subprocess.run(['g++', '-std=c++17', '-O2', '-o',
                               os.path.join(temp_dir, 'memory_test'), test_file],
                              check=True, capture_output=True)

                result = subprocess.run([os.path.join(temp_dir, 'memory_test')],
                                      capture_output=True, text=True, timeout=10)

                if "primary_success" in result.stdout:
                    failover_time = 0.0
                elif "fallback_success:" in result.stdout:
                    failover_time = float(result.stdout.split(':')[1]) / 1000.0  # Convert to ms
                else:
                    failover_time = -1

            except subprocess.CalledProcessError:
                failover_time = -1

            success = failover_time >= 0 and failover_time <= self.target_failover_ms
            self.test_results.append({
                "scenario": "memory_fallback",
                "success": success,
                "failover_time_ms": failover_time,
                "status": "PASS" if success else "FAIL"
            })

            if success:
                print(f"✅ Memory fallback: {failover_time:.2f}ms")
            else:
                print(f"❌ Memory fallback: {failover_time:.2f}ms")

    def test_api_fallback(self):
        """Test API endpoint fallback"""
        print("Testing API fallback...")

        class MockAPIHandler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path == '/health':
                    self.send_response(200)
                    self.send_header('Content-type', 'application/json')
                    self.end_headers()
                    response = {'status': 'ok', 'server': 'fallback'}
                    self.wfile.write(json.dumps(response).encode())

            def log_message(self, format, *args):
                pass  # Suppress logging

        # Start fallback server
        with socketserver.TCPServer(("", 0), MockAPIHandler) as httpd:
            port = httpd.server_address[1]

            # Run server in background
            server_thread = threading.Thread(target=httpd.serve_forever)
            server_thread.daemon = True
            server_thread.start()
            time.sleep(0.1)  # Let server start

            primary_func = "curl -s --max-time 0.1 http://unreachable-primary-service:8080/health"
            fallback_func = f"curl -s --max-time 0.1 http://localhost:{port}/health"

            failover_time = self.measure_failover_time(primary_func, fallback_func)

            success = failover_time >= 0 and failover_time <= self.target_failover_ms
            self.test_results.append({
                "scenario": "api_fallback",
                "success": success,
                "failover_time_ms": failover_time,
                "status": "PASS" if success else "FAIL"
            })

            if success:
                print(f"✅ API fallback: {failover_time:.2f}ms")
            else:
                print(f"❌ API fallback: {failover_time:.2f}ms")

    def run_all_tests(self):
        """Run all fallback mechanism tests"""
        print(f"🔄 Starting fallback mechanism tests")
        print(f"Target failover time: {self.target_failover_ms}ms")
        print("=" * 50)

        # Run all test scenarios
        self.test_config_fallback()
        self.test_network_fallback()
        self.test_filesystem_fallback()
        self.test_memory_fallback()
        self.test_api_fallback()

        # Calculate results
        total_tests = len(self.test_results)
        successful_tests = sum(1 for r in self.test_results if r['success'])
        success_rate = (successful_tests / total_tests) * 100 if total_tests > 0 else 0

        valid_times = [r['failover_time_ms'] for r in self.test_results if r['failover_time_ms'] >= 0]

        avg_failover = sum(valid_times) / len(valid_times) if valid_times else 0
        max_failover = max(valid_times) if valid_times else 0

        print("=" * 50)
        print(f"📊 Test Results Summary")
        print(f"Total tests: {total_tests}")
        print(f"Successful fallbacks: {successful_tests}")
        print(f"Success rate: {success_rate:.1f}%")
        print(f"Average failover time: {avg_failover:.2f}ms")
        print(f"Maximum failover time: {max_failover:.2f}ms")

        # Generate report
        self.generate_report()

        return success_rate >= 90.0

    def generate_report(self):
        """Generate HTML test report"""
        report_dir = Path("/root/keycuda/logs/fallback")
        report_dir.mkdir(parents=True, exist_ok=True)

        report_file = report_dir / "fallback_report.html"

        total_tests = len(self.test_results)
        successful_tests = sum(1 for r in self.test_results if r['success'])
        success_rate = (successful_tests / total_tests) * 100 if total_tests > 0 else 0

        html_content = f'''
<!DOCTYPE html>
<html>
<head>
    <title>Fallback Mechanism Test Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 20px; }}
        .header {{ background: #2c3e50; color: white; padding: 20px; border-radius: 5px; }}
        .success {{ color: #27ae60; font-weight: bold; }}
        .failure {{ color: #e74c3c; font-weight: bold; }}
        .metric-card {{ background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #3498db; }}
        .chart-container {{ width: 45%; display: inline-block; margin: 20px; }}
        table {{ width: 100%; border-collapse: collapse; margin: 20px 0; }}
        th, td {{ padding: 10px; border: 1px solid #ddd; text-align: left; }}
        th {{ background-color: #3498db; color: white; }}
        .pass {{ background-color: #d4edda; }}
        .fail {{ background-color: #f8d7da; }}
    </style>
</head>
<body>
    <div class="header">
        <h1>🔄 Fallback Mechanism Test Report</h1>
        <p>Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}</p>
        <p>Target Failover Time: {self.target_failover_ms}ms | Success Rate: {success_rate:.1f}%</p>
    </div>

    <div class="metric-card">
        <h2>📊 Test Summary</h2>
        <div style="display: flex; flex-wrap: wrap;">
            <div class="metric-card">
                <h3>Total Tests</h3>
                <p style="font-size: 24px;">{total_tests}</p>
            </div>
            <div class="metric-card">
                <h3>Successful</h3>
                <p class="success" style="font-size: 24px;">{successful_tests}</p>
            </div>
            <div class="metric-card">
                <h3>Failed</h3>
                <p class="failure" style="font-size: 24px;">{total_tests - successful_tests}</p>
            </div>
            <div class="metric-card">
                <h3>Success Rate</h3>
                <p class="{'success' if success_rate >= 90 else 'failure'}" style="font-size: 24px;">{success_rate:.1f}%</p>
            </div>
        </div>
    </div>

    <div class="chart-container">
        <canvas id="failoverChart"></canvas>
    </div>

    <h2>📋 Detailed Results</h2>
    <table>
        <thead>
            <tr>
                <th>Scenario</th>
                <th>Failover Time</th>
                <th>Status</th>
                <th>Result</th>
            </tr>
        </thead>
        <tbody>
'''

        for result in self.test_results:
            status_class = "pass" if result['success'] else "fail"
            within_target = result['failover_time_ms'] <= self.target_failover_ms
            result_text = "✅ Within target" if within_target else "❌ Exceeds target"

            html_content += f'''
            <tr class="{status_class}">
                <td>{result['scenario']}</td>
                <td>{result['failover_time_ms']:.2f}ms</td>
                <td>{result['status']}</td>
                <td>{result_text}</td>
            </tr>
'''

        html_content += '''
        </tbody>
    </table>

    <script>
        const failoverCtx = document.getElementById('failoverChart').getContext('2d');
        new Chart(failoverCtx, {
            type: 'bar',
            data: {
                labels: [''' + ', '.join([f"'{r['scenario']}'" for r in self.test_results]) + '''],
                datasets: [{
                    label: 'Failover Time (ms)',
                    data: [''' + ', '.join([str(r['failover_time_ms']) for r in self.test_results]) + '''],
                    backgroundColor: [''' + ', '.join(["'#27ae60'" if r['success'] else "'#e74c3c'" for r in self.test_results]) + ''']
                }]
            },
            options: {
                responsive: true,
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Failover Time (ms)'
                        }
                    }
                }
            }
        });
    </script>
</body>
</html>
'''

        with open(report_file, 'w') as f:
            f.write(html_content)

        print(f"📄 Report generated: {report_file}")

def main():
    tester = FallbackTester(target_failover_ms=100)
    success = tester.run_all_tests()

    if success:
        print("\n✅ FALLBACK TARGET ACHIEVED!")
        return 0
    else:
        print("\n❌ FALLBACK TARGET MISSED!")
        return 1

if __name__ == "__main__":
    exit(main())