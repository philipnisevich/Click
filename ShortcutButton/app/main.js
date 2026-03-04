const { app, BrowserWindow, dialog } = require('electron');
const path = require('path');

function createWindow() {
  const win = new BrowserWindow({
    width: 960,
    height: 820,
    minWidth: 800,
    minHeight: 600,
    title: 'ShortcutButton',
    webPreferences: {
      // Web Serial requires a secure context. Electron's file:// pages are
      // treated as secure, but we also need to enable the serial permission.
      nodeIntegration: false,
      contextIsolation: true,
    },
  });

  // Show a native port-picker dialog when the web UI calls requestPort().
  // select-serial-port fires multiple times with the same callback object as
  // the port list changes, so we guard with a WeakSet to call it only once.
  const handledSerialCallbacks = new WeakSet();
  let pickerOpen = false;

  win.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
    event.preventDefault();
    if (handledSerialCallbacks.has(callback)) return;
    if (pickerOpen) return;
    if (portList.length === 0) return; // Re-fires when a port appears.

    handledSerialCallbacks.add(callback);
    pickerOpen = true;

    const labels = portList.map(p => p.displayName || p.portName || p.portId);
    const buttons = [...labels, 'Cancel'];

    dialog.showMessageBox(win, {
      type: 'question',
      title: 'Select Serial Port',
      message: 'Connect to ShortcutButton',
      detail: 'Choose the serial port for your device:',
      buttons,
      defaultId: 0,
      cancelId: buttons.length - 1,
    }).then(({ response }) => {
      pickerOpen = false;
      callback(response < portList.length ? portList[response].portId : '');
    });
  });

  win.webContents.session.setPermissionCheckHandler((webContents, permission) => {
    if (permission === 'serial') return true;
    return false;
  });

  win.webContents.session.setDevicePermissionHandler((details) => {
    if (details.deviceType === 'serial') return true;
    return false;
  });

  win.loadFile(path.join(__dirname, 'web', 'index.html'));
  win.setMenuBarVisibility(false);
}

app.whenReady().then(() => {
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
