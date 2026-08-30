const path = require('path');
const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

function activate(context) {
    const config = vscode.workspace.getConfiguration('femto');
    const serverCommand = config.get('compilerPath') || 'femtoc';

    const serverOptions = {
        command: serverCommand,
        args: ['--lsp'],
        transport: TransportKind.stdio
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'femto' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.femto')
        }
    };

    client = new LanguageClient(
        'femtoLanguageServer',
        'Femto Language Server',
        serverOptions,
        clientOptions
    );

    client.start().catch((err) => {
        vscode.window.showErrorMessage(
            `Failed to start Femto Language Server from "${serverCommand}". Make sure femtoc is built and in your PATH, or configure "femto.compilerPath".`
        );
    });
}

function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

module.exports = {
    activate,
    deactivate
};