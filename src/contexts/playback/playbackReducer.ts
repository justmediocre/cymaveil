import type { Track } from '../../types'

/** Fisher-Yates shuffle — returns a new array */
export function shuffleArray<T>(arr: T[]): T[] {
  const a = arr.slice()
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1))
    ;[a[i], a[j]] = [a[j]!, a[i]!]
  }
  return a
}

export interface PlaybackState {
  currentTrackIndex: number
  shuffle: boolean
  repeat: 'off' | 'all' | 'one'
  volume: number
  playQueue: string[]       // track IDs
  queueIndex: number        // -1 = queue inactive
  isReady: boolean
}

export type PlaybackAction =
  | { type: 'SET_TRACK_INDEX'; index: number }
  | { type: 'NEXT'; tracksLength: number }
  | { type: 'PREV'; tracksLength: number }
  | { type: 'SET_VOLUME'; volume: number }
  | { type: 'TOGGLE_SHUFFLE'; albumTrackIds: string[]; currentTrackId?: string; tracks: Track[] }
  | { type: 'CYCLE_REPEAT' }
  | { type: 'SET_QUEUE'; queue: string[]; index: number; shuffle?: boolean }
  | { type: 'SET_QUEUE_INDEX'; index: number }
  | { type: 'DEACTIVATE_QUEUE'; trackId?: string; tracks: Track[] }
  | { type: 'RESTORE'; patch: Partial<PlaybackState> }
  | { type: 'SET_READY' }
  | { type: 'CLAMP_INDEX'; tracksLength: number }

export const initialPlaybackState: PlaybackState = {
  currentTrackIndex: 0,
  shuffle: false,
  repeat: 'off',
  volume: (() => {
    if (typeof localStorage === 'undefined') return 75
    const stored = localStorage.getItem('volume')
    if (stored === null) return 75
    const raw = Number(stored)
    return Number.isFinite(raw) ? Math.max(0, Math.min(100, raw)) : 75
  })(),
  playQueue: [],
  queueIndex: -1,
  isReady: false,
}

export function playbackReducer(state: PlaybackState, action: PlaybackAction): PlaybackState {
  switch (action.type) {
    case 'SET_TRACK_INDEX':
      return { ...state, currentTrackIndex: action.index }

    case 'NEXT': {
      const queueActive = state.queueIndex >= 0 && state.playQueue.length > 0
      // repeat: 'one' is handled outside the reducer (imperative seek+resume)
      if (queueActive) {
        if (state.queueIndex < state.playQueue.length - 1) {
          return { ...state, queueIndex: state.queueIndex + 1 }
        } else if (state.repeat === 'all') {
          return { ...state, queueIndex: 0 }
        }
        // queue exhausted, stop
        return state
      }
      return {
        ...state,
        currentTrackIndex: (state.currentTrackIndex + 1) % action.tracksLength,
      }
    }

    case 'PREV': {
      const queueActive = state.queueIndex >= 0 && state.playQueue.length > 0
      if (queueActive) {
        if (state.queueIndex > 0) {
          return { ...state, queueIndex: state.queueIndex - 1 }
        } else if (state.repeat === 'all') {
          return { ...state, queueIndex: state.playQueue.length - 1 }
        }
        return state
      }
      return {
        ...state,
        currentTrackIndex: (state.currentTrackIndex - 1 + action.tracksLength) % action.tracksLength,
      }
    }

    case 'SET_VOLUME':
      return { ...state, volume: action.volume }

    case 'TOGGLE_SHUFFLE': {
      if (state.shuffle) {
        // Turn OFF: deactivate queue
        const globalIndex = action.tracks.findIndex((t) => t.id === action.currentTrackId)
        return {
          ...state,
          shuffle: false,
          playQueue: [],
          queueIndex: -1,
          ...(globalIndex >= 0 ? { currentTrackIndex: globalIndex } : {}),
        }
      }
      // Turn ON: shuffle the album tracks, starting with current track
      if (action.albumTrackIds.length === 0) return state
      const otherIds = action.albumTrackIds.filter((id) => id !== action.currentTrackId)
      const shuffled = [action.currentTrackId!, ...shuffleArray(otherIds)]
      return {
        ...state,
        shuffle: true,
        playQueue: shuffled,
        queueIndex: 0,
      }
    }

    case 'CYCLE_REPEAT': {
      const next = state.repeat === 'off' ? 'all' : state.repeat === 'all' ? 'one' : 'off'
      return { ...state, repeat: next }
    }

    case 'SET_QUEUE':
      return {
        ...state,
        playQueue: action.queue,
        queueIndex: action.index,
        shuffle: action.shuffle ?? state.shuffle,
      }

    case 'SET_QUEUE_INDEX':
      return { ...state, queueIndex: action.index }

    case 'DEACTIVATE_QUEUE': {
      let newIndex = state.currentTrackIndex
      if (action.trackId) {
        const idx = action.tracks.findIndex((t) => t.id === action.trackId)
        if (idx >= 0) newIndex = idx
      }
      return {
        ...state,
        playQueue: [],
        queueIndex: -1,
        shuffle: false,
        currentTrackIndex: newIndex,
      }
    }

    case 'RESTORE':
      return { ...state, ...action.patch }

    case 'SET_READY':
      return { ...state, isReady: true }

    case 'CLAMP_INDEX':
      if (state.currentTrackIndex >= action.tracksLength) {
        return { ...state, currentTrackIndex: 0 }
      }
      return state

    default:
      return state
  }
}
