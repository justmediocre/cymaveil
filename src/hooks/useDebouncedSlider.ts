import { useState, useRef, useCallback, useEffect } from 'react'

export default function useDebouncedSlider<T>(
  storeValue: T,
  commit: (value: T) => void,
  delay = 250,
): [T, (value: T) => void] {
  const [local, setLocal] = useState(storeValue)
  const timerRef = useRef<ReturnType<typeof setTimeout>>(undefined)

  // Sync local state when store changes externally
  useEffect(() => { setLocal(storeValue) }, [storeValue])

  const onChange = useCallback(
    (value: T) => {
      setLocal(value)
      clearTimeout(timerRef.current)
      timerRef.current = setTimeout(() => commit(value), delay)
    },
    [commit, delay],
  )

  // Flush on unmount
  useEffect(() => () => clearTimeout(timerRef.current), [])

  return [local, onChange]
}
