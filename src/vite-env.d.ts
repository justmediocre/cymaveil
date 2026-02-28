/// <reference types="vite/client" />

declare const __PERF_HUD__: boolean
declare const __APP_VERSION__: string

interface Performance {
  memory?: {
    usedJSHeapSize: number
    totalJSHeapSize: number
  }
}
