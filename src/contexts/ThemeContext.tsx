import { createContext, useContext, useState, useCallback, useEffect, useMemo } from 'react'

export type ThemePreference = 'light' | 'dark' | 'system'

interface ThemeContextValue {
  theme: string
  isLight: boolean
  preference: ThemePreference
  setPreference: (pref: ThemePreference) => void
  toggleTheme: () => void
}

const STORAGE_KEY = 'cymaveil-theme'

function getSystemTheme(): string {
  return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark'
}

function getStoredPreference(): ThemePreference {
  const stored = localStorage.getItem(STORAGE_KEY)
  if (stored === 'light' || stored === 'dark' || stored === 'system') return stored
  return 'system'
}

function resolveTheme(pref: ThemePreference): string {
  return pref === 'system' ? getSystemTheme() : pref
}

const ThemeContext = createContext<ThemeContextValue>(null!)

export function ThemeProvider({ children }: { children: React.ReactNode }) {
  const [preference, setPreferenceState] = useState<ThemePreference>(getStoredPreference)
  const [resolved, setResolved] = useState(() => resolveTheme(getStoredPreference()))

  const setPreference = useCallback((pref: ThemePreference) => {
    localStorage.setItem(STORAGE_KEY, pref)
    setPreferenceState(pref)
    setResolved(resolveTheme(pref))
  }, [])

  useEffect(() => {
    document.documentElement.setAttribute('data-theme', resolved)
  }, [resolved])

  // Listen for OS theme changes when preference is 'system'
  useEffect(() => {
    if (preference !== 'system') return
    const mq = window.matchMedia('(prefers-color-scheme: light)')
    const handler = () => setResolved(getSystemTheme())
    mq.addEventListener('change', handler)
    return () => mq.removeEventListener('change', handler)
  }, [preference])

  const toggleTheme = useCallback(() => {
    setPreference(resolved === 'dark' ? 'light' : 'dark')
  }, [resolved, setPreference])

  const value = useMemo<ThemeContextValue>(
    () => ({ theme: resolved, isLight: resolved === 'light', preference, setPreference, toggleTheme }),
    [resolved, preference, setPreference, toggleTheme],
  )

  return <ThemeContext.Provider value={value}>{children}</ThemeContext.Provider>
}

export function useTheme() {
  return useContext(ThemeContext)
}
