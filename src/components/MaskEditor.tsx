import { useState, useCallback, useRef, useEffect } from 'react'
import { motion, AnimatePresence } from 'motion/react'
import type {
  MaskPostProcessParams,
  MaskModelParams,
  SegmentationResult,
  SegmentationBackend,
  DepthModelSize,
  DepthModelDtype,
} from '../types'
import { DEFAULT_MASK_PARAMS } from '../lib/segmentation/depthToMask'

interface SliderDef {
  key: keyof MaskPostProcessParams
  label: string
  min: number
  max: number
  step: number
}

const SLIDERS: SliderDef[] = [
  { key: 'bilateralRadius', label: 'Smoothing radius', min: 1, max: 8, step: 1 },
  { key: 'bilateralSigmaRange', label: 'Edge sharpness', min: 5, max: 60, step: 1 },
  { key: 'morphCloseRadius', label: 'Fill holes', min: 0, max: 15, step: 1 },
  { key: 'morphOpenRadius', label: 'Remove islands', min: 0, max: 15, step: 1 },
  { key: 'textPromotionRadius', label: 'Text recovery', min: 0, max: 8, step: 1 },
  { key: 'textPromotionSensitivity', label: 'Text sensitivity', min: 0, max: 100, step: 5 },
  { key: 'edgeRefineRadius', label: 'Snap to edges', min: 0, max: 6, step: 1 },
  { key: 'featherRadius', label: 'Edge softness', min: 0, max: 5, step: 1 },
]

const MODEL_SIZES: { value: DepthModelSize; label: string; size: string }[] = [
  { value: 'small', label: 'Small', size: '~25 MB' },
  { value: 'base', label: 'Base', size: '~25 MB' },
  { value: 'large', label: 'Large', size: '~80 MB' },
]

const MODEL_DTYPES: { value: DepthModelDtype; label: string }[] = [
  { value: 'q4', label: 'Q4' },
  { value: 'q8', label: 'Q8' },
  { value: 'fp16', label: 'FP16' },
  { value: 'fp32', label: 'FP32' },
]

const RESOLUTIONS = [256, 364, 518]

/** Rough download size estimates by model size + dtype */
function estimateDownloadSize(size: DepthModelSize, dtype: DepthModelDtype): string {
  const sizes: Record<DepthModelSize, Record<DepthModelDtype, string>> = {
    small: { q4: '~6 MB', q8: '~13 MB', fp16: '~25 MB', fp32: '~50 MB' },
    base: { q4: '~12 MB', q8: '~25 MB', fp16: '~50 MB', fp32: '~100 MB' },
    large: { q4: '~25 MB', q8: '~50 MB', fp16: '~100 MB', fp32: '~200 MB' },
  }
  return sizes[size]?.[dtype] ?? '?'
}

interface MaskEditorProps {
  depthMap: Uint8Array | null
  artSrc: string
  width: number
  height: number
  initialPostProcessParams: MaskPostProcessParams
  initialModelParams: MaskModelParams
  hasOverride: boolean
  hasUserPaint: boolean
  reprocessing: boolean
  downloadProgress: number | null
  onPreview: (depthMap: Uint8Array, artSrc: string, w: number, h: number, params: MaskPostProcessParams) => void
  onReprocess: (
    artSrc: string,
    backendId: SegmentationBackend,
    modelParams: MaskModelParams,
    postProcessParams: MaskPostProcessParams,
  ) => Promise<void>
  onSave: (
    artSrc: string,
    backendId: SegmentationBackend,
    modelParams: MaskModelParams,
    postProcessParams: MaskPostProcessParams,
  ) => Promise<void>
  onRemoveOverride: (artSrc: string, backendId: SegmentationBackend, defaultPostParams: MaskPostProcessParams) => Promise<void>
  onClose: () => void
  backendId: SegmentationBackend
  onEditBrush?: () => void
}

export default function MaskEditor({
  depthMap,
  artSrc,
  width,
  height,
  initialPostProcessParams,
  initialModelParams,
  hasOverride,
  hasUserPaint,
  reprocessing,
  downloadProgress,
  onPreview,
  onReprocess,
  onSave,
  onRemoveOverride,
  onClose,
  backendId,
  onEditBrush,
}: MaskEditorProps) {
  const [postParams, setPostParams] = useState<MaskPostProcessParams>(initialPostProcessParams)
  const [modelParams, setModelParams] = useState<MaskModelParams>(initialModelParams)
  const [saving, setSaving] = useState(false)
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  // Track whether model params differ from initial (requires re-process)
  const modelChanged =
    modelParams.modelSize !== initialModelParams.modelSize ||
    modelParams.modelDtype !== initialModelParams.modelDtype ||
    modelParams.inputResolution !== initialModelParams.inputResolution

  // Sync initial params when they change (e.g. switching albums)
  useEffect(() => {
    setPostParams(initialPostProcessParams)
    setModelParams(initialModelParams)
  }, [initialPostProcessParams, initialModelParams])

  // Debounced preview on post-process param change
  const handlePostParamChange = useCallback((key: keyof MaskPostProcessParams, value: number) => {
    setPostParams(prev => {
      const next = { ...prev, [key]: value }
      if (debounceRef.current) clearTimeout(debounceRef.current)
      debounceRef.current = setTimeout(() => {
        if (depthMap) {
          onPreview(depthMap, artSrc, width, height, next)
        }
      }, 150)
      return next
    })
  }, [depthMap, artSrc, width, height, onPreview])

  const handleReprocess = useCallback(async () => {
    await onReprocess(artSrc, backendId, modelParams, postParams)
  }, [artSrc, backendId, modelParams, postParams, onReprocess])

  const handleSave = useCallback(async () => {
    setSaving(true)
    await onSave(artSrc, backendId, modelParams, postParams)
    setSaving(false)
  }, [artSrc, backendId, modelParams, postParams, onSave])

  const handleRemoveOverride = useCallback(async () => {
    if (hasUserPaint) {
      const ok = window.confirm('This will discard your manual brush edits. Continue?')
      if (!ok) return
    }
    const globalDefaults = { ...DEFAULT_MASK_PARAMS }
    await onRemoveOverride(artSrc, backendId, globalDefaults)
    setPostParams(globalDefaults)
    setModelParams(initialModelParams)
  }, [artSrc, backendId, initialModelParams, onRemoveOverride, hasUserPaint])

  const handleResetPost = useCallback(() => {
    setPostParams({ ...DEFAULT_MASK_PARAMS })
    if (depthMap) {
      onPreview(depthMap, artSrc, width, height, { ...DEFAULT_MASK_PARAMS })
    }
  }, [depthMap, artSrc, width, height, onPreview])

  return (
    <motion.div
      initial={{ opacity: 0, x: 20 }}
      animate={{ opacity: 1, x: 0 }}
      exit={{ opacity: 0, x: 20 }}
      transition={{ duration: 0.25, ease: [0.22, 1, 0.36, 1] }}
      className="glass rounded-2xl overflow-hidden flex flex-col"
      style={{
        position: 'absolute',
        right: 16,
        top: 48,
        bottom: 16,
        zIndex: 50,
        background: 'var(--glass-bg-surface)',
        border: '1px solid var(--border-subtle)',
        width: 320,
      }}
    >
      {/* Header */}
      <div className="flex items-center justify-between px-4 py-3 border-b" style={{ borderColor: 'var(--border-subtle)' }}>
        <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
          Mask Editor
        </span>
        <button
          onClick={onClose}
          className="flex items-center justify-center w-6 h-6 rounded-md transition-colors"
          style={{ color: 'var(--text-tertiary)' }}
          onMouseEnter={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--text-primary)')}
          onMouseLeave={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--text-tertiary)')}
        >
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <line x1="18" y1="6" x2="6" y2="18" />
            <line x1="6" y1="6" x2="18" y2="18" />
          </svg>
        </button>
      </div>

      {/* Scrollable content */}
      <div className="flex-1 overflow-y-auto px-4 py-3 flex flex-col gap-3" style={{ fontSize: 12 }}>
        {/* Model section — only shown for ML backends */}
        {backendId === 'depth-anything' && (
          <>
            <div>
              <div
                className="text-[10px] font-bold tracking-wider uppercase mb-2"
                style={{ color: 'var(--text-tertiary)' }}
              >
                Model
              </div>

              {/* Model size */}
              <div className="flex items-center justify-between mb-2">
                <span style={{ color: 'var(--text-secondary)' }}>Quality</span>
                <div className="flex rounded-md overflow-hidden" style={{ border: '1px solid var(--border)' }}>
                  {MODEL_SIZES.map(opt => (
                    <button
                      key={opt.value}
                      onClick={() => setModelParams(prev => ({ ...prev, modelSize: opt.value }))}
                      className="px-2.5 py-1 text-[11px] transition-colors"
                      style={{
                        background: modelParams.modelSize === opt.value ? 'var(--accent)' : 'var(--bg-elevated)',
                        color: modelParams.modelSize === opt.value ? '#fff' : 'var(--text-secondary)',
                      }}
                      title={opt.size}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>

              {/* Model dtype */}
              <div className="flex items-center justify-between mb-2">
                <span style={{ color: 'var(--text-secondary)' }}>Precision</span>
                <div className="flex rounded-md overflow-hidden" style={{ border: '1px solid var(--border)' }}>
                  {MODEL_DTYPES.map(opt => (
                    <button
                      key={opt.value}
                      onClick={() => setModelParams(prev => ({ ...prev, modelDtype: opt.value }))}
                      className="px-2.5 py-1 text-[11px] transition-colors"
                      style={{
                        background: modelParams.modelDtype === opt.value ? 'var(--accent)' : 'var(--bg-elevated)',
                        color: modelParams.modelDtype === opt.value ? '#fff' : 'var(--text-secondary)',
                      }}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>

              {/* Resolution */}
              <div className="flex items-center justify-between mb-2">
                <span style={{ color: 'var(--text-secondary)' }}>Resolution</span>
                <select
                  value={modelParams.inputResolution}
                  onChange={(e) => setModelParams(prev => ({ ...prev, inputResolution: Number(e.target.value) }))}
                  className="text-[11px] rounded-md px-2 py-0.5 cursor-pointer"
                  style={{
                    background: 'var(--bg-elevated)',
                    color: 'var(--text-primary)',
                    border: '1px solid var(--border)',
                  }}
                >
                  {RESOLUTIONS.map(r => (
                    <option key={r} value={r}>{r}px</option>
                  ))}
                </select>
              </div>

              {/* Re-process button with download warning */}
              {modelChanged && (
                <div className="flex flex-col gap-1">
                  <button
                    onClick={handleReprocess}
                    disabled={reprocessing}
                    className="w-full text-[11px] py-1.5 rounded-lg transition-colors"
                    style={{
                      background: 'var(--accent-dim)',
                      color: 'var(--accent)',
                      opacity: reprocessing ? 0.6 : 1,
                    }}
                  >
                    {reprocessing ? (
                      <span className="flex items-center justify-center gap-1.5">
                        <svg className="animate-spin" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round">
                          <path d="M12 2a10 10 0 0 1 10 10" />
                        </svg>
                        {downloadProgress != null
                          ? `Downloading model... ${Math.round(downloadProgress * 100)}%`
                          : 'Processing...'}
                      </span>
                    ) : 'Re-process with new model'}
                  </button>
                  {!reprocessing && (
                    <span className="text-[10px] text-center" style={{ color: 'var(--text-tertiary)' }}>
                      May download {estimateDownloadSize(modelParams.modelSize, modelParams.modelDtype)}
                    </span>
                  )}
                  {reprocessing && downloadProgress != null && (
                    <div
                      className="w-full rounded-full overflow-hidden"
                      style={{ height: 2, background: 'var(--border-subtle)' }}
                    >
                      <div
                        className="h-full rounded-full transition-all duration-300"
                        style={{
                          width: `${Math.round(downloadProgress * 100)}%`,
                          background: 'var(--accent)',
                        }}
                      />
                    </div>
                  )}
                </div>
              )}
            </div>

            {/* Divider */}
            <div style={{ height: 1, background: 'var(--border-subtle)' }} />
          </>
        )}

        {/* Post-processing section */}
        <div>
          <div className="flex items-center justify-between mb-2">
            <span
              className="text-[10px] font-bold tracking-wider uppercase"
              style={{ color: 'var(--text-tertiary)' }}
            >
              Post-Processing
            </span>
            <button
              onClick={handleResetPost}
              className="text-[10px] transition-colors"
              style={{ color: 'var(--text-tertiary)' }}
              onMouseEnter={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--accent)')}
              onMouseLeave={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--text-tertiary)')}
            >
              Reset
            </button>
          </div>

          <div className="flex flex-col gap-1.5">
            {SLIDERS.map(({ key, label, min, max, step }) => (
              <div key={key}>
                <div className="flex items-center justify-between">
                  <span style={{ color: 'var(--text-secondary)' }}>{label}</span>
                  <span
                    className="tabular-nums"
                    style={{ color: 'var(--text-tertiary)', fontFamily: 'var(--font-mono)', fontSize: 10 }}
                  >
                    {postParams[key]}
                  </span>
                </div>
                <input
                  type="range"
                  min={min}
                  max={max}
                  step={step}
                  value={postParams[key]}
                  onChange={(e) => handlePostParamChange(key, Number(e.target.value))}
                  className="w-full accent-[var(--accent)]"
                  style={{ cursor: 'pointer' }}
                />
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* Footer buttons */}
      <div className="shrink-0 px-4 py-3 flex flex-col gap-2 border-t" style={{ borderColor: 'var(--border-subtle)' }}>
        <button
          onClick={handleSave}
          disabled={saving}
          className="w-full text-xs font-medium py-2 rounded-lg transition-colors"
          style={{
            background: 'var(--accent)',
            color: '#fff',
            opacity: saving ? 0.6 : 1,
          }}
        >
          {saving ? 'Saving...' : 'Save'}
        </button>

        {onEditBrush && (
          <button
            onClick={onEditBrush}
            className="w-full text-xs py-2 rounded-lg transition-colors"
            style={{
              background: 'var(--bg-elevated)',
              color: 'var(--text-secondary)',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--accent)')}
            onMouseLeave={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--text-secondary)')}
          >
            Open Brush Editor
          </button>
        )}

        {hasOverride && (
          <button
            onClick={handleRemoveOverride}
            className="w-full text-xs py-2 rounded-lg transition-colors"
            style={{
              background: 'var(--bg-elevated)',
              color: 'var(--text-secondary)',
            }}
            onMouseEnter={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--accent)')}
            onMouseLeave={(e: React.MouseEvent<HTMLButtonElement>) => (e.currentTarget.style.color = 'var(--text-secondary)')}
          >
            Remove override
          </button>
        )}
      </div>
    </motion.div>
  )
}
