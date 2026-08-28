# Femto Programming Language

Femto is a modern, statically typed, compiled systems programming language designed for deterministic performance, high safety guarantees, and low-level control without runtime overhead.

This repository contains the language specification and the reference compiler implementation written in ISO C++26 targeting x86-64 Linux (NASM/libc).

---

## Table of Contents

- [Key Features](#key-features)
- [Language Specification](#language-specification)
  - [Type System](#1-type-system)
  - [Literals and Constants](#2-literals-and-constants)
  - [Declarations and Mutability](#3-declarations-and-mutability)
  - [Functions](#4-functions)
  - [Generics](#5-generics)
  - [Pointers, Arrays, and Slices](#6-pointers-arrays-and-slices)
  - [Structs, Enums, and Unions](#7-structs-enums-and-unions)
  - [Operators and Casting](#8-operators-and-casting)
  - [Control Flow](#9-control-flow)
  - [Error Handling (`!T` / `!void`)](#10-error-handling-t-and-void)
  - [Modules, Visibility, and C Interop](#11-modules-visibility-and-c-interop)
  - [Compile-Time Metaprogramming](#12-compile-time-metaprogramming)
- [ABI and Memory Layout](#abi-and-memory-layout)
- [Compiler Architecture](#compiler-architecture)
- [Prerequisites and Toolchain](#prerequisites-and-toolchain)
- [Quick Start Example](#quick-start-example)
- [License](#license)

---

## Key Features

- **No Undefined States:** Mandatory variable initialization; no implicit uninitialized memory reads.
- **Fixed-Width Primitives:** Strict, platform-independent sizing (`int8`–`int512`, `uint8`–`uint512`, `float16`–`float128`, `uint64` indexing). No ambiguous types like `int` or `size_t`.
- **Zero-Cost Abstractions:** Monomorphized compile-time generics with zero runtime metadata overhead.
- **Explicit Type Conversions:** Clear taxonomy distinguishing safe conversions (`Type(x)`), lossy/narrowing conversions (`Type!(x)`), and bit reinterpretation (`@bitcast(Type, x)`).
- **Result-Based Error Handling:** Built-in discriminated union result types (`!T`, `!void`) with first-class syntactic support (`??`) and no runtime exception overhead.
- **C Interoperability:** First-class `extern "C"` support adhering strictly to the System V AMD64 ABI.

---

## Language Specification

### 1. Type System

#### Primitive Value Types

| Category | Types | Details |
|---|---|---|
| **Signed Integers** | `int8`, `int16`, `int32`, `int64`, `int128`, `int256`, `int512` | Two's-complement. $\ge 256$-bit lowered via compiler runtime helpers. |
| **Unsigned Integers** | `uint8`, `uint16`, `uint32`, `uint64`, `uint128`, `uint256`, `uint512` | Binary unsigned. |
| **Floating-Point** | `float16`, `float32`, `float64`, `float128` | IEEE 754-2008 standard formats. |
| **Boolean** | `bool8`, `bool16`, `bool32`, `bool64`, `bool128`, `bool256`, `bool512` | `0` is `false`; any non-zero value is `true`. Relational ops yield canonical `1`. |
| **Code Units** | `char8`, `char16`, `char32` | UTF-8, UTF-16, and UTF-32 code units. |
| **Strings** | `string8`, `string16`, `string32` | Value fat-pointer: `{ const charN* data; uint64 length; }`. Immutable. |
| **Unit / Void** | `void` | Return-type marker only; cannot be instantiated as a variable. |

*All indexing, lengths, and capacities use `uint64`.*

---

### 2. Literals and Constants

- **Integers**: `42`, `0xFF`, `0b1010`, `0o755`. Underscore separators are supported (`1_000_000`).
- **Floats**: `3.14`, `1.0e-9`.
- **Literal Typing**: Literals are untyped compile-time constants of arbitrary precision. They infer their type based on destination context; values exceeding target bounds cause a compilation error.
- **Characters & Strings**: `'a'`, `'\u{1F600}'`, `"standard string\n"`, and raw strings `` `C:\raw\path` ``.
- **Pointers**: `null` (assignable only to raw pointers `T*`).

---

### 3. Declarations and Mutability

Every variable must be explicitly initialized upon declaration:

```c++
int32 x = 5;
const int32 y = compute_value(); // Runtime immutable

Point p = {};                    // Initialized using default struct field values
int32[4] arr = [1, 2, 3, 4];     // Full element array initialization
int32[8] zeroed = [0...];        // Fill all 8 elements with 0
int32[] slice = arr[0..2];       // Slice reference
int32[] empty_slice = {};        // { data = null, length = 0 }

// Compile-time constants
MAX_RETRIES :: uint32(5);
PI          :: float64(3.141592653589793);
```

---

### 4. Functions

- **Syntax:** `name :: (params) -> ReturnType { body }`
- Parameters are immutable bindings by default.
- Function overloading is resolved statically via arity and parameter types. Default arguments are supported.

```c++
add :: (int32 a, int32 b) -> int32
{
  return a + b;
}

connect :: (string8 host, uint16 port = 80) -> !int32
{
  return success(1);
}
```

---

### 5. Generics

Generic functions and types are fully monomorphized at compile time:

```c++
max :: <T>(T a, T b) -> T
{
  if (a > b) then { return a; }
  else             { return b; }
}

Pair :: struct <K, V>
{
  K key;
  V value;
}

// Instantiation
Pair<string8, int32> p = { .key = "answer", .value = 42 };
int32 m = max<int32>(3, 7);
```

---

### 6. Pointers, Arrays, and Slices

- **Raw Pointer (`T*`)**: Address-of `&x`, dereference `*p`. Single-level auto-dereferencing applies for member access (`p.field` desugars to `(*p).field`). Direct pointer arithmetic on `T*` is prohibited.
- **Fixed-Size Array (`T[N]`)**: Value type stored inline/on stack with fixed compile-time size `N`.
- **Slice (`T[]`)**: Value fat-pointer representation `{ T* data; uint64 length; }`. Sub-slices use `arr[start..end]` syntax with runtime bounds checking.
- **Dynamic Array**: Heap-allocated vector available via `std::collection::array<T>`.

---

### 7. Structs, Enums, and Unions

```c++
Point :: struct
{
  float32 x = 0.0;   // Default field values required
  float32 y = 0.0;
}

Color :: enum -> uint8   // Backing integer type is mandatory
{
  red   = 1,
  green = 2,
  blue  = 3,
}

Value :: union          // Untagged low-level union
{
  int64   i;
  float32 f;
}
```

- Enum-to-integer and integer-to-enum conversions are strongly typed and require explicit casts (`uint8(Color::red)`).

---

### 8. Operators and Casting

- **Statements vs Expressions:** Assignment (`=`), compound assignments (`+=`, `-=`), and increments/decrements (`++`, `--`) are statements.
- **Cast Taxonomy:**
  - `Type(expr)`: Lossless, widening conversion.
  - `Type!(expr)`: Lossy or truncating conversion.
  - `@bitcast(Type, expr)`: Reinterprets bit pattern of equal-sized types.

---

### 9. Control Flow

#### Conditionals & Loops
```c++
// if/then/else requires `then`
if (x > 0) then {
  std::io::print("positive\n");
} else {
  std::io::print("non-positive\n");
}

// Loops
while (condition) { /* ... */ }
do { /* ... */ } while (condition);

// Multi-level break/continue
break(2);     // Break out of 2 nested loops/switches
continue;     // Continue innermost loop
```

#### Pattern Matching & Iteration
```c++
// switch (statement)
switch (c)
{
  case Color::red   { std::io::print("red\n");   }
  case Color::green { std::io::print("green\n"); }
  default           { std::io::print("other\n"); }
}

// match (exhaustive expression with '#' binding)
string8 name = match (c)
{
  # == Color::red                              { "red"   }
  # == Color::green || # == Color::light_green { "green" }
  default                                      { "other" }
};

// foreach loop
foreach (uint64 i, string8 arg in args) {
  std::io::print("{}: {}\n", i, arg);
}
```

---

### 10. Error Handling (`!T` and `!void`)

Femto enforces deterministic error handling without runtime exceptions:

- `!T` is an opaque discriminated union `{ int32 code; T value; }`.
- `!void` represents operations that return only an error code `{ int32 code; }`.
- Return values are constructed using `success(val)` / `success()` and `failure(code)` / `failure()`.

```c++
read_config_file :: (string8 filename) -> !string8
{
  // The postfix '??' operator unwraps the value or immediately returns failure
  string8 data = std::io::read(filename)??;
  return success(data);
}

main :: (string8[] args) -> int32
{
  // Result branching construct
  read_config_file("app.cfg")
    ?? (string8 cfg)      { std::io::print("Loaded: {}\n", cfg); }
    :  (int32 error_code) { std::io::print("Error: {}\n", error_code); return 1; };

  return 0;
}
```

---

### 11. Modules, Visibility, and C Interop

- Modules are mapped one-to-one per source file.
- Declarations are private to the module by default; public symbols are marked with `#export`.
- Namespaces and imports:
  ```c++
  import std::io;
  import std::collection as col;
  ```
- C ABI linkage:
  ```c++
  extern "C"
  {
    read  :: (int32 fd, uint8* buf, uint64 count) -> int64;
    write :: (int32 fd, const uint8* buf, uint64 count) -> int64;
  }
  ```

---

### 12. Compile-Time Metaprogramming

- Conditional compilation: `#if (cond) { ... } #else { ... }`
- Built-in reflection intrinsics:
  - `@target`, `@arch`, `@endian`
  - `@file`, `@line`
  - `@sizeof(T)`, `@alignof(T)`, `@typeof(expr)`
  - `@bitcast(TargetType, expr)`

---

## ABI and Memory Layout

Femto conforms to the System V AMD64 ABI on x86-64 Linux:

| Femto Type | In-Memory Layout | ABI Register Passing |
|---|---|---|
| `int8`..`int64`, `uint8`..`uint64`, `bool8`..`bool64`, `charN` | 1, 2, 4, 8 bytes sign/zero extended | `INTEGER` class (`RAX`, `RDI`, etc.) |
| `int128`, `uint128`, `bool128` | 16 bytes (low 64-bit, high 64-bit) | `INTEGER` pair (`RDI:RSI` or `RAX:RDX`) |
| `int256`, `int512`, `uint256`, `uint512` | 32 / 64 bytes inline buffer | Passed by hidden reference or stack |
| `float16`, `float32`, `float64` | Standard IEEE 754 representations | `SSE` class (`XMM0`–`XMM7`) |
| `float128` | 16 bytes IEEE 754 `binary128` | `X87` / `SSE` pair |
| Raw Pointer `T*` | 8 bytes address | `INTEGER` |
| Slice `T[]` / `stringN` | `{ T* data, uint64 length }` (16 bytes) | Passed in two `INTEGER` registers |
| Result `!T` | `{ int32 code, [pad], T value }` | In registers if $\le 16$ bytes, else via hidden pointer |
| Result `!void` | `{ int32 code }` (4 bytes) | Single `INTEGER` register |

---

## Compiler Architecture

The reference compiler is implemented as a pipeline written in ISO C++26:

```
Source Code (.femt)
       │
       ▼
   [ Lexer ]  ────────► Token Stream
       │
       ▼
   [ Parser ] ────────► Abstract Syntax Tree (AST)
       │
       ▼
[ Semantic Analyzer ] ─► Symbol Resolution, Monomorphization, Type Checking
       │
       ▼
  [ HIR Lowering ] ───► High-Level IR (Desugars match, foreach, ??, !T)
       │
       ▼
  [ LIR Lowering ] ───► Low-Level IR (SSA Form, Control Flow Graph)
       │
       ▼
 [ RegAlloc & Opt ] ──► Register Allocation (Linear Scan / Graph Coloring)
       │
       ▼
[ CodeGen: NASM ] ───► x86-64 Intel Syntax Assembly (.asm)
       │
       ▼
  [ nasm + ld ]  ─────► Native ELF-64 Executable
```

---

## Prerequisites and Toolchain

To build the compiler and execute compiled programs, the following tools are required:

- **C++ Compiler:** Modern C++ compiler supporting ISO C++26 (GCC 14+ or Clang 18+)
- **Assembler:** [NASM](https://www.nasm.us/) ($\ge 2.15$)
- **Linker / C Runtime:** GNU Linker (`ld`) and standard `libc` (`gcc` driver recommended)

---

## Quick Start Example

1. **Write a program (`hello.femt`):**
   ```c++
   import std::io;

   #export
   main :: (string8[] args) -> int32
   {
     std::io::print("Hello, Femto!\n");
     return 0;
   }
   ```

2. **Compile to native binary:**
   ```bash
   femtoc hello.femt -o hello
   ./hello
   ```

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.