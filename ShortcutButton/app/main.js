const { app, BrowserWindow, dialog } = require('electron');
const path = require('path');

const initializedSessions = new WeakSet();

function installSessionHandlers(session) {
  if (initializedSessions.has(session)) return;
  initializedSessions.add(session);

  // select-serial-port can fire multiple times with the same callback while
  // device lists update; this prevents duplicate dialogs/callback calls.
  const handledSerialCallbacks = new WeakSet();
  let pickerOpen = false;

  session.on('select-serial-port', (event, portList, webContents, callback) => {
    event.preventDefault();
    if (handledSerialCallbacks.has(callback)) return;
    if (pickerOpen) return;
    if (portList.length === 0) return;

    handledSerialCallbacks.add(callback);
    pickerOpen = true;

    const labels = portList.map(p => p.displayName || p.portName || p.portId);
    const buttons = [...labels, 'Cancel'];
    const ownerWindow = BrowserWindow.fromWebContents(webContents);
    const options = {
      type: 'question',
      title: 'Select Serial Port',
      message: 'Connect to ShortcutButton',
      detail: 'Choose the serial port for your device:',
      buttons,
      defaultId: 0,
      cancelId: buttons.length - 1,
    };
    const dialogPromise = ownerWindow
      ? dialog.showMessageBox(ownerWindow, options)
      : dialog.showMessageBox(options);

    dialogPromise.then(({ response }) => {
      callback(response < portList.length ? portList[response].portId : '');
    }).finally(() => {
      pickerOpen = false;
    });
  });

  session.setPermissionCheckHandler((webContents, permission) => {
    if (permission === 'serial') return true;
    return false;
  });

  session.setDevicePermissionHandler((details) => {
    if (details.deviceType === 'serial') return true;
    return false;
  });
}

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

  installSessionHandlers(win.webContents.session);

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
