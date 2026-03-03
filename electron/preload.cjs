const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('electronAPI', {
  isElectron: true,
  platform: process.platform,
  selectFolder: () => ipcRenderer.invoke('dialog:selectFolder'),
  scanMusicFolder: (folderPath) => ipcRenderer.invoke('music:scanFolder', folderPath),
  loadLibrary: () => ipcRenderer.invoke('library:load'),
  saveLibrary: (data) => ipcRenderer.invoke('library:save', data),
  clearLibrary: () => ipcRenderer.invoke('library:clear'),
  loadPlaybackState: () => ipcRenderer.invoke('playback:load'),
  savePlaybackState: (data) => ipcRenderer.invoke('playback:save', data),
  pushPlaybackTime: (time) => ipcRenderer.send('playback:pushTime', time),
  loadPlaylists: () => ipcRenderer.invoke('playlists:load'),
  savePlaylists: (data) => ipcRenderer.invoke('playlists:save', data),
  exportPlaylist: (playlist, tracks) => ipcRenderer.invoke('playlists:export', playlist, tracks),
  importPlaylist: () => ipcRenderer.invoke('playlists:import'),
  exportMaskOverrides: (jsonData) => ipcRenderer.invoke('maskOverrides:export', jsonData),
  importMaskOverrides: () => ipcRenderer.invoke('maskOverrides:import'),
  onScanProgress: (callback) => {
    const handler = (_event, data) => callback(data)
    ipcRenderer.on('music:scanProgress', handler)
    return () => ipcRenderer.removeListener('music:scanProgress', handler)
  },
  startWatching: (folders) => ipcRenderer.invoke('watcher:start', folders),
  stopWatching: () => ipcRenderer.invoke('watcher:stop'),
  onWatcherEvent: (callback) => {
    const handler = (_event, data) => callback(data)
    ipcRenderer.on('watcher:event', handler)
    return () => ipcRenderer.removeListener('watcher:event', handler)
  },
  scanSingleFile: (filePath) => ipcRenderer.invoke('music:scanFile', filePath),
  windowMinimize: () => ipcRenderer.invoke('window:minimize'),
  windowMaximize: () => ipcRenderer.invoke('window:maximize'),
  windowClose: () => ipcRenderer.invoke('window:close'),
  windowToggleFullscreen: () => ipcRenderer.invoke('window:toggleFullscreen'),
  windowIsFullscreen: () => ipcRenderer.invoke('window:isFullscreen'),
  onFullscreenChange: (callback) => {
    const handler = (_event, isFullscreen) => callback(isFullscreen)
    ipcRenderer.on('fullscreen:changed', handler)
    return () => ipcRenderer.removeListener('fullscreen:changed', handler)
  },
  isScreenshotMode: process.argv.includes('--screenshot'),
  screenshotCapture: (theme) => ipcRenderer.invoke('screenshot:capture', theme),
  screenshotCombine: () => ipcRenderer.invoke('screenshot:combine'),
  checkForUpdate: () => ipcRenderer.invoke('update:check'),
  dismissUpdate: (version) => ipcRenderer.invoke('update:dismiss', version),
  openReleasePage: (url) => ipcRenderer.invoke('update:openRelease', url),
  getUpdateCheckEnabled: () => ipcRenderer.invoke('update:getEnabled'),
  setUpdateCheckEnabled: (enabled) => ipcRenderer.invoke('update:setEnabled', enabled),
  onUpdateAvailable: (callback) => {
    const handler = (_event, data) => callback(data)
    ipcRenderer.on('update:available', handler)
    return () => ipcRenderer.removeListener('update:available', handler)
  },
})
