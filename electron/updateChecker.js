// @ts-check

import { net, shell } from 'electron'
import Store from 'electron-store'

/**
 * Master toggle — set to `true` when the repo is public and releases exist.
 * When `false`, every exported function is a no-op / returns safe defaults.
 */
const UPDATES_ENABLED = false

const GITHUB_API_URL = 'https://api.github.com/repos/justmediocre/cymaveil/releases/latest'
const CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000 // 24 hours

const store = new Store({
  name: 'update-checker',
  defaults: {
    lastCheckTime: 0,
    dismissedVersion: '',
    updateCheckEnabled: true,
  },
})

/**
 * @typedef {{ version: string; releaseUrl: string; releaseName: string; releaseNotes: string; publishedAt: string }} UpdateInfo
 */

/**
 * Compare two semver strings (major.minor.patch).
 * Returns true if `latest` is newer than `current`.
 * @param {string} current
 * @param {string} latest
 * @returns {boolean}
 */
function isNewerVersion(current, latest) {
  const curParts = current.split('.').map(Number)
  const latParts = latest.split('.').map(Number)
  for (let i = 0; i < 3; i++) {
    const c = curParts[i] || 0
    const l = latParts[i] || 0
    if (l > c) return true
    if (l < c) return false
  }
  return false
}

/**
 * Fetch the latest GitHub release and compare against `currentVersion`.
 * Returns UpdateInfo if a newer version exists, otherwise null.
 * @param {string} currentVersion
 * @returns {Promise<UpdateInfo | null>}
 */
export async function checkForUpdate(currentVersion) {
  if (!UPDATES_ENABLED) return null

  try {
    const response = await net.fetch(GITHUB_API_URL, {
      headers: { 'User-Agent': 'Cymaveil-UpdateChecker' },
    })

    if (!response.ok) return null // 404 (no releases), 403/429 (rate limit)

    const data = await response.json()
    const tag = (data.tag_name || '').replace(/^v/, '')

    if (!tag || !isNewerVersion(currentVersion, tag)) return null

    store.set('lastCheckTime', Date.now())

    return {
      version: tag,
      releaseUrl: data.html_url || '',
      releaseName: data.name || `v${tag}`,
      releaseNotes: data.body || '',
      publishedAt: data.published_at || '',
    }
  } catch {
    // Network error — silently return null
    return null
  }
}

/**
 * Whether enough time has elapsed and auto-check is enabled.
 * @returns {boolean}
 */
export function shouldCheck() {
  if (!UPDATES_ENABLED) return false
  if (!store.get('updateCheckEnabled', true)) return false
  const last = store.get('lastCheckTime', 0)
  return Date.now() - last >= CHECK_INTERVAL_MS
}

/**
 * Whether the user has dismissed notifications for this version.
 * @param {string} version
 * @returns {boolean}
 */
export function isDismissed(version) {
  return store.get('dismissedVersion', '') === version
}

/**
 * Dismiss a specific version so the toast won't reappear.
 * @param {string} version
 */
export function dismissVersion(version) {
  store.set('dismissedVersion', version)
}

/**
 * @returns {boolean}
 */
export function getUpdateCheckEnabled() {
  if (!UPDATES_ENABLED) return false
  return store.get('updateCheckEnabled', true)
}

/**
 * @param {boolean} enabled
 */
export function setUpdateCheckEnabled(enabled) {
  store.set('updateCheckEnabled', enabled)
}

/**
 * Open a URL in the user's default browser.
 * @param {string} url
 */
export function openReleasePage(url) {
  if (url && url.startsWith('https://')) {
    shell.openExternal(url)
  }
}
