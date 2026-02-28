import { Component, type ReactNode, type ErrorInfo } from 'react'

interface ErrorBoundaryProps {
  children: ReactNode
  fallback?: (error: Error, reset: () => void) => ReactNode
  onReset?: () => void
}

interface ErrorBoundaryState {
  error: Error | null
}

export default class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  state: ErrorBoundaryState = { error: null }

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    return { error }
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error('[ErrorBoundary]', error, info.componentStack)
  }

  resetErrorBoundary = () => {
    this.props.onReset?.()
    this.setState({ error: null })
  }

  render() {
    if (this.state.error) {
      if (this.props.fallback) {
        return this.props.fallback(this.state.error, this.resetErrorBoundary)
      }
      return <DefaultFallback error={this.state.error} onReset={this.resetErrorBoundary} />
    }
    return this.props.children
  }
}

function DefaultFallback({ error, onReset }: { error: Error; onReset: () => void }) {
  return (
    <div style={{
      display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
      height: '100%', padding: '2rem', color: 'var(--text-secondary, rgba(255,255,255,0.5))',
      fontFamily: 'var(--font-sans, system-ui, sans-serif)',
    }}>
      <p style={{ fontSize: '0.875rem', marginBottom: '1rem' }}>Something went wrong</p>
      <button
        onClick={onReset}
        style={{
          padding: '0.5rem 1rem', borderRadius: '0.5rem', border: 'none', cursor: 'pointer',
          background: 'rgba(255,255,255,0.1)', color: 'var(--text-primary, rgba(255,255,255,0.9))',
          fontSize: '0.8125rem',
        }}
      >
        Try Again
      </button>
      {import.meta.env.DEV && (
        <details style={{ marginTop: '1rem', maxWidth: '32rem', fontSize: '0.75rem', opacity: 0.6 }}>
          <summary style={{ cursor: 'pointer' }}>Error details</summary>
          <pre style={{ whiteSpace: 'pre-wrap', marginTop: '0.5rem' }}>{error.message}{'\n'}{error.stack}</pre>
        </details>
      )}
    </div>
  )
}
