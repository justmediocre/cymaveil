/** ITU-R BT.601 luminance from linear RGB values (0–255 range). */
export function luminance(r: number, g: number, b: number): number {
  return 0.299 * r + 0.587 * g + 0.114 * b
}
