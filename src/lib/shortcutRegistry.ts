// Centralized keyboard shortcut registry — no React dependency.
// A single window 'keydown' listener dispatches to registered handlers
// in priority order (highest first). First match wins.

const INPUT_SELECTOR = 'input, textarea, select, [contenteditable="true"]'
const INTERACTIVE_SELECTOR =
  'input, textarea, select, button, a, summary, [contenteditable="true"], [role="button"], [role="checkbox"], [role="switch"], [role="slider"], [role="tab"], [role="menuitem"], [role="option"]'

// --- Shortcut parsing ---

interface ParsedShortcut {
  ctrl: boolean
  shift: boolean
  alt: boolean
  meta: boolean
  /** `mod` means "ctrl OR meta" — platform command key. */
  mod: boolean
  /** The matching strategy for the key portion. */
  key: { type: 'code'; value: string } | { type: 'key'; value: string }
}

const LETTER_RE = /^[a-z]$/
const DIGIT_RE = /^[0-9]$/

const SPECIAL_KEY_MAP: Record<string, string> = {
  space: ' ',
  escape: 'Escape',
  tab: 'Tab',
  enter: 'Enter',
  arrowleft: 'ArrowLeft',
  arrowright: 'ArrowRight',
  arrowup: 'ArrowUp',
  arrowdown: 'ArrowDown',
  f1: 'F1', f2: 'F2', f3: 'F3', f4: 'F4',
  f5: 'F5', f6: 'F6', f7: 'F7', f8: 'F8',
  f9: 'F9', f10: 'F10', f11: 'F11', f12: 'F12',
}

export function parseShortcut(shortcut: string): ParsedShortcut {
  const parts = shortcut.toLowerCase().split('+')
  let ctrl = false
  let shift = false
  let alt = false
  let meta = false
  let mod = false
  const keyPart = parts.pop()!

  for (const p of parts) {
    switch (p) {
      case 'ctrl': ctrl = true; break
      case 'shift': shift = true; break
      case 'alt': alt = true; break
      case 'meta': meta = true; break
      case 'mod': mod = true; break
    }
  }

  let key: ParsedShortcut['key']
  if (LETTER_RE.test(keyPart)) {
    // Letters → match via e.code (layout-independent)
    key = { type: 'code', value: `Key${keyPart.toUpperCase()}` }
  } else if (DIGIT_RE.test(keyPart)) {
    key = { type: 'code', value: `Digit${keyPart}` }
  } else if (SPECIAL_KEY_MAP[keyPart]) {
    key = { type: 'key', value: SPECIAL_KEY_MAP[keyPart]! }
  } else {
    // Symbols like [ ] etc. — match on e.key
    key = { type: 'key', value: keyPart }
  }

  return { ctrl, shift, alt, meta, mod, key }
}

// --- Registration ---

export interface Registration {
  id: number
  parsed: ParsedShortcut
  handler: () => void
  priority: number
  skipInput: boolean
  skipInteractive: boolean
  preventDefault: boolean
}

let nextId = 1
const registrations: Registration[] = []

function matches(parsed: ParsedShortcut, e: KeyboardEvent): boolean {
  // Key match
  if (parsed.key.type === 'code') {
    if (e.code !== parsed.key.value) return false
  } else {
    if (e.key !== parsed.key.value) return false
  }

  // Modifier matching
  const expectCtrl = parsed.ctrl || parsed.mod
  const expectMeta = parsed.meta || parsed.mod

  if (parsed.mod) {
    // mod: at least one of ctrl/meta must be true
    if (!e.ctrlKey && !e.metaKey) return false
  } else {
    // Exact: ctrl must match exactly
    if (parsed.ctrl !== e.ctrlKey) return false
    if (parsed.meta !== e.metaKey) return false
  }

  // For non-mod shortcuts, ensure no extra ctrl/meta
  if (!parsed.mod) {
    // Already checked above
  } else {
    // mod is active — we don't care which of ctrl/meta is pressed,
    // but we need to ensure the *other* modifiers match exactly.
    // ctrl and meta are covered by mod, so skip them.
  }

  if (parsed.shift !== e.shiftKey) return false
  if (parsed.alt !== e.altKey) return false

  return true
}

function dispatch(e: KeyboardEvent) {
  for (const reg of registrations) {
    if (reg.skipInteractive) {
      const el = e.target as HTMLElement
      if (el?.closest?.(INTERACTIVE_SELECTOR)) continue
    } else if (reg.skipInput) {
      const el = e.target as HTMLElement
      if (el?.closest?.(INPUT_SELECTOR)) continue
    }

    if (matches(reg.parsed, e)) {
      if (reg.preventDefault) e.preventDefault()
      reg.handler()
      return
    }
  }
}

let listenerInstalled = false

function ensureListener() {
  if (listenerInstalled) return
  window.addEventListener('keydown', dispatch)
  listenerInstalled = true
}

function removeListenerIfEmpty() {
  if (registrations.length === 0 && listenerInstalled) {
    window.removeEventListener('keydown', dispatch)
    listenerInstalled = false
  }
}

export function register(reg: Omit<Registration, 'id'>): number {
  const id = nextId++
  const entry: Registration = { ...reg, id }

  // Insert sorted by priority descending (stable: new entries go after existing same-priority)
  let i = 0
  while (i < registrations.length && registrations[i]!.priority > entry.priority) {
    i++
  }
  registrations.splice(i, 0, entry)

  ensureListener()
  return id
}

export function unregister(id: number): void {
  const idx = registrations.findIndex((r) => r.id === id)
  if (idx !== -1) registrations.splice(idx, 1)
  removeListenerIfEmpty()
}
