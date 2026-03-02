import { useState, useEffect, useRef } from 'react'
import useVisualSettings from '../../hooks/useVisualSettings'
import { segmentationCache, countAllCustomized, type UserEditedMaskExport } from '../../lib/segmentation/cache'
import { artCache } from '../../lib/artCache'
import { maskOverrideStore } from '../../lib/segmentation/maskOverrideStore'
import { DEFAULT_MASK_PARAMS } from '../../lib/segmentation/depthToMask'
import { SettingRow, SettingToggle, SettingSlider, SettingSelect, SettingSection } from './SettingsControls'
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
  { value: 'radial-burst', label: 'Radial Burst', description: 'Bars radiating from center' },
  { value: 'waveform', label: 'Waveform', description: 'Oscilloscope-style time domain line' },
  { value: 'mirrored-bars', label: 'Mirrored Bars', description: 'Symmetric bars from center line' },
  { value: 'random', label: 'Random', description: 'Pick a random style each track' },
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

/** Duration (ms) before status indicators (e.g. "Cleared", "Exported") reset to idle. */
const STATUS_RESET_MS = 2000

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
      const id = setTimeout(() => { setExportStatus('idle'); timersRef.current.delete(id) }, STATUS_RESET_MS)
      timersRef.current.add(id)
    } catch {
      setExportStatus('error')
      const id = setTimeout(() => { setExportStatus('idle'); timersRef.current.delete(id) }, STATUS_RESET_MS)
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
      const id = setTimeout(() => { setImportStatus('idle'); timersRef.current.delete(id) }, STATUS_RESET_MS)
      timersRef.current.add(id)
    } catch {
      setImportStatus('error')
      const id = setTimeout(() => { setImportStatus('idle'); timersRef.current.delete(id) }, STATUS_RESET_MS)
      timersRef.current.add(id)
    }
  }

  return (
    <SettingSection title="Depth Layers">
      <SettingToggle
        label="Enable depth layers"
        description="Split album art into layers — visualizer appears inside the image"
        badge={
          <span
            className="text-[10px] uppercase tracking-wider font-medium px-1.5 py-0.5 rounded"
            style={{ color: 'var(--accent)', background: 'var(--accent-dim)' }}
          >
            {hwAccel}
          </span>
        }
        enabled={settings.depthLayerEnabled}
        onToggle={() => toggle('depthLayerEnabled')}
      />

      <SettingSelect
        label="Segmentation model"
        description={BACKEND_OPTIONS.find(o => o.value === settings.segmentationBackend)?.description ?? ''}
        disabled={!settings.depthLayerEnabled}
        value={settings.segmentationBackend}
        onChange={(v) => setSetting('segmentationBackend', v as SegmentationBackend)}
        options={BACKEND_OPTIONS.map(o => ({ value: o.value, label: o.label + (o.size ? ` (${o.size})` : '') }))}
      />

      <SettingSelect
        label="Visualizer style"
        description={VISUALIZER_STYLE_OPTIONS.find(o => o.value === settings.visualizerStyle)?.description ?? ''}
        value={settings.visualizerStyle}
        onChange={(v) => setSetting('visualizerStyle', v as VisualizerStyle)}
        options={VISUALIZER_STYLE_OPTIONS}
      />

      <SettingSlider
        label="Visualizer intensity"
        description="Controls bar contrast and transparency"
        value={settings.visualizerIntensity}
        onChange={(v) => setSetting('visualizerIntensity', v)}
        min={10} max={100} step={5}
      />

      {/* Bar color selector — custom swatch layout */}
      <SettingRow
        label="Bar color"
        description={
          settings.visualizerColorMode === 'auto'
            ? 'Derived from album art accent color'
            : settings.visualizerColorMode === 'custom'
              ? 'Custom color'
              : COLOR_OPTIONS.find(o => o.value === settings.visualizerColorMode)?.label ?? ''
        }
      >
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
      </SettingRow>

      {/* Custom color picker */}
      {settings.visualizerColorMode === 'custom' && (
        <SettingRow label="Custom color">
          <input
            type="color"
            value={settings.visualizerCustomColor}
            onChange={(e) => setSetting('visualizerCustomColor', e.target.value)}
            className="shrink-0 rounded-lg cursor-pointer border-0"
            style={{ width: 36, height: 28, padding: 0, background: 'none' }}
          />
        </SettingRow>
      )}

      {/* Mask Tuning sliders */}
      {settings.depthLayerEnabled && (
        <SettingSection
          title="Mask Tuning"
          action={{ label: 'Reset to defaults', onClick: () => setSetting('maskDefaults', {}) }}
          description="Global defaults for new masks — per-album overrides take priority"
          className="mt-3"
        >
          {MASK_SLIDERS.map(({ key, label, description, min, max, step }) => (
            <SettingSlider
              key={key}
              label={label}
              description={description}
              compact
              value={settings.maskDefaults[key] ?? DEFAULT_MASK_PARAMS[key]}
              onChange={(v) => setSetting('maskDefaults', { ...settings.maskDefaults, [key]: v })}
              min={min} max={max} step={step}
              valueWidth="w-5"
            />
          ))}
        </SettingSection>
      )}

      {/* Auto-generated cache */}
      <SettingRow label="Auto-generated cache" description="Clear auto-generated masks (keeps user edits)">
        <button
          onClick={async () => {
            await segmentationCache.clear()
            await artCache.clear()
            bumpCacheVersion()
            setCacheCleared(true)
            const id = setTimeout(() => { setCacheCleared(false); timersRef.current.delete(id) }, STATUS_RESET_MS)
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
      </SettingRow>

      {/* User-edited masks */}
      <SettingRow
        label="User-edited masks"
        description="Export, import, or clear per-album mask customizations"
        badge={
          customizedCount > 0 ? (
            <span
              className="text-[10px] px-1.5 py-0.5 rounded"
              style={{ background: 'var(--accent-dim)', color: 'var(--accent)' }}
            >
              {customizedCount}
            </span>
          ) : undefined
        }
      >
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
              const id = setTimeout(() => { setUserCacheCleared(false); timersRef.current.delete(id) }, STATUS_RESET_MS)
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
      </SettingRow>

      {/* Clear all masks */}
      <SettingRow label="All masks" description="Clear everything and re-process from scratch">
        <button
          onClick={async () => {
            await segmentationCache.clearAll()
            await maskOverrideStore.clearAll()
            await artCache.clear()
            bumpCacheVersion()
            setAllCacheCleared(true)
            const id = setTimeout(() => { setAllCacheCleared(false); timersRef.current.delete(id) }, STATUS_RESET_MS)
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
      </SettingRow>

      {/* Process all album art */}
      <SettingRow
        label="Pre-process all"
        description={batchProcessing
          ? 'Processing album art in the background...'
          : 'Generate masks for all uncached album art'}
      >
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
      </SettingRow>
    </SettingSection>
  )
}
