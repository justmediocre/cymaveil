import type { ReactNode } from 'react'

/* ── SettingRow ── */

interface SettingRowProps {
  label: string
  description?: string
  badge?: ReactNode
  disabled?: boolean
  compact?: boolean
  children: ReactNode
}

export function SettingRow({ label, description, badge, disabled, compact, children }: SettingRowProps) {
  return (
    <div
      className="flex items-center justify-between px-4 rounded-xl transition-colors"
      style={{
        paddingTop: compact ? '0.625rem' : '0.75rem',
        paddingBottom: compact ? '0.625rem' : '0.75rem',
        background: 'transparent',
        opacity: disabled ? 0.5 : 1,
        pointerEvents: disabled ? 'none' : 'auto',
      }}
      onMouseEnter={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'var(--bg-hover)')}
      onMouseLeave={(e: React.MouseEvent<HTMLDivElement>) => (e.currentTarget.style.background = 'transparent')}
    >
      <div className="flex flex-col gap-0.5 min-w-0 mr-4">
        {badge ? (
          <div className="flex items-center gap-2">
            <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
              {label}
            </span>
            {badge}
          </div>
        ) : (
          <span className="text-sm font-medium" style={{ color: 'var(--text-primary)' }}>
            {label}
          </span>
        )}
        {description && (
          <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
            {description}
          </span>
        )}
      </div>

      {children}
    </div>
  )
}

/* ── SettingToggle ── */

interface SettingToggleProps {
  label: string
  description?: string
  badge?: ReactNode
  disabled?: boolean
  enabled: boolean
  onToggle: () => void
}

export function SettingToggle({ label, description, badge, disabled, enabled, onToggle }: SettingToggleProps) {
  return (
    <SettingRow label={label} description={description} badge={badge} disabled={disabled}>
      <button
        onClick={onToggle}
        className="shrink-0 relative rounded-full transition-colors duration-200"
        style={{
          width: 40,
          height: 22,
          background: enabled ? 'var(--accent)' : 'var(--bg-elevated)',
          border: `1px solid ${enabled ? 'var(--accent)' : 'var(--border)'}`,
        }}
        aria-label={`Toggle ${label}`}
        role="switch"
        aria-checked={enabled}
      >
        <span
          className="absolute top-[2px] rounded-full transition-all duration-200"
          style={{
            width: 16,
            height: 16,
            background: enabled ? '#fff' : 'var(--text-tertiary)',
            left: enabled ? 20 : 2,
          }}
        />
      </button>
    </SettingRow>
  )
}

/* ── SettingSlider ── */

interface SettingSliderProps {
  label: string
  description?: string
  disabled?: boolean
  compact?: boolean
  value: number
  onChange: (value: number) => void
  min: number
  max: number
  step: number
  format?: (value: number) => string
  valueWidth?: string
}

export function SettingSlider({
  label, description, disabled, compact, value, onChange,
  min, max, step, format, valueWidth = 'w-7',
}: SettingSliderProps) {
  return (
    <SettingRow label={label} description={description} disabled={disabled} compact={compact}>
      <div className="shrink-0 flex items-center gap-2">
        <input
          type="range"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(e) => onChange(Number(e.target.value))}
          className="w-24 accent-[var(--accent)]"
          style={{ cursor: 'pointer' }}
        />
        <span
          className={`text-xs tabular-nums ${valueWidth} text-right`}
          style={{ color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}
        >
          {format ? format(value) : String(value)}
        </span>
      </div>
    </SettingRow>
  )
}

/* ── SettingSelect ── */

interface SettingSelectProps {
  label: string
  description?: string
  disabled?: boolean
  value: string
  onChange: (value: string) => void
  options: { value: string; label: string }[]
}

export function SettingSelect({ label, description, disabled, value, onChange, options }: SettingSelectProps) {
  return (
    <SettingRow label={label} description={description} disabled={disabled}>
      <select
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className="shrink-0 text-sm rounded-lg px-3 py-1.5 cursor-pointer"
        style={{
          background: 'var(--bg-elevated)',
          color: 'var(--text-primary)',
          border: '1px solid var(--border)',
        }}
      >
        {options.map(opt => (
          <option key={opt.value} value={opt.value}>
            {opt.label}
          </option>
        ))}
      </select>
    </SettingRow>
  )
}

/* ── SettingSection ── */

interface SettingSectionProps {
  title: string
  action?: { label: string; onClick: () => void }
  description?: string
  className?: string
  children: ReactNode
}

export function SettingSection({ title, action, description, className, children }: SettingSectionProps) {
  return (
    <section className={`max-w-lg ${className ?? ''}`}>
      <div className="mb-4">
        <div className="flex items-center justify-between">
          <h2
            className="font-display text-xs font-bold tracking-wider uppercase"
            style={{ color: 'var(--text-tertiary)' }}
          >
            {title}
          </h2>
          {action && (
            <button
              onClick={action.onClick}
              className="text-xs px-3 py-1 rounded-lg transition-colors"
              style={{ color: 'var(--accent)', background: 'var(--accent-dim)' }}
            >
              {action.label}
            </button>
          )}
        </div>
        {description && (
          <span className="text-xs" style={{ color: 'var(--text-secondary)' }}>
            {description}
          </span>
        )}
      </div>

      <div className="flex flex-col gap-1">
        {children}
      </div>
    </section>
  )
}
