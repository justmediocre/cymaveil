import type { SegmentationResult, MaskModelParams, MaskPostProcessParams } from '../../types'

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
  /** Depth estimation plus mask post-processing (params merged over DEFAULT_MASK_PARAMS) */
  segment(imageSrc: string, width: number, height: number, params?: Partial<MaskPostProcessParams>): Promise<SegmentationResult | null>
  /** Estimate depth only — returns raw depth map without post-processing */
  estimateDepth?(imageSrc: string, width: number, height: number): Promise<DepthEstimation | null>
  dispose(): void
}
