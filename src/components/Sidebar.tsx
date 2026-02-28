import { motion, AnimatePresence } from 'motion/react'
import { PlayIcon, MusicNoteIcon, DiscIcon, ListIcon, SearchIcon, HeartIcon, SettingsIcon } from './Icons'

interface UpNextItem {
  id: string
  title: string
  artist: string
  art?: string | null
}

interface NavItem {
  icon: React.ComponentType<{ size?: number }>
  label: string
  value?: string
}

const navItems: NavItem[] = [
  { icon: PlayIcon, label: 'Now Playing', value: 'NowPlaying' },
  { icon: SearchIcon, label: 'Search' },
  { icon: MusicNoteIcon, label: 'Library' },
  { icon: DiscIcon, label: 'Albums' },
  { icon: HeartIcon, label: 'Favorites' },
  { icon: ListIcon, label: 'Playlists' },
  { icon: SettingsIcon, label: 'Settings' },
]

interface SidebarProps {
  isOpen: boolean
  activeNav: string
  onNavChange: (nav: string) => void
  upNextTracks?: UpNextItem[]
}

export default function Sidebar({ isOpen, activeNav, onNavChange, upNextTracks }: SidebarProps) {
  return (
    <AnimatePresence>
      {isOpen && (
        <motion.aside
          className="shrink-0 h-full flex flex-col border-r glass"
          style={{
            background: 'var(--glass-bg-surface)',
            borderColor: 'var(--border-subtle)',
          }}
          initial={{ width: 0, opacity: 0 }}
          animate={{ width: 260, opacity: 1 }}
          exit={{ width: 0, opacity: 0 }}
          transition={{ duration: 0.35, ease: [0.22, 1, 0.36, 1] }}
        >
          <div className="flex flex-col h-full overflow-hidden" style={{ width: 260 }}>
            {/* Logo / Brand area */}
            <div className="drag-region pt-6 pb-3 pr-4 pl-6 text-center">
              <h1
                className="font-display brand-emboss text-[1.75rem] font-black tracking-[0.35em] uppercase select-none leading-none"
                style={{ transform: 'scaleX(1.15)', transformOrigin: 'center' }}
              >
                Cymaveil
              </h1>
            </div>

            {/* Navigation */}
            <nav className="flex-1 min-h-0 overflow-y-auto px-4 flex flex-col" style={{ gap: '1.25rem', paddingTop: '1.25rem' }}>
              {navItems.map(({ icon: Icon, label, value }) => {
                const navKey = value || label
                const effectiveNav = activeNav === 'AlbumDetail' ? 'Albums' : activeNav === 'PlaylistDetail' ? 'Playlists' : activeNav
                const isActive = effectiveNav === navKey
                return (
                  <motion.button
                    key={navKey}
                    onClick={() => onNavChange(navKey)}
                    className="no-drag w-full flex items-center gap-4 pr-4 py-3.5 rounded-xl text-base relative"
                    style={{
                      paddingLeft: '2.5rem',
                      color: isActive ? 'var(--text-primary)' : 'var(--text-secondary)',
                      background: 'transparent',
                    }}
                    whileHover={{ color: 'var(--text-primary)' }}
                    whileTap={{ scale: 0.98 }}
                    transition={{ duration: 0.15, ease: 'easeOut' }}
                  >
                    <Icon size={20} />
                    <span>{label}</span>
                    {isActive && (
                      <motion.div
                        className="absolute left-0 top-1/2 -translate-y-1/2 w-[2px] h-4 rounded-r-full"
                        style={{ background: 'var(--text-secondary)' }}
                        layoutId="nav-indicator"
                        transition={{ duration: 0.3, ease: [0.22, 1, 0.36, 1] }}
                      />
                    )}
                  </motion.button>
                )
              })}

            </nav>

            {/* Bottom section — queue preview */}
            <div className="px-4 py-4 border-t" style={{ borderColor: 'var(--border-subtle)' }}>
              <p
                className="text-[11px] uppercase tracking-wider font-medium mb-3"
                style={{ color: 'var(--text-secondary)' }}
              >
                Up Next
              </p>
              <div className="space-y-2">
                {(upNextTracks || []).map((item, i) => (
                  <motion.div
                    key={item.id || i}
                    className="flex items-center gap-2.5 px-2 py-1.5 rounded-md cursor-pointer"
                    whileHover={{ background: 'var(--bg-hover)' }}
                    transition={{ duration: 0.15, ease: 'easeOut' }}
                  >
                    <div
                      className="w-8 h-8 rounded shrink-0 overflow-hidden"
                      style={{ background: 'var(--bg-elevated)' }}
                    >
                      {item.art && (
                        <img src={item.art} alt="" className="w-full h-full object-cover" draggable={false} />
                      )}
                    </div>
                    <div className="min-w-0">
                      <p
                        className="text-xs font-medium truncate"
                        style={{ color: 'var(--text-primary)' }}
                      >
                        {item.title}
                      </p>
                      <p
                        className="text-[10px] truncate"
                        style={{ color: 'var(--text-secondary)' }}
                      >
                        {item.artist}
                      </p>
                    </div>
                  </motion.div>
                ))}
                {(!upNextTracks || upNextTracks.length === 0) && (
                  <p className="text-[11px] px-2" style={{ color: 'var(--text-tertiary)' }}>
                    Nothing queued
                  </p>
                )}
              </div>
            </div>
          </div>
        </motion.aside>
      )}
    </AnimatePresence>
  )
}
