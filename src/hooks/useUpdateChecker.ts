import { useState, useEffect, useCallback } from 'react'
import type { UpdateInfo } from '../types'

export default function useUpdateChecker() {
  const [updateInfo, setUpdateInfo] = useState<UpdateInfo | null>(null)

  // Listen for push events from the main process
  useEffect(() => {
    if (!window.electronAPI?.onUpdateAvailable) return
    return window.electronAPI.onUpdateAvailable((info) => {
      setUpdateInfo(info)
    })
  }, [])

  const dismiss = useCallback((version: string) => {
    window.electronAPI?.dismissUpdate(version)
    setUpdateInfo(null)
  }, [])

  const openRelease = useCallback((url: string) => {
    window.electronAPI?.openReleasePage(url)
  }, [])

  const checkNow = useCallback(async () => {
    const info = await window.electronAPI?.checkForUpdate()
    if (info) setUpdateInfo(info)
    return info ?? null
  }, [])

  return { updateInfo, dismiss, openRelease, checkNow }
}
