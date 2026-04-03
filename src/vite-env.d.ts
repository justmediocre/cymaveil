/// <reference types="vite/client" />

declare module '@fontsource-variable/outfit'
declare module '@fontsource-variable/bricolage-grotesque'
declare module '@fontsource-variable/jetbrains-mono'

declare const __PERF_HUD__: boolean
declare const __APP_VERSION__: string

interface Performance {
  memory?: {
    usedJSHeapSize: number
    totalJSHeapSize: number
  }
}
