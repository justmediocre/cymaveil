import { useState, useEffect, useRef } from 'react'
import useVisualSettings from '../../hooks/useVisualSettings'
import { segmentationCache, countAllCustomized, type UserEditedMaskExport } from '../../lib/segmentation/cache'
import { artCache } from '../../lib/artCache'
import { maskOverrideStore } from '../../lib/segmentation/maskOverrideStore'
import { DEFAULT_MASK_PARAMS } from '../../lib/segmentation/depthToMask'
import type { MaskOverrideRecord } from '../../lib/segmentation/maskOverrideStore'
import type { SegmentationBackend, VisualizerStyle, VisualizerColorMode, MaskPostProcessParams } from '../../types'

interface MaskSliderDef {
  key: keyof MaskPostProcessParams
  label: string
  description: string
  min: number
  max: number
  step: number
}

const MASK_SLIDERS: MaskSliderDef[] = [
  { key: 'bilateralRadius', label: 'Smoothing radius', description: 'Bilateral filter kernel size', min: 1, max: 8, step: 1 },
  { key: 'bilateralSigmaRange', label: 'Edge sharpness', description: 'How strongly edges are preserved', min: 5, max: 60, step: 1 },
  { key: 'morphCloseRadius', label: 'Fill holes', description: 'Close small gaps in the foreground', min: 0, max: 15, step: 1 },
  { key: 'morphOpenRadius', label: 'Remove islands', description: 'Remove small foreground noise', min: 0, max: 15, step: 1 },
  { key: 'textPromotionRadius', label: 'Text recovery', description: 'Promote text/logos from background to foreground', min: 0, max: 8, step: 1 },
  { key: 'textPromotionSensitivity', label: 'Text sensitivity', description: 'How aggressively text regions are detected', min: 0, max: 100, step: 5 },
  { key: 'edgeRefineRadius', label: 'Snap to edges', description: 'Align mask boundary to image edges', min: 0, max: 6, step: 1 },
  { key: 'featherRadius', label: 'Edge softness', description: 'Anti-alias blur on mask edges', min: 0, max: 5, step: 1 },
]

const BACKEND_OPTIONS: { value: SegmentationBackend; label: string; description: string; size: string }[] = [
  { value: 'none', label: 'None', description: 'Depth layers disabled', size: '' },
  { value: 'manual', label: 'Manual', description: 'User-painted masks only — no ML model', size: '' },
  { value: 'depth-anything', label: 'Depth Anything v2', description: 'Monocular depth estimation — works on any image', size: '~25 MB' },
]

const VISUALIZER_STYLE_OPTIONS: { value: VisualizerStyle; label: string; description: string }[] = [
  { value: 'contour-bars', label: 'Contour Bars', description: 'Bars along detected edges (classic)' },
  { value: 'full-surface', label: 'Full Surface', description: 'Bars across entire image width' },
]

const COLOR_OPTIONS: { value: VisualizerColorMode; label: string; swatch: string }[] = [
  { value: 'auto', label: 'Auto', swatch: '' },
  { value: 'white', label: 'White', swatch: '#ffffff' },
  { value: 'cyan', label: 'Cyan', swatch: '#00e6ff' },
  { value: 'magenta', label: 'Magenta', swatch: '#ff32c8' },
  { value: 'gold', label: 'Gold', swatch: '#ffc832' },
  { value: 'red', label: 'Red', swatch: '#ff3232' },
  { value: 'green', label: 'Green', swatch: '#32ff64' },
  { value: 'custom', label: 'Custom', swatch: '' },
]

function detectAcceleration(): string {
  if (typeof navigator !== 'undefined' && 'gpu' in navigator) return 'WebGPU'
  return 'WASM'
}

interface DepthLayersTabProps {
  onProcessAll: () => void
  batchProcessing: boolean
}

export default function DepthLayersTab({ onProcessAll, batchProcessing }: DepthLayersTabProps) {
  const { settings, toggle, setSetting } = useVisualSettings()
  const [hwAccel] = useState(detectAcceleration)
  const [cacheCleared, setCacheCleared] = useState(false)
  const [userCacheCleared, setUserCacheCleared] = useState(false)
  const [allCacheCleared, setAllCacheCleared] = useState(false)
  const [customizedCount, setCustomizedCount] = useState(0)
  const [exportStatus, setExportStatus] = useState<'idle' | 'done' | 'error'>('idle')
  const [importStatus, setImportStatus] = useState<'idle' | 'done' | 'error'>('idle')
  const timersRef = useRef<Set<ReturnType<typeof setTimeout>>>(new Set())

  useEffect(() => {
    return () => {
      timersRef.current.forEach(clearTimeout)
    }
  }, [])

  useEffect(() => {
    countAllCustomized().then(setCustomizedCount).catch(() => {})
  }, [cacheCleared, userCacheCleared, allCacheCleared, importStatus])

  const bumpCacheVersion = () => {
    setSetting('maskCacheVersion', (settings.maskCacheVersion ?? 0) + 1)
  }

  const handleExport = async () => {
    try {
      const [overrides, userEditedMasks] = await Promise.all([
        maskOverrideStore.getAll(),
        segmentationCache.exportUserEdited(),
      ])
      const payload = {
        version: 1,
        exportedAt: new Date().toISOString(),
        maskDefaults: settings.maskDefaults,
        overrides,
        userEditedMasks,
      }
      const json = JSON.stringify(payload, null, 2)
      const api = window.electronAPI
      if (api) {
        const result = await api.exportMaskOverrides(json)
        if (!result) return // cancelled
      } else {
        const blob = new Blob([json], { type: 'application/json' })
        const url = URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = 'mask-overrides.json'
        a.click()
        URL.revokeObjectURL(url)
      }
      setExportStatus('done')
      const id = setTimeout(() => { setExportStatus('idle'); timersRef.current.delete(id) }, 2000)
      timersRef.current.add(id)
    } catch {
      setExportStatus('error')
      const id = setTimeout(() => { setExportStatus('idle'); timersRef.current.delete(id) }, 2000)
      timersRef.current.add(id)
    }
  }

  const handleImport = async () => {
    try {
      let raw: string | null = null
      const api = window.electronAPI
      if (api) {
        raw = await api.importMaskOverrides()
      } else {
        raw = await new Promise<string | null>((resolve) => {
          const input = document.createElement('input')
          input.type = 'file'
          input.accept = '.json'
          input.onchange = () => {
            const file = input.files?.[0]
            if (!file) { resolve(null); return }
            const reader = new FileReader()
            reader.onload = () => resolve(reader.result as string)
            reader.onerror = () => resolve(null)
            reader.readAsText(file)
          }
          input.click()
        })
      }
      if (!raw) return // cancelled

      const data = JSON.parse(raw)
      if (data.version !== 1 || !Array.isArray(data.overrides)) {
        throw new Error('Invalid format')
      }

      // Merge maskDefaults
      if (data.maskDefaults && typeof data.maskDefaults === 'object') {
        setSetting('maskDefaults', { ...settings.maskDefaults, ...data.maskDefaults })
      }

      // Upsert overrides
      const records = data.overrides as MaskOverrideRecord[]
      if (records.length > 0) {
        await maskOverrideStore.putBatch(records)
      }

      // Restore user-edited mask pixel data (brush-painted masks)
      if (Array.isArray(data.userEditedMasks) && data.userEditedMasks.length > 0) {
        await segmentationCache.importUserEdited(data.userEditedMasks as UserEditedMaskExport[])
      }

      bumpCacheVersion()
      setImportStatus('done')
      const id = setTimeout(() => { setImportStatus('idle'); timersRef.current.delete(id) }, 2000)
      timersRef.current.add(id)
    } catch {
      setImportStatus('error')
      const id = setTimeout(() => { setImportStatus('idle'); timersRef.current.delete(id) }, 2000)
      timersRef.current.add(id)
    }
  }

  return (
    <section className="max-w-lg">
      <h2
        className="font-display text-xs font-bold tracking-wider uppercase mb-4"
        style={{ color: 'var(--text-tertiary)' }}
      >
        Depth Layers
      </h2>

      <div className="flex flex-col gap-1">
        {/* Enable toggle */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <div className="flex items-center gap-2">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                Enable depth layers
              </span>
              <span
                className="text-[10px] uppercase tracking-wider font-medium px-1.5 py-0.5 rounded"
                style={{ color: 'var(--accent)', background: 'var(--accent-dim)' }}
              >
                {hwAccel}
              </span>
            </div>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              Split album art into layers — visualizer appears inside the image
            </span>
          </div>

          <button
            onClick={() => toggle('depthLayerEnabled')}
            className="shrink-0 relative rounded-full transition-colors duration-200"
            style={{
              width: 40,
              height: 22,
              background: settings.depthLayerEnabled ? 'var(--accent)' : 'var(--bg-elevated)',
              border: `1px solid ${settings.depthLayerEnabled ? 'var(--accent)' : 'var(--border)'}`,
            }}
            aria-label="Toggle depth layers"
            role="switch"
            aria-checked={settings.depthLayerEnabled}
          >
            <span
              className="absolute top-[2px] rounded-full transition-all duration-200"
              style={{
                width: 16,
                height: 16,
                background: settings.depthLayerEnabled ? '#fff' : 'var(--text-tertiary)',
                left: settings.depthLayerEnabled ? 20 : 2,
              }}
            />
          </button>
        </div>

        {/* Segmentation backend dropdown */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{
            background: 'transparent',
            opacity: settings.depthLayerEnabled ? 1 : 0.5,
            pointerEvents: settings.depthLayerEnabled ? 'auto' : 'none',
          }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              Segmentation model
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              {BACKEND_OPTIONS.find(o => o.value === settings.segmentationBackend)?.description ?? ''}
            </span>
          </div>

          <select
            value={settings.segmentationBackend}
            onChange={(e) => setSetting('segmentationBackend', e.target.value as SegmentationBackend)}
            className="shrink-0 text-sm rounded-lg px-3 py-1.5 cursor-pointer"
            style={{
              background: 'var(--bg-elevated)',
              color: 'var(--text-primary)',
              border: '1px solid var(--border)',
            }}
          >
            {BACKEND_OPTIONS.map(opt => (
              <option key={opt.value} value={opt.value}>
                {opt.label}{opt.size ? ` (${opt.size})` : ''}
              </option>
            ))}
          </select>
        </div>

        {/* Visualizer style dropdown */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              Visualizer style
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              {VISUALIZER_STYLE_OPTIONS.find(o => o.value === settings.visualizerStyle)?.description ?? ''}
            </span>
          </div>

          <select
            value={settings.visualizerStyle}
            onChange={(e) => setSetting('visualizerStyle', e.target.value as VisualizerStyle)}
            className="shrink-0 text-sm rounded-lg px-3 py-1.5 cursor-pointer"
            style={{
              background: 'var(--bg-elevated)',
              color: 'var(--text-primary)',
              border: '1px solid var(--border)',
            }}
          >
            {VISUALIZER_STYLE_OPTIONS.map(opt => (
              <option key={opt.value} value={opt.value}>
                {opt.label}
              </option>
            ))}
          </select>
        </div>

        {/* Visualizer intensity slider */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              Visualizer intensity
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              Controls bar contrast and transparency
            </span>
          </div>

          <div className="shrink-0 flex items-center gap-2">
            <input
              type="range"
              min={10}
              max={100}
              step={5}
              value={settings.visualizerIntensity}
              onChange={(e) => setSetting('visualizerIntensity', Number(e.target.value))}
              className="w-24 accent-[var(--accent)]"
              style={{ cursor: 'pointer' }}
            />
            <span
              className="text-xs tabular-nums w-7 text-right"
              style={{ color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}
            >
              {settings.visualizerIntensity}
            </span>
          </div>
        </div>

        {/* Bar color selector */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              Bar color
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              {settings.visualizerColorMode === 'auto'
                ? 'Derived from album art accent color'
                : settings.visualizerColorMode === 'custom'
                  ? 'Custom color'
                  : COLOR_OPTIONS.find(o => o.value === settings.visualizerColorMode)?.label ?? ''}
            </span>
          </div>

          <div className="shrink-0 flex items-center gap-1.5">
            {COLOR_OPTIONS.map(opt => (
              <button
                key={opt.value}
                onClick={() => setSetting('visualizerColorMode', opt.value)}
                title={opt.label}
                className="rounded-full transition-all duration-150"
                style={{
                  width: settings.visualizerColorMode === opt.value ? 22 : 18,
                  height: settings.visualizerColorMode === opt.value ? 22 : 18,
                  background: opt.value === 'auto'
                    ? 'conic-gradient(#f44, #ff0, #0f0, #0ff, #00f, #f0f, #f44)'
                    : opt.value === 'custom'
                      ? settings.visualizerCustomColor
                      : opt.swatch,
                  border: settings.visualizerColorMode === opt.value
                    ? '2px solid var(--text-primary)'
                    : '1px solid var(--border-subtle)',
                  opacity: settings.visualizerColorMode === opt.value ? 1 : 0.7,
                }}
              />
            ))}
          </div>
        </div>

        {/* Custom color picker — only visible when custom mode selected */}
        {settings.visualizerColorMode === 'custom' && (
          <div
            className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
            style={{ background: 'transparent' }}
            onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
            onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
          >
            <div className="flex flex-col gap-0.5 min-w-0 mr-4">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                Custom color
              </span>
            </div>

            <input
              type="color"
              value={settings.visualizerCustomColor}
              onChange={(e) => setSetting('visualizerCustomColor', e.target.value)}
              className="shrink-0 rounded-lg cursor-pointer border-0"
              style={{ width: 36, height: 28, padding: 0, background: 'none' }}
            />
          </div>
        )}

        {/* Mask Tuning — global default sliders */}
        {settings.depthLayerEnabled && (
          <>
            <div className="mt-3 mb-1 px-4">
              <div className="flex items-center justify-between">
                <span
                  className="text-[10px] font-bold tracking-wider uppercase"
                  style={{ color: 'var(--text-tertiary)' }}
                >
                  Mask Tuning
                </span>
                <button
                  onClick={() => setSetting('maskDefaults', {})}
                  className="text-[10px] transition-colors"
                  style={{ color: 'var(--text-tertiary)' }}
                  onMouseEnter={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--accent)')}
                  onMouseLeave={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--text-tertiary)')}
                >
                  Reset to defaults
                </button>
              </div>
              <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                Global defaults for new masks — per-album overrides take priority
              </span>
            </div>

            {MASK_SLIDERS.map(({ key, label, description, min, max, step }) => {
              const value = settings.maskDefaults[key] ?? DEFAULT_MASK_PARAMS[key]
              return (
                <div
                  key={key}
                  className="flex items-center justify-between py-2.5 px-4 rounded-xl transition-colors"
                  style={{ background: 'transparent' }}
                  onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
                  onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
                >
                  <div className="flex flex-col gap-0.5 min-w-0 mr-4">
                    <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                      {label}
                    </span>
                    <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
                      {description}
                    </span>
                  </div>

                  <div className="shrink-0 flex items-center gap-2">
                    <input
                      type="range"
                      min={min}
                      max={max}
                      step={step}
                      value={value}
                      onChange={(e) => {
                        const next = { ...settings.maskDefaults, [key]: Number(e.target.value) }
                        setSetting('maskDefaults', next)
                      }}
                      className="w-24 accent-[var(--accent)]"
                      style={{ cursor: 'pointer' }}
                    />
                    <span
                      className="text-xs tabular-nums w-5 text-right"
                      style={{ color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}
                    >
                      {value}
                    </span>
                  </div>
                </div>
              )
            })}
          </>
        )}

        {/* Clear auto-generated cache */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              Auto-generated cache
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              Clear auto-generated masks (keeps user edits)
            </span>
          </div>

          <button
            onClick={async () => {
              await segmentationCache.clear()
              await artCache.clear()
              bumpCacheVersion()
              setCacheCleared(true)
              const id = setTimeout(() => { setCacheCleared(false); timersRef.current.delete(id) }, 2000)
              timersRef.current.add(id)
            }}
            className="shrink-0 text-sm px-3 py-1.5 rounded-lg transition-colors"
            style={{
              color: cacheCleared ? 'var(--text-secondary)' : 'var(--accent)',
              background: cacheCleared ? 'var(--bg-elevated)' : 'var(--accent-dim)',
            }}
          >
            {cacheCleared ? 'Cleared' : 'Clear cache'}
          </button>
        </div>

        {/* User-edited masks — Export / Import / Clear */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <div className="flex items-center gap-2">
              <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
                User-edited masks
              </span>
              {customizedCount > 0 && (
                <span
                  className="text-[10px] px-1.5 py-0.5 rounded"
                  style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
                >
                  {customizedCount}
                </span>
              )}
            </div>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              Export, import, or clear per-album mask customizations
            </span>
          </div>

          <div className="shrink-0 flex items-center gap-1.5">
            <button
              onClick={handleExport}
              disabled={customizedCount === 0}
              className="text-sm px-3 py-1.5 rounded-lg transition-colors"
              style={{
                color: exportStatus === 'done' ? 'var(--text-secondary)' : exportStatus === 'error' ? '#f87171' : 'var(--accent)',
                background: exportStatus === 'idle' ? 'var(--accent-dim)' : 'var(--bg-elevated)',
                opacity: customizedCount === 0 ? 0.5 : 1,
              }}
            >
              {exportStatus === 'done' ? 'Exported' : exportStatus === 'error' ? 'Error' : 'Export'}
            </button>
            <button
              onClick={handleImport}
              className="text-sm px-3 py-1.5 rounded-lg transition-colors"
              style={{
                color: importStatus === 'done' ? 'var(--text-secondary)' : importStatus === 'error' ? '#f87171' : 'var(--accent)',
                background: importStatus === 'idle' ? 'var(--accent-dim)' : 'var(--bg-elevated)',
              }}
            >
              {importStatus === 'done' ? 'Imported' : importStatus === 'error' ? 'Error' : 'Import'}
            </button>
            <button
              onClick={async () => {
                await segmentationCache.clearUserEdited()
                await maskOverrideStore.clearAll()
                bumpCacheVersion()
                setUserCacheCleared(true)
                const id = setTimeout(() => { setUserCacheCleared(false); timersRef.current.delete(id) }, 2000)
                timersRef.current.add(id)
              }}
              disabled={customizedCount === 0}
              className="text-sm px-3 py-1.5 rounded-lg transition-colors"
              style={{
                color: userCacheCleared ? 'var(--text-secondary)' : 'var(--accent)',
                background: userCacheCleared ? 'var(--bg-elevated)' : 'var(--accent-dim)',
                opacity: customizedCount === 0 ? 0.5 : 1,
              }}
            >
              {userCacheCleared ? 'Cleared' : 'Clear'}
            </button>
          </div>
        </div>

        {/* Clear all masks */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              All masks
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              Clear everything and re-process from scratch
            </span>
          </div>

          <button
            onClick={async () => {
              await segmentationCache.clearAll()
              await maskOverrideStore.clearAll()
              await artCache.clear()
              bumpCacheVersion()
              setAllCacheCleared(true)
              const id = setTimeout(() => { setAllCacheCleared(false); timersRef.current.delete(id) }, 2000)
              timersRef.current.add(id)
            }}
            className="shrink-0 text-sm px-3 py-1.5 rounded-lg transition-colors"
            style={{
              color: allCacheCleared ? 'var(--text-secondary)' : 'var(--accent)',
              background: allCacheCleared ? 'var(--bg-elevated)' : 'var(--accent-dim)',
            }}
          >
            {allCacheCleared ? 'Cleared' : 'Clear all'}
          </button>
        </div>

        {/* Process all album art */}
        <div
          className="flex items-center justify-between py-3 px-4 rounded-xl transition-colors"
          style={{ background: 'transparent' }}
          onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
          onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
        >
          <div className="flex flex-col gap-0.5 min-w-0 mr-4">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              Pre-process all
            </span>
            <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
              {batchProcessing
                ? 'Processing album art in the background...'
                : 'Generate masks for all uncached album art'}
            </span>
          </div>

          <button
            onClick={onProcessAll}
            disabled={batchProcessing || !settings.depthLayerEnabled || settings.segmentationBackend === 'none' || settings.segmentationBackend === 'manual'}
            className="shrink-0 text-sm px-3 py-1.5 rounded-lg transition-colors"
            style={{
              color: batchProcessing ? 'var(--text-secondary)' : 'var(--accent)',
              background: batchProcessing ? 'var(--bg-elevated)' : 'var(--accent-dim)',
              opacity: (!settings.depthLayerEnabled || settings.segmentationBackend === 'none' || settings.segmentationBackend === 'manual') ? 0.5 : 1,
            }}
          >
            {batchProcessing ? 'Processing...' : 'Process all'}
          </button>
        </div>
      </div>
    </section>
  )
}
