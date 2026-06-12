#pragma once

namespace appearance {

// The desktop's preferred color scheme, read from the XDG desktop portal
// (org.freedesktop.appearance / color-scheme). Unknown when there's no
// preference, no portal, or no D-Bus support compiled in.
enum class Scheme { Unknown, Light, Dark };

// Synchronous, best-effort one-shot query. Cheap enough to call when the
// theme preference resolves; returns Unknown rather than blocking forever.
Scheme SystemScheme();

}  // namespace appearance
