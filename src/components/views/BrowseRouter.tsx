import LibraryView from './LibraryView'
import AlbumsView from './AlbumsView'
import AlbumDetailView from './AlbumDetailView'
import SearchView from './SearchView'
import PlaylistsView from './PlaylistsView'
import PlaylistDetailView from './PlaylistDetailView'
import SettingsView from './SettingsView'
import EmptyState from '../EmptyState'

interface BrowseRouterProps {
  activeNav: string
  selectedAlbumId: string | null
  selectedPlaylistId: string | null
  onAlbumSelect: (albumId: string) => void
  onPlaylistSelect: (playlistId: string) => void
  onBackToAlbums: () => void
  onBackToPlaylists: () => void
  onBackToLibrary: () => void
  onNavigateToNowPlaying: () => void
  onSetActiveNav: (nav: string) => void
  onProcessAll: () => void
  batchProcessing: boolean
}

export default function BrowseRouter({
  activeNav,
  selectedAlbumId,
  selectedPlaylistId,
  onAlbumSelect,
  onPlaylistSelect,
  onBackToAlbums,
  onBackToPlaylists,
  onBackToLibrary,
  onNavigateToNowPlaying,
  onSetActiveNav,
  onProcessAll,
  batchProcessing,
}: BrowseRouterProps) {
  switch (activeNav) {
    case 'Library':
      return <LibraryView onNavigateToNowPlaying={onNavigateToNowPlaying} />
    case 'Albums':
      return <AlbumsView onAlbumSelect={onAlbumSelect} onNavigateToNowPlaying={onNavigateToNowPlaying} />
    case 'AlbumDetail':
      return (
        <AlbumDetailView
          albumId={selectedAlbumId}
          onBack={onBackToAlbums}
          onNavigateToNowPlaying={onNavigateToNowPlaying}
        />
      )
    case 'Search':
      return <SearchView onAlbumSelect={onAlbumSelect} />
    case 'Favorites':
      return (
        <PlaylistDetailView
          playlistId="favorites"
          onBack={onBackToLibrary}
          onNavigateToNowPlaying={onNavigateToNowPlaying}
        />
      )
    case 'Playlists':
      return <PlaylistsView onPlaylistSelect={onPlaylistSelect} />
    case 'PlaylistDetail':
      return (
        <PlaylistDetailView
          playlistId={selectedPlaylistId!}
          onBack={onBackToPlaylists}
          onNavigateToNowPlaying={onNavigateToNowPlaying}
          onDeleteNavigate={() => onSetActiveNav('Playlists')}
        />
      )
    case 'NowPlaying':
      return (
        <EmptyState
          icon="music"
          title="No track playing"
          subtitle="Play a track to see it here"
        />
      )
    case 'Settings':
      return <SettingsView onProcessAll={onProcessAll} batchProcessing={batchProcessing} />
    default:
      return (
        <EmptyState
          icon="disc"
          title={`${activeNav}`}
          subtitle="This section is coming soon"
        />
      )
  }
}
