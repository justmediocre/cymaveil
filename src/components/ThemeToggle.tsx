import { motion } from 'motion/react'
import { SunIcon, MoonIcon } from './Icons'
import { useTheme } from '../contexts/ThemeContext'

export default function ThemeToggle() {
  const { theme, toggleTheme } = useTheme()
  const isDark = theme === 'dark'

  return (
    <motion.button
      onClick={toggleTheme}
      className="no-drag relative flex items-center justify-center w-8 h-8 rounded-lg"
      style={{ color: 'var(--text-secondary)', background: 'transparent' }}
      whileHover={{ scale: 1.1, color: 'var(--text-primary)' }}
      whileTap={{ scale: 0.9 }}
      transition={{ duration: 0.15, ease: 'easeOut' }}
      aria-label={isDark ? 'Switch to light mode' : 'Switch to dark mode'}
      title={isDark ? 'Switch to light mode' : 'Switch to dark mode'}
    >
      <motion.div
        initial={false}
        animate={{ rotate: isDark ? 0 : 180, opacity: 1 }}
        transition={{ duration: 0.4, ease: 'easeInOut' }}
      >
        {isDark ? <MoonIcon size={16} /> : <SunIcon size={16} />}
      </motion.div>
    </motion.button>
  )
}
