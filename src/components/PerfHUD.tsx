/* global __PERF_HUD__ */
import { useState, useEffect, useRef, useCallback } from 'react'
import { perfSubscribe } from '../lib/perf'
import type { PerfSnapshot } from '../types'

interface Position {
  x: number
  y: number
}

function PerfHUDInner() {
  const [snap, setSnap] = useState<PerfSnapshot | null>(null)
  const [visible, setVisible] = useState<boolean>(true)
  const [pos, setPos] = useState<Position>({ x: 12, y: 12 })
  const dragging = useRef<boolean>(false)
  const dragOffset = useRef<Position>({ x: 0, y: 0 })
  const preRef = useRef<HTMLPreElement | null>(null)

  // Subscribe to perf snapshots at 2fps
  useEffect(() => perfSubscribe(setSnap), [])

  // Toggle with Ctrl+Shift+P
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.ctrlKey && e.shiftKey && e.code === 'KeyP') {
        e.preventDefault()
        setVisible((v) => !v)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])

  // Drag via pointer capture
  const onPointerDown = useCallback((e: React.PointerEvent<HTMLPreElement>) => {
    dragging.current = true
    dragOffset.current = { x: e.clientX - pos.x, y: e.clientY - pos.y }
    e.currentTarget.setPointerCapture(e.pointerId)
  }, [pos])

  const onPointerMove = useCallback((e: React.PointerEvent<HTMLPreElement>) => {
    if (!dragging.current) return
    setPos({ x: e.clientX - dragOffset.current.x, y: e.clientY - dragOffset.current.y })
  }, [])

  const onPointerUp = useCallback(() => {
    dragging.current = false
  }, [])

  if (!visible) return null

  const lines: string[] = ['PERF HUD (Ctrl+Shift+P)']

  if (snap) {
    // Channel timing metrics
    const channelLabels: Record<string, string> = {
      'visualizer:frame': 'Canvas frame',
      'audioPlayer:tick': 'audioPlayer',
      'vizBg:tick': 'vizBg',
      'albumArt:tick': 'albumArt',
    }
    for (const [key, label] of Object.entries(channelLabels)) {
      const ch = snap.channels[key]
      if (ch) {
        const avg = ch.avg.toFixed(2)
        const max = ch.max.toFixed(2)
        lines.push(`${label.padEnd(14)} avg:${avg}ms  max:${max}ms`)
      } else {
        lines.push(`${label.padEnd(14)} --`)
      }
    }

    // Frame budget
    if (snap.frameBudget) {
      const avg = snap.frameBudget.avgDelta.toFixed(1)
      const drops = snap.frameBudget.dropsPerSec
      lines.push(`${'Frames'.padEnd(14)} avg:${avg}ms  drops:${drops}/s`)
    }

    // Render counters
    for (const [name, rps] of Object.entries(snap.renders)) {
      lines.push(`${name.padEnd(14)} ${rps} renders/s`)
    }

    // Heap
    if (snap.heap) {
      const used = (snap.heap.used / 1048576).toFixed(1)
      const total = (snap.heap.total / 1048576).toFixed(1)
      lines.push(`Heap ${used}MB / ${total}MB`)
    }
  } else {
    lines.push('Waiting for data...')
  }

  return (
    <pre
      ref={preRef}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      style={{
        position: 'fixed',
        top: pos.y,
        left: pos.x,
        zIndex: 99999,
        background: 'rgba(0, 0, 0, 0.82)',
        color: '#00ff88',
        fontFamily: 'monospace',
        fontSize: 11,
        lineHeight: 1.5,
        padding: '8px 12px',
        borderRadius: 6,
        border: '1px solid rgba(0, 255, 136, 0.2)',
        pointerEvents: 'auto',
        cursor: dragging.current ? 'grabbing' : 'grab',
        userSelect: 'none',
        whiteSpace: 'pre',
        margin: 0,
      }}
    >
      {lines.join('\n')}
    </pre>
  )
}

export default function PerfHUD() {
  if (!__PERF_HUD__) return null
  return <PerfHUDInner />
}
