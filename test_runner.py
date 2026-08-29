#!/usr/bin/env python3
import os
import sys
import subprocess
import time
from pathlib import Path

# ANSI Color Codes
GREEN = "\033[92m"
RED = "\033[91m"
BOLD = "\033[1m"
CYAN = "\033[96m"
GRAY = "\033[90m"
YELLOW = "\033[93m"
MAGENTA = "\033[95m"
RESET = "\033[0m"

def find_binary(build_dir: Path, name: str) -> Path:
    candidates = [
        build_dir / name,
        build_dir / f"{name}.exe",
        build_dir / "bin" / name
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c
    return None

def main():
    repo_root = Path(__file__).resolve().parent
    build_dir = repo_root / "build"
    tests_dir = repo_root / "tests"
    neg_dir = tests_dir / "negative"
    stdlib_dir = repo_root / "stdlib"

    compiler = find_binary(build_dir, "femtoc")
    if not compiler:
        print(f"{RED}{BOLD}Error:{RESET} Could not find 'femtoc' binary in '{build_dir}'. Build the project first.")
        sys.exit(1)

    unit_test_bin = find_binary(build_dir, "femto_unit_tests")

    e2e_tests = sorted(tests_dir.glob("test_*.femto"))
    neg_tests = sorted(neg_dir.glob("neg_*.femto")) if neg_dir.exists() else []

    print(f"\n{BOLD}{CYAN}======================================================={RESET}")
    print(f"{BOLD}{CYAN}       FEMTO COMPILER FULL-BLOWN TEST SUITE            {RESET}")
    print(f"{BOLD}{CYAN}======================================================={RESET}")
    print(f"{GRAY}Compiler   : {compiler}{RESET}")
    print(f"{GRAY}Unit Runner: {unit_test_bin if unit_test_bin else 'Not Built'}{RESET}")
    print(f"{GRAY}Stdlib     : {stdlib_dir}{RESET}")
    print(f"{GRAY}E2E Tests  : {len(e2e_tests)} test suites{RESET}")
    print(f"{GRAY}Neg Tests  : {len(neg_tests)} test suites{RESET}\n")

    total_passed = 0
    total_failed = 0
    start_global = time.perf_counter()

    # -------------------------------------------------------------------------
    # PART 1: C++ Native Unit Tests (Compiler Parts in Isolation)
    # -------------------------------------------------------------------------
    if unit_test_bin:
        print(f"{BOLD}{MAGENTA}>>> Running Tier 1: C++ Compiler Internal Unit Tests{RESET}")
        unit_res = subprocess.run([str(unit_test_bin)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        print(unit_res.stdout.strip())
        if unit_res.returncode == 0:
            total_passed += 1
        else:
            total_failed += 1
        print()

    # -------------------------------------------------------------------------
    # PART 2: Negative / Diagnostic Tests (Must Fail to Compile)
    # -------------------------------------------------------------------------
    if neg_tests:
        print(f"{BOLD}{MAGENTA}>>> Running Tier 2: Negative Diagnostic Tests (Expected Failures){RESET}")
        out_temp = build_dir / "temp_neg_bin"
        for idx, neg_file in enumerate(neg_tests, start=1):
            test_name = neg_file.stem
            print(f"[{idx:02d}/{len(neg_tests):02d}] Testing {test_name:<32} ... ", end="", flush=True)
            start_t = time.perf_counter()

            compile_cmd = [
                str(compiler),
                str(neg_file),
                "-o", str(out_temp),
                "--stdlib", str(stdlib_dir)
            ]
            comp_res = subprocess.run(compile_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace")
            elapsed = (time.perf_counter() - start_t) * 1000

            if comp_res.returncode != 0:
                print(f"{GREEN}{BOLD}PASSED (Rejected Correctly){RESET} {GRAY}({elapsed:.1f}ms){RESET}")
                total_passed += 1
            else:
                print(f"{RED}{BOLD}FAILED (Compilation Succeeded Unexpectedly){RESET} {GRAY}({elapsed:.1f}ms){RESET}")
                total_failed += 1

            if out_temp.exists():
                out_temp.unlink()
        print()

    # -------------------------------------------------------------------------
    # PART 3: End-to-End Execution Tests
    # -------------------------------------------------------------------------
    print(f"{BOLD}{MAGENTA}>>> Running Tier 3: End-to-End Program Compilation & Execution{RESET}")
    out_bin = build_dir / "temp_test_bin"

    for idx, test_file in enumerate(e2e_tests, start=1):
        test_name = test_file.stem
        print(f"[{idx:02d}/{len(e2e_tests):02d}] Running {test_name:<32} ... ", end="", flush=True)

        start_t = time.perf_counter()

        compile_cmd = [
            str(compiler),
            str(test_file),
            "-o", str(out_bin),
            "--stdlib", str(stdlib_dir)
        ]
        
        comp_res = subprocess.run(compile_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace")
        if comp_res.returncode != 0:
            elapsed = (time.perf_counter() - start_t) * 1000
            print(f"{RED}{BOLD}FAILED (Compilation){RESET} {GRAY}({elapsed:.1f}ms){RESET}")
            print(f"{RED}{comp_res.stderr.strip()}{RESET}\n")
            total_failed += 1
            continue

        try:
            exec_res = subprocess.run([str(out_bin)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", timeout=5)
            elapsed = (time.perf_counter() - start_t) * 1000

            if exec_res.returncode == 0:
                print(f"{GREEN}{BOLD}PASSED{RESET} {GRAY}({elapsed:.1f}ms){RESET}")
                total_passed += 1
            else:
                print(f"{RED}{BOLD}FAILED (Exit Code {exec_res.returncode}){RESET} {GRAY}({elapsed:.1f}ms){RESET}")
                if exec_res.stdout:
                    print(f"{GRAY}{exec_res.stdout.strip()}{RESET}")
                if exec_res.stderr:
                    print(f"{RED}{exec_res.stderr.strip()}{RESET}")
                total_failed += 1
        except subprocess.TimeoutExpired:
            print(f"{RED}{BOLD}FAILED (Timeout){RESET}")
            total_failed += 1
        finally:
            if out_bin.exists():
                out_bin.unlink()

    # Clean up test artifacts
    for artifact in ["femto_test.txt", "suite_test.txt"]:
        p = Path(artifact)
        if p.exists():
            p.unlink()

    total_time = (time.perf_counter() - start_global) * 1000
    print(f"\n{BOLD}{CYAN}-------------------------------------------------------{RESET}")
    print(f"{BOLD}Final Results:{RESET} {GREEN}{total_passed} Passed{RESET}, {RED}{total_failed} Failed{RESET} ({total_time:.1f}ms total)")
    print(f"{BOLD}{CYAN}======================================================={RESET}\n")

    sys.exit(0 if total_failed == 0 else 1)

if __name__ == "__main__":
    main()