# Femto Compiler — Status Report

## Overview

femto is a C-like language compiler targeting x86-64 Linux, implemented in C++26. The pipeline is:
Lexer → Parser → Semantic Analysis → AST → HIR → HIR Optimizer → LIR → LIR Optimizer → x86-64 Codegen (NASM)

## Test Results

### Unit Tests: 82/82 PASSED
All Lexer, Parser, and Sema unit tests pass.

### Integration Tests (11 example .femto files):

| Example | Status | Notes |
|---------|--------|-------|
| hello_world | **PASS** | Full pipeline OK |
| factorial | **PASS** | Full pipeline OK |
| gcd | **PASS** | Full pipeline OK |
| patterns | **PASS** | Full pipeline OK |
| primes | **PASS** | Full pipeline OK |
| bubble_sort | **PASS** | Full pipeline OK |
| fizzbuzz | **SEGFAULT** | Pre-existing `else if` parsing bug |
| fibonacci | **SEGFAULT** | Pre-existing `else if` parsing bug |
| error_handling | **SEGFAULT** | Pre-existing `else if` parsing bug |
| generics | **SEGFAULT** | Pre-existing `else if` parsing bug |
| structs_enums | **SEGFAULT** | Pre-existing `else if` parsing bug |

### Summary: 6 pass, 5 segfault (all due to same root cause)

## Fixes Applied This Session

### 1. Parser Bug A — `::` after type tokens (FIXED)
- **Problem**: `std::io::print(std::string8::from_int(5))` — `::` handler called `parse_name()` which only accepts `Identifier`. Type tokens like `string8` (tokenized as `TY_string8`) weren't consumed, causing infinite loops in argument parsing.
- **Fix**: `.` and `::` handlers now check `is_builtin_type_token()` as fallback before `parse_name()`.
- **File**: `src/parser/parser.cpp` lines 764-777

### 2. Parser Bug B — `<` comparison vs generic syntax (FIXED)
- **Problem**: `while (i < 10)` — `<` after an Identifier triggered generic type parsing. `parse_type()` on `10` failed without advancing, causing infinite loops in the generic parsing while loop. This crashed 5 example tests (bubble_sort, generics, patterns, primes, structs_enums).
- **Fix**: Added speculative lookahead in `parse_postfix()`. Before entering generic parsing, the code scans forward from `<` looking for a matching `>` at depth 0. If it encounters a comparison operator, semicolon, brace, `)`, `,`, `=`, `==`, `!=`, `<=`, `>=` — it breaks early and treats `<` as comparison. Only if `>` is found at depth 0 does it enter generic mode.
- **File**: `src/parser/parser.cpp` lines 807-849

### 3. Arrow operator `->` (FIXED)
- **Problem**: `p->x` pointer member access syntax not supported.
- **Fix**: Added `match(TokenType::Arrow)` handler in `parse_postfix()` that desugars `ptr->field` to `(*ptr).field` using `Deref` unary + `MemberExpr`.
- **File**: `src/parser/parser.cpp` lines 778-783

### 4. `??` branch form (FIXED)
- **Problem**: Only bare `??` propagation was supported. Branch form `expr ?? (type name) { body } : (type name) { body }` was not parsed.
- **Fix**: Added branch arm parsing in `parse_postfix()` `??` handler and implemented `parse_branch_arm()` helper.
- **File**: `src/parser/parser.cpp` lines 785-802, 854-864

### 5. `synchronize()` error recovery (FIXED)
- **Problem**: `synchronize()` stopped at `}` without advancing, causing the top-level parse loop to get stuck re-processing the same `}` forever.
- **Fix**: Now advances past `}` before returning.
- **File**: `src/parser/parser.cpp` lines 49-55

### 6. `parse_basic_type()` infinite loop guard (FIXED)
- **Problem**: Generic type argument parsing loop `while (!Gt) { parse_type(); }` could infinite-loop if `parse_type()` failed without advancing.
- **Fix**: Added `pos_` check — if position didn't advance after `parse_type()`, force an `advance()`.
- **File**: `src/parser/parser.cpp` lines 444-449

### 7. Type checker — forward references (FIXED)
- **Problem**: Functions called before their declaration (e.g. `safe_compute()` defined after `main()` that calls it) failed with "undefined function".
- **Fix**: Two-pass type checking — first pass registers all function/struct/enum signatures, second pass checks bodies.
- **File**: `src/sema/type_checker.cpp` lines 46-113

### 8. Type checker — member function call return type (FIXED)
- **Problem**: `check_call_expr()` returned `void` for all non-`Identifier` callees (e.g. `std.io.print()`).
- **Fix**: Added `MemberExpr` callee resolution that builds a qualified name and looks up the function symbol.
- **File**: `src/sema/type_checker.cpp` lines 506-557

### 9. Type checker — `??` propagation type (FIXED)
- **Problem**: `expr??` (bare propagation) always returned `void`.
- **Fix**: When branches are null (propagation form), extracts the success type from the inner error-return type.
- **File**: `src/sema/type_checker.cpp` lines 413-424

## Remaining Issues

### Critical: `else if` causes segfault in parser
- **Symptom**: Any file using `else if` chains segfaults during parsing (stack overflow).
- **Root cause**: `parse_if_stmt()` wraps `else if` in a `Block` containing the elif, then recursively calls itself. With deep nesting or specific patterns, this overflows the stack.
- **Pre-existing**: Confirmed this crashes the original code before any changes.
- **Impact**: Blocks 5 example tests (fizzbuzz, fibonacci, error_handling, generics, structs_enums).
- **Fix needed**: Convert recursive `else if` to iterative loop in `parse_if_stmt()`.

### Medium: Struct initializer `{ .field = val }` not supported in expressions
- **Symptom**: `Point origin = { .x = 0.0, .y = 0.0 }` hangs.
- **Root cause**: `parse_primary()` has no handler for `{` tokens. The `{` would need to be recognized as a struct literal in expression context.
- **Impact**: Blocks structs_enums test (beyond the else-if issue).

### Medium: User-defined type variable declarations
- **Symptom**: `is_type_start()` only recognizes builtin types. `Point origin = ...` isn't parsed as a variable declaration.
- **Root cause**: The parser needs to detect `Identifier Identifier` patterns as type+name for user-defined types.
- **Impact**: Same as struct initializer issue.

### Low: Generic type parameters unknown to sema
- **Symptom**: `T` in `max :: <T>(T a, T b)` produces "unknown type 'T'".
- **Root cause**: Generic type parameters aren't registered in the symbol table.
- **Impact**: generics test would still fail type checking even if parsing worked.

### Low: Array method `.length()` not recognized
- **Symptom**: `arr.length()` returns `void` instead of the array length type.
- **Root cause**: No built-in methods on array types in the type system.
- **Impact**: bubble_sort sema errors (but test still passes because the errors don't block HIR lowering).

## Files Modified (uncommitted)

- `src/parser/parser.cpp` — 102 lines changed (6 parser fixes)
- `src/parser/parser.h` — 3 lines added (parse_branch_arm declaration)
- `src/sema/type_checker.cpp` — 99 lines added (forward refs, member calls, ?? propagation)

## Architecture Notes

- All strings interned via StringPool; tokens store pool indices
- AST uses `std::variant` with `std::unique_ptr` child nodes
- Monomorphization at HIR stage after type checking
- Codegen targets NASM x86-64 Intel syntax → nasm -f elf64 → ld
- Build: CMake with GCC 16.0.1 / Clang 22.1.2, C++26
- Tests: GoogleTest via FetchContent
