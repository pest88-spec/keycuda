#!/usr/bin/env python3

"""
Prometheus Metrics Exporter for Integration Monitoring
Exports integration metrics in Prometheus format for monitoring systems
"""

import json
import time
import os
import sys
import glob
from datetime import datetime, timedelta
from pathlib import Path
import argparse

class IntegrationMetricsExporter:
    def __init__(self, metrics_dir="/root/keycuda/monitoring/metrics", alerts_dir="/root/keycuda/monitoring/alerts"):
        self.metrics_dir = Path(metrics_dir)
        self.alerts_dir = Path(alerts_dir)

    def get_latest_system_metrics(self):
        """Get the most recent system metrics"""
        today = datetime.now().strftime("%Y%m%d")
        metrics_file = self.metrics_dir / f"system_metrics_{today}.json"

        if not metrics_file.exists():
            return {}

        try:
            with open(metrics_file, 'r') as f:
                lines = f.readlines()
                if lines:
                    return json.loads(lines[-1])
        except (json.JSONDecodeError, IndexError):
            pass

        return {}

    def get_latest_integration_metrics(self):
        """Get the most recent integration metrics"""
        today = datetime.now().strftime("%Y%m%d")
        metrics_file = self.metrics_dir / f"integration_metrics_{today}.json"

        if not metrics_file.exists():
            return {}

        try:
            with open(metrics_file, 'r') as f:
                lines = f.readlines()
                if lines:
                    return json.loads(lines[-1])
        except (json.JSONDecodeError, IndexError):
            pass

        return {}

    def get_recent_alerts(self, hours=1):
        """Get alerts from the last N hours"""
        alerts = []
        cutoff_time = datetime.now() - timedelta(hours=hours)

        for alert_file in glob.glob(str(self.alerts_dir / "alert_*.json")):
            try:
                with open(alert_file, 'r') as f:
                    alert_data = json.load(f)
                    alert_time = datetime.fromisoformat(alert_data['timestamp'].replace('Z', '+00:00'))

                    if alert_time > cutoff_time:
                        alerts.append(alert_data)
            except (json.JSONDecodeError, KeyError, ValueError):
                continue

        return alerts

    def calculate_build_success_rate(self, days=7):
        """Calculate build success rate over the last N days"""
        success_count = 0
        total_count = 0

        for i in range(days):
            date = (datetime.now() - timedelta(days=i)).strftime("%Y%m%d")
            metrics_file = self.metrics_dir / f"integration_metrics_{date}.json"

            if metrics_file.exists():
                try:
                    with open(metrics_file, 'r') as f:
                        for line in f:
                            metrics = json.loads(line)
                            if 'integration' in metrics:
                                success_rate = metrics['integration'].get('build_success_rate', 0)
                                if success_rate > 0:
                                    success_count += success_rate
                                    total_count += 1
                except (json.JSONDecodeError, KeyError):
                    continue

        if total_count > 0:
            return success_count / total_count
        return 0

    def export_metrics(self):
        """Export all metrics in Prometheus format"""
        metrics = []

        # System metrics
        system_metrics = self.get_latest_system_metrics()
        if system_metrics and 'system' in system_metrics:
            sys_data = system_metrics['system']
            metrics.append(f"integration_cpu_usage_percent {sys_data.get('cpu_usage_percent', 0)}")
            metrics.append(f"integration_memory_usage_percent {sys_data.get('memory_usage_percent', 0)}")
            metrics.append(f"integration_disk_usage_percent {sys_data.get('disk_usage_percent', 0)}")
            metrics.append(f"integration_load_average {sys_data.get('load_average', 0)}")

        # GPU metrics
        if system_metrics and 'gpu' in system_metrics:
            gpu_data = system_metrics['gpu']
            metrics.append(f"integration_gpu_usage_percent {gpu_data.get('usage_percent', 0)}")
            metrics.append(f"integration_gpu_memory_usage_percent {gpu_data.get('memory_usage_percent', 0)}")
            metrics.append(f"integration_gpu_temperature_celsius {gpu_data.get('temperature_celsius', 0)}")

        # Integration metrics
        integration_metrics = self.get_latest_integration_metrics()
        if integration_metrics and 'integration' in integration_metrics:
            int_data = integration_metrics['integration']
            metrics.append(f"integration_files_total {int_data.get('total_files', 0)}")
            metrics.append(f"integration_size_bytes {int_data.get('total_size_bytes', 0)}")

            # Build success rate
            build_success_rate = int_data.get('build_success_rate', 0)
            metrics.append(f"integration_build_success_rate {build_success_rate}")

            # Health status as a metric (1=healthy, 0.5=degraded, 0=unhealthy)
            health_status = int_data.get('health_status', 'unknown')
            health_value = {'healthy': 1, 'degraded': 0.5, 'unhealthy': 0}.get(health_status, 0)
            metrics.append(f"integration_health_status {health_value}")

        # Alert metrics
        recent_alerts = self.get_recent_alerts(1)
        alert_counts = {'critical': 0, 'warning': 0, 'info': 0}
        for alert in recent_alerts:
            severity = alert.get('severity', 'info').lower()
            if severity in alert_counts:
                alert_counts[severity] += 1

        metrics.append(f"integration_alerts_critical_total {alert_counts['critical']}")
        metrics.append(f"integration_alerts_warning_total {alert_counts['warning']}")
        metrics.append(f"integration_alerts_info_total {alert_counts['info']}")
        metrics.append(f"integration_alerts_total {len(recent_alerts)}")

        # Historical build success rate
        historical_success_rate = self.calculate_build_success_rate(7)
        metrics.append(f"integration_build_success_rate_7d_avg {historical_success_rate}")

        # Export timestamp
        metrics.append(f"integration_last_export_timestamp {time.time()}")

        return "\n".join(metrics)

def main():
    parser = argparse.ArgumentParser(description='Export integration metrics in Prometheus format')
    parser.add_argument('--metrics-dir', default='/root/keycuda/monitoring/metrics',
                       help='Directory containing metrics files')
    parser.add_argument('--alerts-dir', default='/root/keycuda/monitoring/alerts',
                       help='Directory containing alert files')
    parser.add_argument('--port', type=int, default=9101,
                       help='Port to run HTTP server on')
    parser.add_argument('--once', action='store_true',
                       help='Export metrics once and exit')

    args = parser.parse_args()

    exporter = IntegrationMetricsExporter(args.metrics_dir, args.alerts_dir)

    if args.once:
        # Export once and print to stdout
        print(exporter.export_metrics())
        return

    # Run HTTP server for continuous scraping
    from http.server import HTTPServer, BaseHTTPRequestHandler

    class MetricsHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/metrics':
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain; version=0.0.4')
                self.end_headers()
                metrics = exporter.export_metrics()
                self.wfile.write(metrics.encode('utf-8'))
            elif self.path == '/health':
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.end_headers()
                self.wfile.write(b'OK')
            else:
                self.send_response(404)
                self.end_headers()

        def log_message(self, format, *args):
            # Suppress log messages
            pass

    server = HTTPServer(('', args.port), MetricsHandler)
    print(f"Starting Prometheus metrics exporter on port {args.port}")
    print(f"Metrics available at: http://localhost:{args.port}/metrics")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down metrics exporter")
        server.shutdown()

if __name__ == '__main__':
    main()