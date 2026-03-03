import { useEffect, useRef } from 'react'
import { parseShortcut, register, unregister } from '../lib/shortcutRegistry'

interface UseShortcutOptions {
  /** Higher priority = checked first. Default 0. */
  priority?: number
  /** When false, shortcut is not registered. Default true. */
  enabled?: boolean
  /** Skip when focused on input/textarea/select/[contenteditable]. Default false. */
  skipInput?: boolean
  /** Skip when focused on any interactive element (superset of skipInput). Default false. */
  skipInteractive?: boolean
  /** Call e.preventDefault() on match. Default true. */
  preventDefault?: boolean
}

export default function useShortcut(
  shortcut: string,
  handler: () => void,
  options?: UseShortcutOptions,
): void {
  const handlerRef = useRef(handler)
  handlerRef.current = handler

  const priority = options?.priority ?? 0
  const enabled = options?.enabled ?? true
  const skipInput = options?.skipInput ?? false
  const skipInteractive = options?.skipInteractive ?? false
  const preventDefault = options?.preventDefault ?? true

  useEffect(() => {
    if (!enabled) return

    const parsed = parseShortcut(shortcut)
    const id = register({
      parsed,
      handler: () => handlerRef.current(),
      priority,
      skipInput,
      skipInteractive,
      preventDefault,
    })

    return () => unregister(id)
  }, [shortcut, enabled, priority, skipInput, skipInteractive, preventDefault])
}
