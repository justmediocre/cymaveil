import { useRef, useCallback } from 'react'

const DOUBLE_CLICK_DELAY = 250

/**
 * Returns a single click handler that discriminates between single and double clicks.
 * Single click fires after a 250ms delay (cancelled if a second click arrives).
 * Double click fires immediately on the second click.
 */
export function useClickHandler(
  onSingle: (index: number) => void,
  onDouble: (index: number) => void,
) {
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const pendingIndexRef = useRef<number>(-1)

  return useCallback(
    (index: number) => {
      if (timerRef.current !== null && pendingIndexRef.current === index) {
        // Second click on same item — double click
        clearTimeout(timerRef.current)
        timerRef.current = null
        onDouble(index)
      } else {
        // First click (or different item) — start timer
        if (timerRef.current !== null) {
          clearTimeout(timerRef.current)
        }
        pendingIndexRef.current = index
        timerRef.current = setTimeout(() => {
          timerRef.current = null
          onSingle(index)
        }, DOUBLE_CLICK_DELAY)
      }
    },
    [onSingle, onDouble],
  )
}
