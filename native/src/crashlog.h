#pragma once

namespace crashlog {

// Install a last-chance handler that writes a symbolized stack trace to logPath
// (and stderr) when the process crashes. Windows-only; a no-op elsewhere.
void Install(const char* logPath);

}  // namespace crashlog
