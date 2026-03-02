import { useState, useEffect, useCallback, useMemo, useRef, memo } from 'react'
import useVisualSettings from '../hooks/useVisualSettings'
import { cmdOrCtrl } from '../lib/keyboard'
import type { Album, MosaicTransition } from '../types'

interface AlbumArtBackgroundProps {
  albums: Album[]
  isPlaying: boolean
}

type ConcreteTransition = Exclude<MosaicTransition, 'random'>

const CONCRETE_TRANSITIONS: ConcreteTransition[] = ['flip', 'shrink-grow', 'cross-fade', 'fade', 'iris']

interface TileData {
  frontArt: string
  backArt: string
  active: boolean
  transition: ConcreteTransition
}

/** Batch size for staggered initial mount — tiles revealed per rAF frame */
const MOUNT_BATCH = 12

function computeGridSize(columns: number): number {
  return columns * Math.ceil(columns * 1.5)
}

/** Pick a random index from `indices` weighted toward the vignette center (0.5, 0.6). */
function weightedCenterPick(indices: number[], columns: number, rows: number): number {
  let totalWeight = 0
  const weights = new Float64Array(indices.length)
  for (let j = 0; j < indices.length; j++) {
    const i = indices[j]!
    const nx = columns > 1 ? (i % columns) / (columns - 1) : 0.5
    const ny = rows > 1 ? Math.floor(i / columns) / (rows - 1) : 0.5
    const dx = nx - 0.5
    const dy = ny - 0.6
    const dist = Math.sqrt(dx * dx + dy * dy)
    const w = (1 - Math.min(dist / 0.8, 1)) ** 2
    weights[j] = w
    totalWeight += w
  }
  // Fallback to uniform if all weights are ~zero (shouldn't happen)
  if (totalWeight <= 0) return indices[Math.floor(Math.random() * indices.length)]!
  const r = Math.random() * totalWeight
  let acc = 0
  for (let j = 0; j < weights.length; j++) {
    acc += weights[j]!
    if (r <= acc) return indices[j]!
  }
  return indices[indices.length - 1]!
}

const AnimatedTile = memo(function AnimatedTile({
  index,
  frontArt,
  backArt,
  active,
  transition,
  hidden,
  onComplete,
}: {
  index: number
  frontArt: string
  backArt: string
  active: boolean
  transition: ConcreteTransition
  hidden: boolean
  onComplete: (index: number) => void
}) {
  const innerRef = useRef<HTMLDivElement>(null)
  const wasActive = useRef(false)
  const completedRef = useRef(false)

  // When resetting from active→inactive, suppress transitions/animations so it snaps instantly
  useEffect(() => {
    if (wasActive.current && !active && innerRef.current) {
      const el = innerRef.current
      el.style.transition = 'none'
      el.style.animation = 'none'
      const faces = el.querySelectorAll<HTMLElement>('.album-art-bg-tile-face')
      faces.forEach((f) => {
        f.style.transition = 'none'
        f.style.animation = 'none'
      })
      // Force reflow so the browser applies the snap
      el.offsetHeight
      requestAnimationFrame(() => {
        el.style.transition = ''
        el.style.animation = ''
        faces.forEach((f) => {
          f.style.transition = ''
          f.style.animation = ''
        })
      })
    }
    if (active && !wasActive.current) {
      completedRef.current = false
    }
    wasActive.current = active
  }, [active])

  const handleComplete = useCallback(() => {
    if (!completedRef.current) {
      completedRef.current = true
      onComplete(index)
    }
  }, [onComplete, index])

  return (
    <div
      className={`album-art-bg-tile tile-${transition}`}
      style={hidden ? { display: 'none' } : undefined}
    >
      <div
        ref={innerRef}
        className={`album-art-bg-tile-inner${active ? ' active' : ''}`}
        onTransitionEnd={handleComplete}
        onAnimationEnd={handleComplete}
      >
        <div className="album-art-bg-tile-face album-art-bg-tile-front">
          <img src={frontArt} alt="" draggable={false} loading="lazy" />
        </div>
        <div className="album-art-bg-tile-face album-art-bg-tile-back">
          {backArt && <img src={backArt} alt="" draggable={false} loading="lazy" />}
        </div>
      </div>
    </div>
  )
})

export default function AlbumArtBackground({ albums, isPlaying }: AlbumArtBackgroundProps) {
  const { settings } = useVisualSettings()
  const columns = settings.mosaicDensity
  const maxTiles = settings.mosaicMaxTiles

  // poolSize = total allocated tiles (rarely changes)
  const poolSize = maxTiles
  // visibleCount = tiles shown for current density (changes with slider)
  const visibleCount = useMemo(() => Math.min(computeGridSize(columns), maxTiles), [columns, maxTiles])

  const allArts = useMemo(() => {
    const result: string[] = []
    for (const a of albums) {
      if (a.art && !a.art.startsWith('data:image/svg')) result.push(a.art)
    }
    return result
  }, [albums])

  const [tiles, setTiles] = useState<TileData[]>([])

  // Pool builder: keeps existing tile references, only creates new objects for new indices
  useEffect(() => {
    if (allArts.length === 0) return
    setTiles((prev) => {
      if (prev.length === poolSize) return prev
      const result: TileData[] = []
      for (let i = 0; i < poolSize; i++) {
        if (i < prev.length) {
          // Reuse existing reference — preserves memo()
          result.push(prev[i]!)
        } else {
          result.push({
            frontArt: allArts[i % allArts.length]!,
            backArt: '',
            active: false,
            transition: 'flip',
          })
        }
      }
      return result
    })
  }, [allArts, poolSize])

  // Staggered initial mount: reveal tiles in batches via rAF
  const [mountedCount, setMountedCount] = useState(0)
  const mountedRef = useRef(0)

  useEffect(() => {
    if (poolSize === 0) return
    // Reset if pool grew beyond what we've mounted
    if (mountedRef.current >= poolSize) return

    let rafId: number

    const step = () => {
      const next = Math.min(mountedRef.current + MOUNT_BATCH, poolSize)
      mountedRef.current = next
      setMountedCount(next)
      if (next < poolSize) {
        rafId = requestAnimationFrame(step)
      }
    }

    rafId = requestAnimationFrame(step)
    return () => cancelAnimationFrame(rafId)
  }, [poolSize])

  // Reset hidden active tiles when visibleCount shrinks
  const visibleCountRef = useRef(visibleCount)
  useEffect(() => {
    const prevVisible = visibleCountRef.current
    visibleCountRef.current = visibleCount
    if (visibleCount >= prevVisible) return

    setTiles((prev) => {
      let changed = false
      const next = prev.map((tile, i) => {
        if (i >= visibleCount && tile.active) {
          changed = true
          return {
            frontArt: tile.backArt || tile.frontArt,
            backArt: '',
            active: false,
            transition: tile.transition,
          }
        }
        return tile
      })
      return changed ? next : prev
    })
  }, [visibleCount])

  // Animate a random visible tile to a new artwork
  const mosaicTransition = settings.mosaicTransition
  const animateRandomTile = useCallback(() => {
    if (allArts.length < 2) return
    setTiles((prev) => {
      const available: number[] = []
      for (let i = 0; i < Math.min(prev.length, visibleCount); i++) {
        if (!prev[i]!.active) available.push(i)
      }
      if (available.length === 0) return prev

      const rows = Math.ceil(visibleCount / columns)
      const idx = weightedCenterPick(available, columns, rows)
      const tile = prev[idx]!

      let newArt: string
      do {
        newArt = allArts[Math.floor(Math.random() * allArts.length)]!
      } while (newArt === tile.frontArt)

      const transition: ConcreteTransition =
        mosaicTransition === 'random'
          ? CONCRETE_TRANSITIONS[Math.floor(Math.random() * CONCRETE_TRANSITIONS.length)]!
          : mosaicTransition

      const next = [...prev]
      next[idx] = { ...tile, backArt: newArt, active: true, transition }
      return next
    })
  }, [allArts, columns, mosaicTransition, visibleCount])

  // Periodic tile animation while playing — depends on visibleCount, not tiles.length
  useEffect(() => {
    if (!isPlaying || allArts.length < 2 || visibleCount === 0) return

    let timerId: ReturnType<typeof setTimeout>

    const scheduleNext = () => {
      timerId = setTimeout(
        () => {
          animateRandomTile()
          scheduleNext()
        },
        2500 + Math.random() * 2500,
      )
    }

    scheduleNext()
    return () => clearTimeout(timerId)
  }, [isPlaying, allArts, visibleCount, animateRandomTile])

  // Hidden hotkey: Ctrl+Shift+B to manually animate a tile
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (cmdOrCtrl(e) && e.shiftKey && e.key === 'B') {
        animateRandomTile()
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [animateRandomTile])

  // Reset tile after animation completes
  const handleAnimationComplete = useCallback((idx: number) => {
    setTiles((prev) => {
      const next = [...prev]
      const tile = next[idx]!
      if (!tile.active) return prev
      next[idx] = {
        frontArt: tile.backArt,
        backArt: '',
        active: false,
        transition: tile.transition,
      }
      return next
    })
  }, [])

  if (tiles.length === 0 || !settings.backgroundMosaic) return null

  return (
    <div
      className="fixed inset-0 overflow-hidden pointer-events-none"
      style={{ zIndex: 0, perspective: settings.mosaicFlat ? undefined : '1200px' }}
    >
      {/* Isometric/flat wrapper */}
      <div className={settings.mosaicFlat ? 'album-art-bg-flat' : 'album-art-bg-iso'}>
        <div
          className="album-art-bg-grid"
          style={{
            gridTemplateColumns: `repeat(${columns}, 1fr)`,
            opacity: settings.mosaicOpacity / 100,
            animationPlayState: isPlaying ? 'running' : 'paused',
          }}
        >
          {tiles.map((tile, i) =>
            i >= mountedCount ? null : (
              <AnimatedTile
                key={i}
                index={i}
                frontArt={tile.frontArt}
                backArt={tile.backArt}
                active={tile.active}
                transition={tile.transition}
                hidden={i >= visibleCount}
                onComplete={handleAnimationComplete}
              />
            ),
          )}
        </div>
      </div>
      {/* Vignette overlay for depth */}
      <div
        className="absolute inset-0"
        style={{
          background:
            'radial-gradient(ellipse at 50% 60%, transparent 10%, var(--bg-primary) 70%)',
        }}
      />
    </div>
  )
}
