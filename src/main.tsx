import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import '@fontsource-variable/outfit'
import '@fontsource-variable/bricolage-grotesque'
import '@fontsource-variable/jetbrains-mono'
import App from './App'
import ErrorBoundary from './components/ErrorBoundary'
import './index.css'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <ErrorBoundary fallback={(error) => (
      <div style={{
        display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
        height: '100vh', background: '#0a0a0b', color: 'rgba(255,255,255,0.5)',
        fontFamily: 'system-ui, sans-serif',
      }}>
        <p style={{ fontSize: '0.875rem', marginBottom: '1rem' }}>The app encountered a fatal error</p>
        <button
          onClick={() => window.location.reload()}
          style={{
            padding: '0.5rem 1.25rem', borderRadius: '0.5rem', border: 'none', cursor: 'pointer',
            background: 'rgba(255,255,255,0.1)', color: 'rgba(255,255,255,0.9)', fontSize: '0.8125rem',
          }}
        >
          Reload
        </button>
        {import.meta.env.DEV && (
          <details style={{ marginTop: '1rem', maxWidth: '32rem', fontSize: '0.75rem', opacity: 0.6 }}>
            <summary style={{ cursor: 'pointer' }}>Error details</summary>
            <pre style={{ whiteSpace: 'pre-wrap', marginTop: '0.5rem', color: 'rgba(255,255,255,0.4)' }}>
              {error.message}{'\n'}{error.stack}
            </pre>
          </details>
        )}
      </div>
    )}>
      <App />
    </ErrorBoundary>
  </StrictMode>
)
