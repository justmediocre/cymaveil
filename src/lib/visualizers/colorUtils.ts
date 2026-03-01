import type { ParsedColor, FrameStyle } from './types'

const COLOR_PATTERN = /(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/

export function parseColor(colorStr: string | undefined): ParsedColor {
  if (!colorStr) return { r: 200, g: 200, b: 255 }
  if (colorStr.startsWith('#')) return hexToRgb(colorStr)
  const match = colorStr.match(COLOR_PATTERN)
  if (match) {
    return { r: parseInt(match[1]!), g: parseInt(match[2]!), b: parseInt(match[3]!) }
  }
  return { r: 200, g: 200, b: 255 }
}

export function hexToRgb(hex: string): ParsedColor {
  const h = hex.replace('#', '')
  if (h.length === 3) {
    return { r: parseInt(h[0]! + h[0]!, 16), g: parseInt(h[1]! + h[1]!, 16), b: parseInt(h[2]! + h[2]!, 16) }
  }
  return { r: parseInt(h.slice(0, 2), 16), g: parseInt(h.slice(2, 4), 16), b: parseInt(h.slice(4, 6), 16) }
}

export const PRESET_COLORS: Record<string, ParsedColor> = {
  white:   { r: 255, g: 255, b: 255 },
  cyan:    { r: 0,   g: 230, b: 255 },
  magenta: { r: 255, g: 50,  b: 200 },
  gold:    { r: 255, g: 200, b: 50  },
  red:     { r: 255, g: 50,  b: 50  },
  green:   { r: 50,  g: 255, b: 100 },
}

export function resolveBarColor(mode: string, customHex: string, accentColor: string): ParsedColor {
  if (mode === 'auto') return parseColor(accentColor)
  if (mode === 'custom') return parseColor(customHex)
  return PRESET_COLORS[mode] ?? parseColor(accentColor)
}

export function computeFrameStyle(
  intensity: number,
  colorMode: string,
  customColor: string,
  accentColor: string,
  hasDepthMask: boolean
): FrameStyle {
  const intensityNorm = intensity / 100
  const color = resolveBarColor(colorMode, customColor, accentColor)

  const maxC = Math.max(color.r, color.g, color.b, 1)
  const satBoost = 0.3 + intensityNorm * 0.4
  const satR = Math.min(255, color.r + (color.r / maxC) * 255 * satBoost)
  const satG = Math.min(255, color.g + (color.g / maxC) * 255 * satBoost)
  const satB = Math.min(255, color.b + (color.b / maxC) * 255 * satBoost)

  const brighten = 0.15 + intensityNorm * 0.15
  const glowR = Math.round(satR + (255 - satR) * brighten)
  const glowG = Math.round(satG + (255 - satG) * brighten)
  const glowB = Math.round(satB + (255 - satB) * brighten)

  const coreBrighten = 0.3 + intensityNorm * 0.3
  const coreR = Math.round(glowR + (255 - glowR) * coreBrighten)
  const coreG = Math.round(glowG + (255 - glowG) * coreBrighten)
  const coreB = Math.round(glowB + (255 - glowB) * coreBrighten)

  const glowAlphaMul = 0.3 + intensityNorm * 0.7
  const coreAlphaMul = 0.4 + intensityNorm * 0.6

  const padX = hasDepthMask ? 0.06 : 0
  const padBot = hasDepthMask ? 0.04 : 0

  return { glowR, glowG, glowB, coreR, coreG, coreB, glowAlphaMul, coreAlphaMul, padX, padBot }
}
