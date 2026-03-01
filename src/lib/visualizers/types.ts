import type { ContourData } from '../../types'

export interface ParsedColor {
  r: number
  g: number
  b: number
}

export interface FrameStyle {
  glowR: number
  glowG: number
  glowB: number
  coreR: number
  coreG: number
  coreB: number
  glowAlphaMul: number
  coreAlphaMul: number
  padX: number
  padBot: number
}

export interface RenderContext {
  ctx: CanvasRenderingContext2D
  w: number
  h: number
  dataArray: Uint8Array
  timeDomainArray: Uint8Array | null
  style: FrameStyle
  intensity: number
}

export interface VisualizerRenderer {
  readonly name: string
  readonly needsTimeDomain: boolean
  readonly needsContourData: boolean
  smoothedSize: number

  init(w: number, h: number, contourData?: ContourData | null): void
  render(rc: RenderContext, smoothed: Float32Array): void
  dispose(): void
}
