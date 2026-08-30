# Femto Programming Language

Femto is a modern, statically typed, compiled systems programming language designed for deterministic performance, high safety guarantees, and low-level hardware control with zero runtime overhead.

This repository contains the complete language specification, standard library, Language Server Protocol (`femtoc --lsp`) daemon, reference compiler implementation written in ISO C++20 generating direct x86-64 NASM assembly for **Linux** (System V AMD64 ABI / ELF64) and **Windows** (Microsoft x64 ABI / Win64 / MSVC / Clang / GCC), and the official Visual Studio Code extension.

---

## Table of Contents

- [Key Features](#key-features)
- [Language Specification](#language-specification)
  - [1. Type System](#1-type-system)
  - [2. Literals and Constants](#2-literals-and-constants)
  - [3. Declarations and Mutability](#3-declarations-and-mutability)
  - [4. Functions & Variadics](#4-functions--variadics)
  - [5. Generics](#5-generics)
  - [6. Pointers, Arrays, and Slices](#6-pointers-arrays-and-slices)
  - [7. Structs, Enums, and Unions](#7-structs-enums-and-unions)
  - [8. Operators and Casting](#8-operators-and-casting)
  - [9. Control Flow & `defer`](#9-control-flow--defer)
  - [10. Error Handling (`!T` / `!void`)](#10-error-handling-t-and-void)
  - [11. Modules, Visibility, and C Interop](#11-modules-visibility-and-c-interop)
  - [12. Compile-Time Metaprogramming](#12-compile-time-metaprogramming)
- [ABI and Calling Conventions](#abi-and-calling-conventions)
- [Compiler & LSP Architecture](#compiler--lsp-architecture)
- [Project Layout](#project-layout)
- [Editor Support (VS Code & LSP)](#editor-support-vs-code--lsp)
- [Prerequisites and Toolchain](#prerequisites-and-toolchain)
- [Building the Compiler](#building-the-compiler)
- [Automated Test Suite](#automated-test-suite)
- [CLI Reference](#cli-reference)
- [Quick Start Example](#quick-start-example)
- [License](#license)

---

## Key Features

- **No Undefined States:** Mandatory variable initialization; uninitialized memory reads are syntactically and semantically rejected.
- **Fixed-Width Primitives:** Platform-independent numeric sizing (`int8`–`int512`, `uint8`–`uint512`, `float16`–`float128`, `uint64` indexing).
- **Cross-Platform Native Code Generation:** Direct x86-64 NASM emission targeting **Linux (System V AMD64 ABI)** and **Windows (Microsoft x64 ABI)** with automatic MSVC/UCRT/SDK toolchain discovery.
- **Type-Erased Variadics (`any... args`):** First-class polymorphic formatting and variadic functions with `{}` placeholder templates and zero hidden heap allocations.
- **Deterministic Cleanup (`defer`):** LIFO resource cleanup on block exits, early returns, and error unwraps with automatic register preservation (`RAX`, `RDX`, `XMM0`).
- **Compile-Time Reflection & Constant Evaluation:** Reflection intrinsics (`@typeof`, `@file`, `@line`, `@target`, `@arch`, `@endian`, `@sizeof`, `@alignof`) and static conditional compilation (`#if` / `#else`).
- **Array Fill Syntax:** Contiguous repeat-fill array initialization syntax (`int32[8] zeroed = [0...];`).
- **Zero-Cost Generics:** Fully monomorphized compile-time generics for functions, structs, and unions with zero runtime metadata overhead.
- **Explicit Type Conversions:** Clear taxonomy distinguishing checked widening/narrowing casts (`@cast`) and bit reinterpretation (`@bitcast`).
- **Result-Based Error Handling:** Built-in discriminated union result types (`!T`, `!void`) with syntactic unwrapping and propagation (`??`) with zero exception overhead.
- **Full Language Server Protocol (`femtoc --lsp`):** Out-of-the-box live diagnostics, type hovers, autocompletion with snippets & member fields, signature help, definition navigation (`F12`), symbol renaming (`F2`), references (`Shift+F12`), document formatting (`Shift+Alt+F`), and symbol outlines for VS Code.

---

## Language Specification

### 1. Type System

#### Primitive Value Types

| Category | Types | Details |
|---|---|---|
| **Signed Integers** | `int8`, `int16`, `int32`, `int64`, `int128`, `int256`, `int512` | Two's-complement arithmetic. Native 128-bit multi-register operations (`ADC`/`SBB`/`CQO`). |
| **Unsigned Integers** | `uint8`, `uint16`, `uint32`, `uint64`, `uint128`, `uint256`, `uint512` | Binary unsigned representation. |
| **Floating-Point** | `float16`, `float32`, `float64`, `float128` | IEEE 754 standard formats passed via SSE vector registers (`XMM0`–`XMM7` on Linux, `XMM0`–`XMM3` on Windows). |
| **Boolean** | `bool8`, `bool16`, `bool32`, `bool64`, `bool128`, `bool256`, `bool512` | `0` is `false`; non-zero is `true`. Logical operators yield canonical `1`. |
| **Code Units** | `char8`, `char16`, `char32` | Sized Unicode code unit primitives (UTF-8, UTF-16, UTF-32). |
| **Strings** | `string8`, `string16`, `string32` | Sized string references. Supports native equality (`==`) and inequality (`!=`). |
| **Type Reflection** | `any` | 16-byte value fat-pointer: `{ int64 data, uint64 type_id }`. |
| **Unit / Void** | `void` | Return-type marker for procedures. |

*All indexing, lengths, and capacities strictly use `uint64`.*

---

### 2. Literals and Constants

- **Integers**: Decimal (`42`), Hexadecimal (`0xFF`), Binary (`0b1010`), Octal (`0o755`), with optional digit separators (`1_000_000`).
- **Floats**: `3.14159`, `1.0e-9`, `2.5E+3`.
- **Characters & Strings**: `'a'`, `'\u{1F600}'`, `"standard string\n"`, and raw strings `` `C:\raw\path` ``.
- **Pointers**: `null` (assignable only to raw pointers `T*` or comparable with pointer types).

---

### 3. Declarations and Mutability

Every variable must be explicitly initialized upon declaration:

```c++
int32 x = 5;
const int32 y = 100;             // Immutable binding (compile-time checked)

Point p = { .x = 10, .y = 20 };  // Designated struct field initialization
int32[4] arr = [1, 2, 3, 4];     // Fixed-size stack array
int32[8] zeroed = [0...];        // Repeat fill all 8 elements with 0
int32[] slice = arr[0..3];       // Slice view over subrange [0, 3)

// Compile-time constants
BUFFER_SIZE :: 1024;
PI          :: 3.141592653589793;
```

---

### 4. Functions & Variadics

- **Syntax:** `name :: (params) -> ReturnType { body }`
- **Default Arguments:** Supported on parameters (`int32 b = 10`).
- **Native Variadics (`any... args`):** Heterogeneous arguments bundled into an `any[]` slice on the caller's stack with zero dynamic heap allocations.
- **C-FFI Variadics (`...`):** C-style `...` ellipsis is supported strictly within `extern "C"` blocks.

```c++
#export
add :: (int32 a, int32 b = 10) -> int32 {
    return a + b;
}

// Native variadic function in pure Femto:
#export
log_info :: (string8 header, any... args) -> void {
    std::io::print("[{}] ", header);
    std::io::println("Received {} extra arguments", args.length());
}

// C-FFI variadics:
extern "C" {
    printf :: (string8 fmt, ...) -> int32;
}
```

---

### 5. Generics

Generic functions, structs, and unions are fully monomorphized at compile time with zero runtime metadata overhead:

```c++
max :: <T>(T a, T b) -> T {
    if (a > b) then { return a; }
    else             { return b; }
}

Pair :: struct <K, V> {
    K key;
    V value;
}

// Concrete instantiations (supports nested generic closing '>>')
Pair<string8, int32> p = { .key = "answer", .value = 42 };
int32 m = max<int32>(3, 7);
```

---

### 6. Pointers, Arrays, and Slices

- **Raw Pointer (`T*`)**: Address-of `&x`, dereference `*p`. Single-level auto-dereferencing applies for member access (`p.field` desugars to `(*p).field`).
- **Fixed-Size Array (`T[N]`)**: Contiguous value type allocated inline or on the stack with fixed compile-time size `N`. Supports fill initialization `[val...]`.
- **Slice (`T[]`)**: 16-byte value fat-pointer `{ T* data, uint64 length }`.
- **Bounds Checking**: Sub-slicing `arr[start..end]` and element indexing `slice[i]` perform runtime safety bounds validation, calling panic handlers if violated.

---

### 7. Structs, Enums, and Unions

```c++
Point :: struct {
    int32 x = 0;
    int32 y = 0;
}

Color :: enum -> uint8 {
    red   = 1,
    green = 2,
    blue  = 3
}

DataUnion :: union {
    int32   bits;
    float32 val;
}
```

---

### 8. Operators and Casting

- **Assignment & Compound Ops:** `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `++`, `--` are statements.
- **Arithmetic & Bitwise:** `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `<<`, `>>`.
- **Logical & String Comparisons:** `&&`, `||`, `!`, and string equality `==` / `!=`.
- **Explicit Casting:** `@cast(TargetType, expr)` performs checked widening/narrowing or float/int conversions.
- **Bitcasting:** `@bitcast(TargetType, expr)` reinterprets bit patterns of equal-sized types.

---

### 9. Control Flow & `defer`

#### Conditionals & Loops
```c++
// if/then/else requires 'then'
if (x > 0) then {
    std::io::println("positive: {}", x);
} else {
    std::io::println("non-positive");
}

// While & Do-While
while (condition) { /* ... */ }
do { /* ... */ } while (condition);

// 3-Clause For Loop
for (int32 i = 0; i < 10; i++) {
    if (i == 2) then { continue; }
    if (i == 8) then { break; }
}

// Multi-Level Break / Continue
break(2);     // Break out of 2 enclosing loops/switches
continue(2);  // Continue parent loop
```

#### Deterministic Resource Cleanup (`defer`)
```c++
read_and_process :: (string8 path) -> !int32 {
    int32 fd = std::fs::open_read(path)??;
    defer std::fs::close_file(fd); // Automatically called upon any exit / error unwrap

    int64 buf = std::c::malloc(1024);
    defer std::c::free(buf);       // LIFO order: freed before file is closed

    return success(0);
}
```

#### Pattern Matching & Foreach
```c++
// Pattern match expression with '#' subject binding
int32 mapped = match (code) {
    # == 1 || # == 2 { 100 }
    # >= 10          { 500 }
    default          { 0 }
};

// Foreach iteration over Arrays and Slices
foreach (uint64 idx, int32 val in slice) {
    std::io::println("{}: {}", idx, val);
}
```

---

### 10. Error Handling (`!T` and `!void`)

Femto enforces deterministic error handling without runtime exceptions:

- `!T` represents a result returning either a success payload `T` or an `int32` error code.
- `!void` represents operations returning only an error code status.
- Return values are constructed using `success(val)` / `success()` and `failure(code)` / `failure()`.

```c++
safe_divide :: (int32 a, int32 b) -> !int32 {
    if (b == 0) then {
        return failure(101); // Error code
    }
    return success(a / b);
}

calculate :: (int32 a, int32 b) -> !int32 {
    // '??' unwraps payload or immediately runs defers and returns failure
    int32 result = safe_divide(a, b)??;
    return success(result * 2);
}

main :: () -> int32 {
    // Result branching construct
    calculate(100, 5)
        ?? (int32 val)  { std::io::println("Calculated result: {}", val); }
        :  (int32 code) { std::io::println("Division error code: {}", code); return 1; };

    return 0;
}
```

---

### 11. Modules, Visibility, and C Interop

- Modules are mapped to `.femto` source files:
  ```c++
  import std::io;
  import std::math;
  import std::collection as col;
  ```
- Namespaces:
  ```c++
  namespace Geometry::Circle {
      #export
      area :: (float64 r) -> float64 { return 3.14159 * r * r; }
  }
  ```
- C ABI linkage:
  ```c++
  extern "C" {
      printf :: (string8 fmt, ...) -> int32;
      malloc :: (uint64 size) -> int64;
      free   :: (int64 ptr) -> void;
  }
  ```

---

### 12. Compile-Time Metaprogramming

- Static conditionals: `#if (cond) { ... } #else { ... }`
- Built-in reflection intrinsics:
  - `@typeof(expr)`: Evaluates the compile-time type name of an expression as a `string8`.
  - `@file`, `@line`: Resolves the current source file path and line number.
  - `@target`, `@arch`, `@endian`: Evaluates target architecture and platform strings (`x86_64-linux` / `x86_64-windows`).
  - `@sizeof(T)`, `@alignof(T)`: Computes struct, union, or primitive sizes and alignment bytes.
  - `@cast(TargetType, expr)`, `@bitcast(TargetType, expr)`.

---

## ABI and Calling Conventions

Femto supports target-specific ABI lowering, register allocation, and stack alignment:

| Femto Type | Memory Layout | Linux (System V AMD64 ABI) | Windows (Microsoft x64 ABI) |
|---|---|---|---|
| `int8`..`int64`, `uint8`..`uint64`, `boolN`, `charN`, `T*` | 1, 2, 4, 8 bytes | `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9` | `RCX`, `RDX`, `R8`, `R9` |
| `int128`, `uint128` | 16 bytes (low 64, high 64) | Pair (`RDI:RSI` or `RAX:RDX`) | Register pair / stack frame |
| `float32`, `float64` | IEEE 754 (4 or 8 bytes) | `XMM0`–`XMM7` | `XMM0`–`XMM3` (shadow space preserved) |
| Slice `T[]` / `stringN` / `any[]` | `{ T* data, uint64 len }` (16 bytes) | Passed across two integer registers | Passed across two integer registers |
| Type Reflection `any` | `{ int64 data, uint64 type_id }` | 16 bytes on stack / registers | 16 bytes on stack / registers |
| Result `!T` | `{ int32 code, [pad], T val }` | Register pair (`RAX:RDX`) | Register pair (`RAX:RDX`) |
| Read-Only Data Section | Static literals / tables | `.rodata` | `.rdata` |
| Stack Frame Alignment | 16-byte dynamic | 16-byte boundary | 32-byte shadow space + 16-byte alignment |

---

## Compiler & LSP Architecture

```
                      Source Code (.femto)
                               │
            ┌──────────────────┴──────────────────┐
            ▼ (Batch Compilation)                 ▼ (LSP Mode: femtoc --lsp)
   [ Arena Allocator ]                   [ LSP JSON-RPC Engine ]
            │                                     │
            ▼                                     ├── Live Diagnostics
        [ Lexer ] ──► Token Stream                ├── Type Hover Inspection
            │                                     ├── Definition Navigation (F12)
            ▼                                     ├── Symbol Rename (F2)
       [ Parser ] ──► Abstract Syntax Tree (AST)  ├── Find References (Shift+F12)
            │                                     ├── Document Formatting
            ▼                                     ├── Smart Autocompletion
  [ Type Checker & Sema ]                         ├── Signature Help
   ├── Monomorphization                           └── Document Symbol Outline
   ├── Memory Layout Calculation
   └── Compile-Time Const Eval
            │
            ▼
   [ NASM Code Generator ]
   ├── Dynamic Stack Frame & 16-Byte Boundary Alignment
   ├── System V (Linux) / MS x64 (Windows) ABI Lowering
   ├── Greedy Callee-Saved Register Allocation (R12–R15, XMM6–XMM13)
   ├── LIFO Defer Execution Engine with Register Preservation
   ├── Auto-Packed `any...` Variadic Slices
   └── Multi-Pass Peephole Optimizer
            │
            ▼
   Assembly File (.asm)
            │
            ▼
[ nasm + linker (gcc / clang / MSVC link.exe) ] ──► Native Executable
```

---

## Project Layout

```
femto/
├── CMakeLists.txt              # CMake unified build configuration & check target
├── test_runner.py              # Automated 3-tier cross-platform test runner
├── runtime/
│   ├── femto_rt_linux.asm      # Linux assembly runtime (sys_mmap, sys_write, exit)
│   └── femto_rt_windows.asm    # Windows assembly runtime (MSVC UCRT / Win64)
├── src/
│   ├── main.cpp                # Compiler CLI driver, auto-discovery & module loader
│   ├── common/
│   │   ├── arena.hpp           # Monotonic arena memory allocator
│   │   ├── diagnostic.hpp      # ANSI color source diagnostic reporter
│   │   ├── module_loader.hpp   # Recursive multi-module resolver
│   │   └── source_manager.hpp  # Line/column tracking & binary search
│   ├── frontend/
│   │   ├── token.hpp           # Token taxonomy
│   │   ├── lexer.hpp / .cpp    # Lexer & tokenizer
│   │   ├── ast.hpp             # AST node data structures
│   │   └── parser.hpp / .cpp   # Pratt precedence parser & AST builder
│   ├── sema/
│   │   └── type_checker.hpp / .cpp # Type system, memory layout & monomorphizer
│   ├── codegen/
│   │   └── nasm_emitter.hpp / .cpp # Cross-platform x86-64 Intel NASM code generator
│   └── lsp/
│       ├── json.hpp            # Zero-dependency JSON parser & serializer
│       └── lsp_server.hpp / .cpp # Language Server Protocol daemon
├── stdlib/
│   └── std/
│       ├── builtin.femto       # Builtin runtime assembly bindings
│       ├── c.femto             # Libc / MSVCRT C-FFI wrappers
│       ├── collection.femto    # Dynamic Array<T>
│       ├── fs.femto            # File stream I/O
│       ├── io.femto            # Native {} formatting & printing (println, print)
│       ├── math.femto          # Mathematical functions & constants
│       ├── mem.femto           # Low-level memory management utilities
│       ├── string.femto        # Dynamic growable String builder
│       └── sys.femto           # Process exit & panic utilities
├── editors/
│   └── vscode/                 # Visual Studio Code syntax & LSP extension (v0.2.0)
│       ├── package.json
│       ├── extension.js
│       ├── language-configuration.json
│       └── syntaxes/
│           └── femto.tmLanguage.json
└── tests/
    ├── unit/                   # Tier 1: C++ compiler component unit tests (25 tests)
    │   ├── main.cpp            # Native unit test runner
    │   ├── test_framework.hpp  # Zero-dependency test assertion framework
    │   ├── test_lexer.cpp      # Lexer component test cases
    │   ├── test_parser.cpp     # Parser component test cases
    │   ├── test_sema.cpp       # Semantic analyzer & layout tests
    │   ├── test_codegen.cpp    # Code generator & stack frame tests
    │   └── test_lsp.cpp        # Language Server Protocol unit tests
    ├── negative/               # Tier 2: Negative compilation rejection tests (5 tests)
    │   ├── neg_01_type_mismatch.femto
    │   ├── neg_02_const_mutation.femto
    │   ├── neg_03_break_depth.femto
    │   ├── neg_04_missing_then.femto
    │   └── neg_05_uninitialized.femto
    └── test_*.femto            # Tier 3: End-to-end integration tests (16 test suites)
```

---

## Editor Support (VS Code & LSP)

The repository includes a Language Server Protocol extension for Visual Studio Code in `editors/vscode`.

To install it locally:

### Linux / macOS
```bash
ln -s "$(pwd)/editors/vscode" ~/.vscode/extensions/femto-vscode
cd editors/vscode && npm install
```

### Windows (PowerShell)
```powershell
New-Item -ItemType SymbolicLink -Path "$HOME\.vscode\extensions\femto-vscode" -Target "$PWD\editors\vscode"
cd editors/vscode; npm install
```

Reload VS Code to activate:
- Real-time syntax and semantic error diagnostics.
- Type hover inspection for variables, functions, and structs.
- Autocompletion with snippets and struct member fields.
- Signature help with active parameter highlighting.
- Jump to definition (`F12`), rename symbol (`F2`), and find references (`Shift+F12`).
- Document formatting (`Shift+Alt+F`) and symbol outlines in the sidebar.

---

## Prerequisites and Toolchain

- **C++ Compiler:** GCC ($\ge 12$), Clang ($\ge 16$), or MSVC (Visual Studio 2022+) supporting ISO C++20.
- **Assembler:** [NASM](https://www.nasm.us/) ($\ge 2.15$) in your `PATH`.
- **Build System:** CMake ($\ge 3.20$) and Make/Ninja.
- **Linker / C Runtime:** 
  - **Linux:** GNU `gcc` / `ld` with standard `libc` and `libm`.
  - **Windows:** Visual Studio MSVC tools (`link.exe` / `cl.exe`), Clang, or GCC / MinGW.
- **Python:** Python 3.8+ (for automated test orchestration).

---

## Building the Compiler

### On Linux / macOS:
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### On Windows (Visual Studio / Ninja / MSVC):
```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Generated Binaries:
- `femtoc` / `femtoc.exe`: Reference compiler and LSP server (`femtoc --lsp`).
- `femto_unit_tests` / `femto_unit_tests.exe`: Native C++ component unit test runner.
- `femto_rt.o` / `femto_rt.obj`: Assembled native runtime object.

---

## Automated Test Suite

Femto features a unified 3-tier test suite:

1. **Tier 1 (Internal Unit Tests):** Verifies internal compiler components (`Lexer`, `Parser`, `Sema`, `TypeChecker`, `Monomorphizer`, `NasmEmitter`, `LspServer`) in isolation (25 tests).
2. **Tier 2 (Negative Diagnostic Tests):** Verifies that semantic and syntactic errors (e.g. type mismatches, mutating `const` variables, invalid `break` levels) are rejected with accurate diagnostics (5 tests).
3. **Tier 3 (End-to-End Tests):** Compiles and executes 16 comprehensive `.femto` test suites verifying integer math, floats, control flow, `defer` LIFO cleanup, pattern matching, structs, unions, pointers, slices, results, generics, metaprogramming, C-FFI variadics, native `{}` formatting, array repeat fills (`[val...]`), reflection intrinsics, and stdlib modules.

### Running All Tests

From the repository root:
```bash
python -u test_runner.py
```
Or directly using CMake/Ninja from your build directory:
```bash
cmake --build build --target check
```

---

## CLI Reference

```text
Usage: femtoc <source.femto> [options]

Options:
  -o <file>             Specify output executable name (default: a.out / a.exe)
  --target <target>     Target platform: x86_64-linux, x86_64-windows
  -I <dir>              Add search directory for module imports
  --stdlib <dir>        Specify custom path to standard library
  --lsp                 Start the Language Server Protocol (LSP) daemon
  --no-bounds-check     Disable runtime array and slice bounds checks
  --keep-temps, -k      Keep intermediate assembly (.asm) and object (.obj/.o) files
```

---

## Quick Start Example

1. **Write a program (`hello.femto`):**
   ```c++
   import std::io;
   import std::fs;

   #export
   main :: () -> int32 {
       int32 apples = 3;
       string8 person = "Joe";
       std::io::println("I have {} apples I got from {}", apples, person);

       // Clean resource cleanup using defer
       int32 fd = std::fs::open_write("output.txt")??;
       defer std::fs::close_file(fd);

       std::fs::write_str(fd, "Femto is operational!\n");
       return 0;
   }
   ```

2. **Compile to a native binary:**
   ```bash
   # On Linux:
   build/femtoc hello.femto -o hello
   ./hello

   # On Windows:
   build\Release\femtoc.exe hello.femto -o hello.exe
   .\hello.exe
   ```

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.