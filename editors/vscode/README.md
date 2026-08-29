# Femto Language Support for VS Code

Official Visual Studio Code extension providing rich syntax highlighting, bracket matching, indentation rules, and language configurations for the Femto programming language.

---

## Features

- **Semantic Syntax Highlighting:**
  - Sized integer, float, boolean, unicode, and string primitives (`int8`–`int512`, `float16`–`float128`, `string8`–`string32`).
  - Result error handling types (`!T`, `!void`), `success()`, `failure()`, and `??` postfix/branch operators.
  - Pattern matching expressions (`match`) with subject binding (`#`).
  - Generic types, functions, and structs with `>>` bracket awareness.
  - Compile-time builtins (`@sizeof`, `@alignof`, `@cast`, `@bitcast`) and directives (`#export`, `#if`, `#else`).
  - Hex (`0x`), binary (`0b`), octal (`0o`), and float literals with underscore separators (`1_000_000`).
  - Nested block comments (`/* /* ... */ */`) and raw strings (`` `...` ``).
- **Smart Editing & Language Configuration:**
  - Automatic indentation after `then`, `do`, `{`, and control blocks.
  - Auto-closing pairs for quotes, brackets (`{}` `[]` `()`), and generic angle brackets (`<>`).
  - Line (`//`) and block (`/* */`) comment toggling.

---

## Installation

### Method 1: Local Link (Recommended for Development)

Symlink or copy the extension folder into your VS Code extensions directory:

#### Linux & macOS:
```bash
ln -s "$(pwd)/editors/vscode" ~/.vscode/extensions/femto-vscode
```

#### Windows (PowerShell):
```powershell
New-Item -ItemType SymbolicLink -Path "$HOME\.vscode\extensions\femto-vscode" -Target "$PWD\editors\vscode"
```

Restart VS Code or reload the window (`Ctrl+Shift+P` / `Cmd+Shift+P` ➔ **Developer: Reload Window**). All `.femto` and `.femt` files will now be automatically recognized with syntax highlighting!

---

### Method 2: Package as a `.vsix`

Install the Visual Studio Code Extension Manager CLI and package the extension:

```bash
npm install -g @vscode/vsce
cd editors/vscode
vsce package
```

Install the resulting `femto-vscode-0.1.0.vsix` file into VS Code:
```bash
code --install-extension femto-vscode-0.1.0.vsix
```