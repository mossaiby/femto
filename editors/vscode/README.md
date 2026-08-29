# Femto Language Support & Language Server Protocol (LSP) for VS Code

Official Visual Studio Code extension providing syntax highlighting, Language Server Protocol (LSP) integration, real-time compiler error diagnostics, type hover inspection, and document symbol outlines for the Femto programming language.

---

## Table of Contents

- [Features](#features)
- [Prerequisites](#prerequisites)
- [Method 1: Local Development Symlink (Fastest)](#method-1-local-development-symlink-fastest)
- [Method 2: Building and Packaging a `.vsix` File](#method-2-building-and-packaging-a-vsix-file)
- [Installing the `.vsix` Package](#installing-the-vsix-package)
- [Configuring the Compiler / LSP Binary Path](#configuring-the-compiler--lsp-binary-path)
- [Troubleshooting](#troubleshooting)

---

## Features

- **Language Server Protocol (`femtoc --lsp`):**
  - **Live Red Squiggly Diagnostics:** Real-time semantic and syntax validation as you type or save.
  - **Type Hover Inspection:** Hovering your mouse over functions, structs, or constants reveals their full type signatures in Markdown code blocks.
  - **Document Symbol Outline:** Powers the VS Code Outline sidebar and breadcrumbs with functions, structs, unions, enums, and constants.
- **Rich Syntax Highlighting:**
  - Fixed-width numeric primitives (`int8`–`int512`, `uint8`–`uint512`, `float16`–`float128`, `boolN`, `charN`, `string8`–`string32`, `any`, `void`).
  - Result error handling types (`!T`, `!void`), `success()`, `failure()`, and `??` postfix/branch operators.
  - Native variadic slices (`any... args`) and C-FFI variadics (`...`).
  - Pattern matching expressions (`match`) with subject binding (`#`).
  - Unified `->` arrow operator rendering without bracket-pair interference.
- **Smart Editing & Language Configuration:**
  - Automatic indentation after `then`, `do`, `{`, and control blocks.
  - Auto-closing pairs for quotes, braces (`{}` `[]` `()`).
  - Line (`//`) and block (`/* */`) comment toggling (`Ctrl+/` / `Cmd+/`).

---

## Prerequisites

1. **Node.js & npm:** Node.js ($\ge 18.0$) and npm installed on your system.
2. **Femto Compiler Binary (`femtoc`):** The compiler must be built with LSP support:
   ```bash
   cd /path/to/femto
   mkdir -p build && cd build
   cmake ..
   cmake --build .
   ```

---

## Method 1: Local Development Symlink (Fastest)

This method directly links the extension folder to your VS Code extensions directory so you can edit the extension and see changes immediately without rebuilding a package file.

### 1. Install extension dependencies:
```bash
cd editors/vscode
npm install
```

### 2. Symlink the folder into VS Code:

#### On Linux & macOS:
```bash
ln -s "$(pwd)" ~/.vscode/extensions/femto-vscode
```

#### On Windows (PowerShell):
```powershell
New-Item -ItemType SymbolicLink -Path "$HOME\.vscode\extensions\femto-vscode" -Target "$PWD"
```

### 3. Reload VS Code:
Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on macOS), type **Developer: Reload Window**, and press Enter.

---

## Method 2: Building and Packaging a `.vsix` File

To distribute or install the extension as a standalone `.vsix` package:

### 1. Navigate to the extension folder and install dependencies:
```bash
cd editors/vscode
npm install
```

### 2. Package the extension with `vsce`:
You can package directly using `npx`:
```bash
npx @vscode/vsce package
```
*(Alternatively, install `vsce` globally via `npm install -g @vscode/vsce` and run `vsce package`)*

This produces a file named **`femto-vscode-0.2.0.vsix`** in `editors/vscode`.

---

## Installing the `.vsix` Package

### Option A: Using the VS Code Command Line (CLI)
```bash
code --install-extension femto-vscode-0.2.0.vsix
```

### Option B: Using the VS Code User Interface (GUI)
1. Open Visual Studio Code.
2. Open the **Extensions View** (`Ctrl+Shift+X` / `Cmd+Shift+X`).
3. Click the **`...` (Views and More Actions)** menu at the top-right corner of the Extensions panel.
4. Select **Install from VSIX...**
5. Select the `femto-vscode-0.2.0.vsix` file.

---

## Configuring the Compiler / LSP Binary Path

The extension connects to `femtoc --lsp` to provide language services. By default, it looks for `femtoc` in your system `PATH`.

If `femtoc` is built in a local directory (e.g. `/home/user/femto/build/femtoc`), you can configure its path in VS Code:

1. Open **Settings** (`Ctrl+,` / `Cmd+,`).
2. Search for `femto.compilerPath`.
3. Set the absolute path to your `femtoc` binary:
   ```
   /home/user/femto/build/femtoc
   ```

Alternatively, add this to your `.vscode/settings.json` in your project workspace:
```json
{
  "femto.compilerPath": "/home/user/femto/build/femtoc",
  "femto.trace.server": "verbose"
}
```

---

## Troubleshooting

### Error: "Failed to start Femto Language Server from 'femtoc'"
- Verify that `femtoc` is built: check that `build/femtoc` exists and is executable (`chmod +x build/femtoc`).
- Set `femto.compilerPath` in VS Code Settings to the absolute path of `femtoc`.
- Verify LSP mode manually in your terminal by running:
  ```bash
  /path/to/build/femtoc --lsp
  ```
  *(Press `Ctrl+C` to exit)*.

### Red squiggles not updating?
- Ensure the file is saved with the `.femto` extension.
- Check the output log in VS Code: open **View ➔ Output** and select **Femto Language Server** from the dropdown menu.