import { createContext, useContext, useState, useCallback, useEffect, useMemo } from 'react'

interface ThemeContextValue {
  theme: string
  isLight: boolean
  toggleTheme: () => void
}

const ThemeContext = createContext<ThemeContextValue>(null!)

export function ThemeProvider({ children }: { children: React.ReactNode }) {
  const [theme, setTheme] = useState('dark')

  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme)
  }, [theme])

  const toggleTheme = useCallback(() => {
    setTheme((prev) => (prev === 'dark' ? 'light' : 'dark'))
  }, [])

  const value = useMemo<ThemeContextValue>(
    () => ({ theme, isLight: theme === 'light', toggleTheme }),
    [theme, toggleTheme],
  )

  return <ThemeContext.Provider value={value}>{children}</ThemeContext.Provider>
}

export function useTheme() {
  return useContext(ThemeContext)
}
