# raylib 6.0 / bundled GLFW: on a multi-monitor Wayland desktop, fullscreen
# always lands on the primary display regardless of which monitor the window is
# on. Wayland deliberately hides the window position from clients, so raylib's
# GetCurrentMonitor() (which infers the monitor from the window's top-left
# position) can only ever return monitor 0. GLFW then pins fullscreen to that
# specific output via xdg_toplevel_set_fullscreen(toplevel, output) — so it
# leaves whatever display the window was on and jumps to the primary.
#
# The Wayland-idiomatic way to fullscreen "where the window already is" is to
# pass a NULL output, which tells the compositor to fullscreen the surface on
# the output it currently occupies. Patch acquireMonitorWayland() — the single
# function GLFW calls for a runtime fullscreen toggle — to do exactly that.
#
# This only touches the Wayland backend; the X11 path (which can read window
# positions, so the app's own monitor detection works there) is untouched.
# Run as PATCH_COMMAND from the raylib source dir. Idempotent: once patched the
# original text is gone, so a re-run is a no-op.
file(READ src/external/glfw/src/wl_window.c CONTENT)

string(REPLACE
    "    if (window->wl.libdecor.frame)
    {
        libdecor_frame_set_fullscreen(window->wl.libdecor.frame,
                                      window->monitor->wl.output);
    }
    else if (window->wl.xdg.toplevel)
    {
        xdg_toplevel_set_fullscreen(window->wl.xdg.toplevel,
                                    window->monitor->wl.output);
    }

    setIdleInhibitor(window, GLFW_TRUE);"
    "    // PATCHED (cymaveil): pass a NULL output so the compositor fullscreens on
    // the window's current output. Wayland hides the window position, so raylib
    // cannot tell which monitor we are on and would otherwise force the primary.
    if (window->wl.libdecor.frame)
    {
        libdecor_frame_set_fullscreen(window->wl.libdecor.frame, NULL);
    }
    else if (window->wl.xdg.toplevel)
    {
        xdg_toplevel_set_fullscreen(window->wl.xdg.toplevel, NULL);
    }

    setIdleInhibitor(window, GLFW_TRUE);"
    CONTENT "${CONTENT}")

file(WRITE src/external/glfw/src/wl_window.c "${CONTENT}")
