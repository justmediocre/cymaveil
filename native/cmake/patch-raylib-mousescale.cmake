# raylib 6.0: on a HiDPI Wayland display, resizing the window leaves the mouse
# position wrong. InitWindow decides whether to apply DPI mouse scaling with a
# *runtime* check (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) and correctly
# skips it on Wayland, where cursor coords are already in logical space. But the
# resize callbacks gate the same SetMouseScale() call behind a *compile-time*
# `#if !defined(_GLFW_WAYLAND)` — and raylib's own CMake never defines
# _GLFW_WAYLAND for the raylib target (it only ever defines _GLFW_X11), so the
# guard is always true. The first resize then calls SetMouseScale(1/dpi) and
# every mouse coordinate gets scaled down, breaking hit-testing until restart.
#
# Replace the broken compile-time guard with the same runtime check InitWindow
# uses, so resize and content-scale changes match init on every backend.
# Run as PATCH_COMMAND from the raylib source dir. Idempotent.
file(READ src/platforms/rcore_desktop_glfw.c CONTENT)

# FramebufferSizeCallback (window-mode high-dpi branch)
string(REPLACE
    "#if !defined(__APPLE__) && !defined(_GLFW_WAYLAND)
            // On macOS and Linux-Wayland, mouse coords are already in logical space
            SetMouseScale(1.0f/scaleDpi.x, 1.0f/scaleDpi.y);
#endif"
    "#if !defined(__APPLE__)
            // On macOS and Linux-Wayland, mouse coords are already in logical space.
            // Runtime check: _GLFW_WAYLAND is not defined for the raylib target.
            if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
                SetMouseScale(1.0f/scaleDpi.x, 1.0f/scaleDpi.y);
#endif"
    CONTENT "${CONTENT}")

# WindowContentScaleCallback (monitor DPI change)
string(REPLACE
    "#if !defined(__APPLE__) && !defined(_GLFW_WAYLAND)
    // On macOS and Linux-Wayland, mouse coords are already in logical space
    SetMouseScale(1.0f/scalex, 1.0f/scaley);
#endif"
    "#if !defined(__APPLE__)
    // On macOS and Linux-Wayland, mouse coords are already in logical space.
    // Runtime check: _GLFW_WAYLAND is not defined for the raylib target.
    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
        SetMouseScale(1.0f/scalex, 1.0f/scaley);
#endif"
    CONTENT "${CONTENT}")

file(WRITE src/platforms/rcore_desktop_glfw.c "${CONTENT}")
