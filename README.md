# femto - A C-like language for the future

## Basic types
- signed integers:   `int8`, `int16`, `int32`, `int64`, `int128`, `int256`, `int512`
- unsigned integers: `uint8`, `uint16`, `uint32`, `uint64`, `uint128`, `uint256`, `uint512`
- floating point:    `float16`, `float32`, `float64`, `float128`
- boolean: `bool8`, `bool16`, `bool32`, `bool64`, `bool128`, `bool256`, `bool512`
- character:         `char8`, `char16`, `char32`
- string:            `string8`, `string16`, `string32`
- `void` for functions returning nothing; not a value type

Notes:
- `charN` is an N-bit code unit; `stringN` is an immutable sequence of `charN`
  (`string8` = UTF-8, `string16` = UTF-16, `string32` = UTF-32)
- `boolN` occupies N bits; only `0` is false, any other bit pattern is true
- there are no platform-dependent types (no `int`, no `size_t`)

## Literals
- integer: `42`, `0xFF`, `0b1010`, `0o755`; digit separator `_`: `1_000_000`
- float:   `3.14`, `1.0e-9`
- no type suffixes; literals are untyped constants that adopt the exact type
  required by context (compile-time error if the value does not fit)
- where context cannot determine the type (e.g., ambiguous overloads, generic
  inference), pin it with a cast: `float32(1.5)`, `int8(42)` — on untyped
  constants this is evaluated at compile time and value-checked, zero cost
- char:    `'a'`, `'\n'`, `'\u{1F600}'`
- string:  `"hello"`, raw string `` `C:\path\{not interpolated}` ``
- bool:    `true`, `false`
- `null`:  only assignable to pointer types

## Comments
- C/C++ style comments `//` and `/* ... */`
- `/* ... */` comments nest

## Keywords
- `import`, `for`, `while`, `do`, `if`, `then`, `else`, `switch`, `case`,
  `match`, `return`, `break`, `continue`, `enum`, `struct`, `null`, `extern`,
  `foreach`, `array`, `namespace`, `success`, `failure`, `true`, `false`,
  `void`, `in`, `default`, `const`, `union`
- reserved for future use (may not be used as identifiers): `class`, `interface`, `defer`

## Declarations
- variables are declared C-style: `int32 x = 5;`
- every variable must be initialized at declaration; `int32 x;` is a
  compile-time error
- compile-time constants use `::` (consistent with function declarations):
```c++
MAX_RETRIES :: uint32(5);
PI          :: float64(3.141592653589793);
```
- `const` qualifies runtime immutability: `const int32 x = compute();`

## Functions
- declared with `name :: (params) -> return_type { ... }`
- overloading is allowed; overloads must differ in arity or parameter types
- default arguments are allowed and must be trailing:
  `connect :: (string8 host, uint16 port = 80) -> !int32 { ... }`
- a call must never be ambiguous between overloads and default arguments;
  ambiguity is a compile-time error at the call site
- variadics are not currently supported
```c++
add :: (int32 a, int32 b) -> int32
{
  return a + b;
}
```
- parameters are immutable by default; pass a pointer to mutate the caller's value

## Generics
- simple, C#-like syntax on functions and structs; monomorphized at compile time
  (zero runtime cost, no boxing)
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

Pair<string8, int32> p = { .key = "answer", .value = 42 };
int32 n = max<int32>(3, 7);
```
- generic overloads participate in normal overload resolution;
  a non-generic exact match wins over a generic one

## Pointers and arrays
- pointer type: `T*`; address-of `&x`; dereference `*p`; member access `p.field`
  (no `->`, the compiler auto-dereferences on `.`)
- no pointer arithmetic on raw pointers; use slices
- fixed-size array: `int32[8] a;` — size is part of the type, stack-allocated
- slice: `int32[] s;` — pointer + length, bounds-checked, `s.length()`
- dynamic array: `std::collection::array<int32> v;` — heap-allocated, growable,
  `v.push(x)`, `v.pop()`, `v.length()`, `v.capacity()`
  (`array<T>` is an ordinary generic struct in `std::collection`, not compiler magic)

## Structs, enums, unions
```c++
Point :: struct
{
  float32 x = 0.0;   // default field values required
  float32 y = 0.0;
}

Color :: enum -> uint8   // backing type mandatory
{
  red   = 1,
  green,
  blue,
}

Value :: union          // untagged
{
  int64   i;
  float32 f;
}
```
- struct literal: `Point p = { .x = 1.0, .y = 2.0 };`
- no methods in v1 (`class`/`interface` reserved for later); free functions
  operate on structs
- enums are strongly typed; no implicit conversion to/from integers,
  use `uint8(Color::red)` explicitly

## Operators and precedences
- like C/C++
- `++` and `--` are statements, not expressions
- no comma operator
- assignment is a statement, not an expression (`if (a = b)` is impossible)
- explicit conversion is a call-style cast: `int64(x)`, `float32(i)`;
  it must be value-preserving or the compiler requires `int8!(x)` /
  `@bitcast(uint32, f)`
- `??` is the result-branch operator (see error handling)

## Control flow

### `if` / `then` / `else`
`then` is always required between the condition and the body block:
```c++
if (x > 0) then
{
  std::io::print("positive\n");
}
else
{
  std::io::print("non-positive\n");
}
```

### `while` and `do` / `while`
```c++
while (condition)
{
  // ...
}

do
{
  // body executes at least once
} while (condition);
```

### `switch`
`switch` has no fallthrough; every `case` is a block; `default` is required
unless cases are exhaustive:
```c++
switch (c)
{
  case Color::red   { std::io::print("red\n");   }
  case Color::green { std::io::print("green\n"); }
  default           { std::io::print("other\n"); }
}
```

### `match`
`match` is expression-oriented pattern matching and must be exhaustive.
Inside a `match` block, `#` refers to the value being matched (the subject
expression is evaluated exactly once and bound to `#` for the duration of
the match):
```c++
string8 name = match (c)
{
  # == Color::red                              { "red"   }
  # == Color::green || # == Color::light_green { "green" }
  default                                      { "other" }
};
```
- each arm is a boolean expression over `#` followed by a block whose
  last (and typically only) expression is the arm's value
- `default` matches any value not covered by earlier arms
- the type of every arm's value must be identical; the compiler enforces this

### `foreach`
`foreach` iterates over any array, slice, or string.
When iterating a `stringN`, each element has type `charN`
(`string8` → `char8`, `string16` → `char16`, `string32` → `char32`):
```c++
foreach (string8 arg in args)            { ... }   // array / slice
foreach (uint32 i, string8 arg in args)  { ... }   // with index

foreach (char8 c in "hello")             { ... }   // string8 → char8 elements
foreach (uint32 i, char8 c in "hello")   { ... }   // with index
```

### `break` and `continue`
`break` and `continue` each take an optional positive integer literal
specifying how many enclosing loop or `switch` levels to exit/skip.
Omitting the argument is equivalent to passing `1`:
```c++
break;      // exit 1 level
break(3);   // exit 3 levels
continue;   // skip to next iteration of innermost loop
continue(2);// skip to next iteration of 2 levels up
```

## Error handling
- no exceptions; a return type prepended with `!` wraps the value in a
  result structure: `!T` is conceptually `struct { int32 code; T value; }`
- construct with `success(value)` / `failure(code)`:
  - `success(value)` sets `code` to `0` and stores `value`
  - `failure(code)` sets `code` to the given value; `failure()` with no
    argument sets `code` to `1`
  - when a `failure` result is produced, the `value` field does not exist
    and is inaccessible; any attempt to read it is a compile-time error
- consume with the `??` branch operator: `expr ?? on_success : on_failure`,
  where `on_success` may bind the unwrapped value and `on_failure` binds `code`
- `expr??` alone propagates: if `expr` is a failure, the current function
  returns `failure(code)` immediately (function must itself return `!T`)
```c++
read_config_file :: (string8 filename) -> !string8
{
  string8 data = std::io::read(filename)??;   // propagate on failure
  return success(data);
}

main :: (string8[] args) -> int32
{
  read_config_file("app.cfg")
    ?? (string8 cfg)      { std::io::print("loaded: {}\n", cfg); }
    :  (int32 error_code) { std::io::print("failed; code: {}\n", error_code); return 1; };
  return 0;
}
```

## Memory management
- manual, explicit: `std::mem::alloc<T>(count)` returns `!T[]`,
  `std::mem::free(slice)`
- no ownership/borrowing semantics; the programmer is responsible for
  lifetimes, as in C
- no hidden allocations in the language core; only `array<T>` and the
  `std::stringN` building functions allocate, and they accept an optional
  allocator argument
- `defer` is reserved for scope-exit cleanup in a future version

### String building functions
Because `stringN` values are immutable, any operation that produces a new
string must allocate. These operations live in `std::string8`,
`std::string16`, and `std::string32` (one namespace per encoding) and
always return `!stringN` to surface allocation failures:

| Function | Description |
|---|---|
| `std::string8::concat(string8 a, string8 b) -> !string8` | Concatenate two strings |
| `std::string8::repeat(string8 s, uint32 n) -> !string8` | Repeat `s` exactly `n` times |
| `std::string8::slice(string8 s, uint32 start, uint32 end) -> !string8` | Copy a sub-range (byte indices, exclusive end) |
| `std::string8::from_chars(char8[] buf) -> !string8` | Build from a char slice |
| `std::string8::from_int(int64 n) -> !string8` | Decimal representation of an integer |
| `std::string8::from_float(float64 f) -> !string8` | Decimal representation of a float |

`string16` and `string32` expose the same interface with their
respective `char16`/`char32` types. All functions accept an optional
`std::mem::Allocator*` as a final parameter; omitting it uses the
default heap allocator.

## Namespaces and modules
- `namespace name { ... }`; access with `::`; namespaces may nest and reopen
- one module per file; `import std::io;` makes `std::io::...` available
- `import std::io as io;` aliases; no wildcard imports
- everything is module-private by default; place `#export` on the line
  immediately preceding a declaration to make it visible to importers:
```c++
#export
add :: (int32 a, int32 b) -> int32
{
  return a + b;
}

#export
Point :: struct
{
  float32 x = 0.0;
  float32 y = 0.0;
}

#export
MAX_RETRIES :: uint32(5);
```
- `#export` may appear on functions, structs, enums, unions, and
  compile-time constants; it may not appear on local variables
- namespaces themselves are not exported; exporting a name makes it
  reachable under the full `namespace::name` path from the importing module

## Compile-time programming
- `#if` / `#else` compile-time conditionals; branches are blocks and
  discarded branches must still parse:
```c++
#if (@target == "linux" && ENABLE_FEATURE)
{
  // linux-specific path
}
#else
{
  // fallback
}
```
- the condition of `#if` is an arbitrary compile-time boolean expression;
  operands may be `@`-builtins, compile-time constants declared with `::`,
  and literals; boolean operators `&&`, `||`, and `!` may be used freely
- `#export` (see Namespaces and modules)
- `@`-builtins (compile-time known):
  `@target` (`"windows"`, `"linux"`, `"macos"`, ...), `@arch`, `@endian`,
  `@file`, `@line`, `@sizeof(T)`, `@alignof(T)`, `@typeof(expr)`, `@bitcast(T, x)`

## Interop
- `extern "C" name :: (int32 fd, uint8[] buf) -> int64;` declares a foreign
  function with C ABI; `extern` blocks group declarations
- `extern "C"` functions cannot be generic or overloaded (no name mangling)
- femto structs are C-layout-compatible unless the compiler is told otherwise

## Rules
- no automatic type widening/narrowing/signedness change
- `if` conditions always require `then` before the body block
- `if`/`then`, `else`, `match`, `switch` cases always expect a block `{ }`
- `break` and `continue` take an optional positive integer argument: `break(3);`
- no exceptions; use `!T` results (see error handling)
- no uninitialized reads: definite-assignment enforced by the compiler
- the `value` field of a `failure` result is inaccessible at compile time
- integer overflow on signed and unsigned types traps in debug builds and is
  two's-complement wrapping in release builds
- evaluation order of function arguments is strictly left-to-right
- `main :: (string8[] args) -> int32` is the entry point; return value is the process exit code

## Pipeline
```
Lexer -> Parser -> Semantic Analysis -> AST -> HIR -> HIR Optimizer -> LIR -> LIR Optimizer -> Code Generator (Assembler for the given target)
```
The produced Assembler must be assembled and linked with `libc` to create the
final executable. The compiler does this automatically.

## Implementation
- Use C++26
- Only `std` dependency
- Multi-code generator; x86-64 Linux for now (NASM compatible; Intel syntax)
