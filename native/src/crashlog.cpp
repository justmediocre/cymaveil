#include "crashlog.h"

#ifdef _WIN32

// Own TU with no raylib include, so windows.h/dbghelp don't clash with raylib.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>  // CaptureStackBackTrace / SymFromAddr — linked via dbghelp

#include <cstdio>
#include <cstring>

namespace {

char g_logPath[1024] = {0};

void WriteBacktrace(EXCEPTION_POINTERS* ep) {
    FILE* f = g_logPath[0] ? std::fopen(g_logPath, "a") : nullptr;
    FILE* out = f ? f : stderr;
    std::fprintf(out, "\n=== CRASH code=0x%08lX addr=%p ===\n",
                 ep ? ep->ExceptionRecord->ExceptionCode : 0UL,
                 ep ? ep->ExceptionRecord->ExceptionAddress : nullptr);

    const HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    void* frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    alignas(SYMBOL_INFO) char symbuf[sizeof(SYMBOL_INFO) + 512];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;

    for (USHORT i = 0; i < n; i++) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line;
        std::memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        if (SymFromAddr(proc, addr, &disp, sym)) {
            if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
                std::fprintf(out, "  #%2u %s  (%s:%lu)\n", i, sym->Name, line.FileName,
                             line.LineNumber);
            } else {
                std::fprintf(out, "  #%2u %s +0x%llX\n", i, sym->Name,
                             static_cast<unsigned long long>(disp));
            }
        } else {
            std::fprintf(out, "  #%2u 0x%llX\n", i, static_cast<unsigned long long>(addr));
        }
    }
    std::fflush(out);
    if (f) std::fclose(f);
}

LONG WINAPI Filter(EXCEPTION_POINTERS* ep) {
    WriteBacktrace(ep);
    return EXCEPTION_EXECUTE_HANDLER;  // let the process die after logging
}

}  // namespace

namespace crashlog {

void Install(const char* logPath) {
    std::strncpy(g_logPath, logPath ? logPath : "", sizeof(g_logPath) - 1);
    SetUnhandledExceptionFilter(Filter);
}

}  // namespace crashlog

#else

namespace crashlog {
void Install(const char*) {}
}  // namespace crashlog

#endif
