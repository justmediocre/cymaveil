interface PlaceholderViewProps {
  label: string
}

export default function PlaceholderView({ label }: PlaceholderViewProps) {
  return (
    <div className="h-full flex items-center justify-center">
      <p className="text-sm" style={{ color: 'var(--text-tertiary)' }}>
        {label} coming soon
      </p>
    </div>
  )
}
