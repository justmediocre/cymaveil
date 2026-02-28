import { useSyncExternalStore } from 'react'

interface BatteryManager extends EventTarget {
  charging: boolean
  chargingTime: number
  dischargingTime: number
  level: number
  addEventListener(type: 'chargingchange' | 'chargingtimechange' | 'dischargingtimechange' | 'levelchange', listener: () => void): void
}

interface NavigatorWithBattery extends Navigator {
  getBattery?: () => Promise<BatteryManager>
}

let battery: BatteryManager | null = null
let onBattery = false
const listeners = new Set<() => void>()

function notify() {
  const was = onBattery
  onBattery = battery ? !battery.charging : false
  if (was !== onBattery) listeners.forEach((l) => l())
}

// Init once — navigator.getBattery() is Chromium/Electron only
if (typeof navigator !== 'undefined' && (navigator as NavigatorWithBattery).getBattery) {
  (navigator as NavigatorWithBattery).getBattery!().then((b) => {
    battery = b
    notify()
    b.addEventListener('chargingchange', notify)
  }).catch(() => { /* getBattery() unsupported on this hardware/OS */ })
}

export function useBatteryOnBattery(): boolean {
  return useSyncExternalStore(
    (l) => { listeners.add(l); return () => { listeners.delete(l) } },
    () => onBattery,
  )
}
