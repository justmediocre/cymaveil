import { useState } from 'react'
import LibraryTab from '../settings/LibraryTab'
import PlaybackTab from '../settings/PlaybackTab'
import VisualsTab from '../settings/VisualsTab'
import DepthLayersTab from '../settings/DepthLayersTab'
import AboutTab from '../settings/AboutTab'

interface SettingsViewProps {
  onProcessAll: () => void
  batchProcessing: boolean
}

type SettingsTab = 'library' | 'playback' | 'visuals' | 'depth' | 'about'

const TABS: { id: SettingsTab; label: string }[] = [
  { id: 'library', label: 'Library' },
  { id: 'playback', label: 'Playback' },
  { id: 'visuals', label: 'Visuals' },
  { id: 'depth', label: 'Depth Layers' },
  { id: 'about', label: 'About' },
]

export default function SettingsView({ onProcessAll, batchProcessing }: SettingsViewProps) {
  const [activeTab, setActiveTab] = useState<SettingsTab>('library')

  return (
    <div className="h-full overflow-y-auto overflow-x-hidden px-10 pb-6">
      <div className="pt-6 pb-2">
        <h1 className="font-display text-2xl font-bold" style={{ color: 'var(--text-primary)' }}>
          Settings
        </h1>
      </div>

      {/* Tab bar */}
      <div className="flex gap-1 mb-6 pb-px" style={{ borderBottom: '1px solid var(--border-subtle)' }}>
        {TABS.map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id)}
            className="relative px-4 py-2.5 text-sm font-medium transition-colors rounded-t-lg"
            style={{
              color: activeTab === tab.id ? 'var(--accent)' : 'var(--text-secondary)',
              background: activeTab === tab.id ? 'var(--accent-dim)' : 'transparent',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLButtonElement>) => {
              if (activeTab !== tab.id) e.currentTarget.style.color = 'var(--text-primary)'
            }}
            onMouseLeave={(e: React.MouseEvent<HTMLButtonElement>) => {
              if (activeTab !== tab.id) e.currentTarget.style.color = 'var(--text-secondary)'
            }}
          >
            {tab.label}
            {activeTab === tab.id && (
              <span
                className="absolute bottom-[-1px] left-2 right-2 rounded-full"
                style={{ height: 2, background: 'var(--accent)' }}
              />
            )}
          </button>
        ))}
      </div>

      {activeTab === 'library' && <LibraryTab />}
      {activeTab === 'playback' && <PlaybackTab />}
      {activeTab === 'visuals' && <VisualsTab />}
      {activeTab === 'depth' && <DepthLayersTab onProcessAll={onProcessAll} batchProcessing={batchProcessing} />}
      {activeTab === 'about' && <AboutTab />}
    </div>
  )
}
