/**
 * Renderer-side abstraction for music library operations.
 * Uses Electron IPC when available, falls back to mock data in browser.
 */

import type { ImportResult } from '../types'

const isElectron = () => !!(window.electronAPI?.isElectron)

/**
 * Open a native folder picker and scan the selected folder for music files.
 * Returns { albums, tracks, folderPath } or null if cancelled.
 */
export async function selectAndImportFolder(): Promise<ImportResult | null> {
  if (!isElectron()) {
    return null
  }

  const folderPath = await window.electronAPI!.selectFolder()
  if (!folderPath) return null

  const result = await window.electronAPI!.scanMusicFolder(folderPath)
  return { ...result, folderPath }
}
