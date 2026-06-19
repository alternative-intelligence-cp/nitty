#!/usr/bin/env python3
import subprocess
import sys
import os

BINARIES = [
    ".nitpick_make/build/test_throughput",
    ".nitpick_make/build/test_latency",
    ".nitpick_make/build/test_startup",
    ".nitpick_make/build/test_memory"
]

def run_bench():
    print(f"{'Benchmark':<20} | {'Iters':<10} | {'Result':<15}")
    print("-" * 50)
    for bin_path in BINARIES:
        if not os.path.exists(bin_path):
            print(f"Error: {bin_path} not found. Run npkbld build first.")
            continue
        try:
            res = subprocess.run([bin_path], capture_output=True, text=True, check=True)
            for line in res.stdout.strip().split('\n'):
                if not line:
                    continue
                parts = line.split('|')
                if len(parts) >= 3:
                    name = parts[0]
                    iters = parts[1]
                    if "Memory" in name:
                        val = f"{int(parts[2])/1024.0/1024.0:.2f} MB, {parts[3]} FDs"
                    else:
                        val = f"{parts[2]} ms"
                    print(f"{name:<20} | {iters:<10} | {val:<15}")
        except subprocess.CalledProcessError as e:
            print(f"Error running {bin_path}: {e}")

if __name__ == "__main__":
    run_bench()
