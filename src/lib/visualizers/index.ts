import type { VisualizerStyle } from '../../types'
import type { VisualizerRenderer } from './types'
import { createContourBarsRenderer } from './contourBars'
import { createFullSurfaceRenderer } from './fullSurface'
import { createRadialBurstRenderer } from './radialBurst'
import { createWaveformRenderer } from './waveform'
import { createMirroredBarsRenderer } from './mirroredBars'

export type { VisualizerRenderer, RenderContext, FrameStyle } from './types'
export { computeFrameStyle } from './colorUtils'

/** Concrete (non-random) styles that can be instantiated */
export const CONCRETE_STYLES: Exclude<VisualizerStyle, 'random'>[] = [
  'contour-bars', 'full-surface', 'radial-burst', 'waveform', 'mirrored-bars'
]

export function createRenderer(style: Exclude<VisualizerStyle, 'random'>): VisualizerRenderer {
  switch (style) {
    case 'contour-bars': return createContourBarsRenderer()
    case 'full-surface': return createFullSurfaceRenderer()
    case 'radial-burst': return createRadialBurstRenderer()
    case 'waveform': return createWaveformRenderer()
    case 'mirrored-bars': return createMirroredBarsRenderer()
  }
}
