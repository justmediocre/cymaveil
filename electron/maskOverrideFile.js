// @ts-check

import { dialog } from 'electron'
import fs from 'fs'

/**
 * Export mask override data to a JSON file.
 * Shows a native save dialog and writes the JSON string to the selected path.
 * Returns the saved file path, or null if cancelled.
 * @param {import('electron').BrowserWindow} win
 * @param {string} jsonData
 * @returns {Promise<string | null>}
 */
export async function exportMaskOverrides(win, jsonData) {
  const result = await dialog.showSaveDialog(win, {
    title: 'Export Mask Overrides',
    defaultPath: 'mask-overrides.json',
    filters: [{ name: 'JSON Files', extensions: ['json'] }],
  })

  if (result.canceled || !result.filePath) return null

  fs.writeFileSync(result.filePath, jsonData, 'utf-8')
  return result.filePath
}

/**
 * Import mask override data from a JSON file.
 * Shows a native open dialog and reads the file content as a string.
 * Returns the raw JSON string, or null if cancelled.
 * @param {import('electron').BrowserWindow} win
 * @returns {Promise<string | null>}
 */
export async function importMaskOverrides(win) {
  const result = await dialog.showOpenDialog(win, {
    title: 'Import Mask Overrides',
    filters: [{ name: 'JSON Files', extensions: ['json'] }],
    properties: ['openFile'],
  })

  if (result.canceled || result.filePaths.length === 0) return null

  return fs.readFileSync(result.filePaths[0], 'utf-8')
}
