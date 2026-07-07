# VSCode support for femto

To package this extenstion:

1. Install vsce if you don't have it:
```shell
npm install -g @vscode/vsce
```

2. Package from the extension directory:
```shell
cd vscode-femto
vsce package                              # This produces femto-0.1.0.vsix
code --install-extension femto-0.1.0.vsix # Install into VSCode
```
A pre-packaged version can be found in the `dist` folder.