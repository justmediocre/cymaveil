/** Returns true when the platform "command" modifier is held (Ctrl on Windows/Linux, Cmd on macOS). */
export const cmdOrCtrl = (e: { ctrlKey: boolean; metaKey: boolean }) =>
  e.ctrlKey || e.metaKey
