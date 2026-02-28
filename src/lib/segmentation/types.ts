import type { SegmentationResult, MaskModelParams } from '../../types'

/** Raw depth estimation output (before post-processing) */
export interface DepthEstimation {
  depthMap: Uint8Array
  width: number
  height: number
}

/** Interface that every segmentation backend must implement */
export interface SegmentationBackendModule {
  readonly name: string
  readonly modelSize: string
  isLoaded(): boolean
  load(onProgress?: (progress: number) => void): Promise<void>
  /** Load with specific model parameters (disposes and reloads if config changes) */
  loadWithParams?(params: MaskModelParams, onProgress?: (progress: number) => void): Promise<void>
  segment(imageSrc: string, width: number, height: number): Promise<SegmentationResult | null>
  /** Estimate depth only — returns raw depth map without post-processing */
  estimateDepth?(imageSrc: string, width: number, height: number): Promise<DepthEstimation | null>
  dispose(): void
}
