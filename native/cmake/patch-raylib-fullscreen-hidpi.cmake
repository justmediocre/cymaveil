# raylib 6.0: fullscreen ignores FLAG_WINDOW_HIGHDPI. FramebufferSizeCallback()
# deliberately sets the logical screen size to the physical framebuffer size
# ("strategy is ignoring high-dpi") whenever FLAG_FULLSCREEN_MODE is set, so on
# a 2x display the whole UI is laid out at half size the moment F11 is pressed:
# a 340-unit cover becomes 340 physical pixels on a 2880x1800 panel. raylib has
# a Wayland branch that would use the logical window size instead, but it sits
# behind `#if defined(_GLFW_WAYLAND)`, which raylib's CMake never defines for
# the raylib target, so it is dead code.
#
# Replace the fullscreen branch with the same logical-unit strategy window mode
# uses. Where the window size already differs from the framebuffer (Wayland
# with GLFW_SCALE_FRAMEBUFFER, macOS) the window size *is* the logical size and
# the cursor is already logical; otherwise (X11, Windows) derive it from the
# content scale and scale the cursor like window mode does.
#
# Leaving fullscreen has the mirror problem: the size handed back to
# glfwSetWindowMonitor() is multiplied by the DPI scale behind a compile-time
# `!defined(_GLFW_WAYLAND)` guard, which is always true, so on a 2x Wayland
# display the window came back at twice its previous logical size. Use the same
# runtime platform check InitWindow uses.
#
# Run as PATCH_COMMAND from the raylib source dir. Idempotent: once patched the
# original text is gone, so a re-run is a no-op.
file(READ src/platforms/rcore_desktop_glfw.c CONTENT)

# FramebufferSizeCallback: fullscreen branch
string(REPLACE
    "    if (FLAG_IS_SET(CORE.Window.flags, FLAG_FULLSCREEN_MODE))
    {
        // On fullscreen mode, strategy is ignoring high-dpi and
        // use the all available display size

        // Set screen size to render size (physical pixel size)
        CORE.Window.screen.width = width;
        CORE.Window.screen.height = height;
        CORE.Window.screenScale = MatrixScale(1.0f, 1.0f, 1.0f);
        SetMouseScale(1.0f, 1.0f);

        // On Wayland with GLFW_SCALE_FRAMEBUFFER, the framebuffer is still scaled in fullscreen, use logical window size as screen and apply screenScale
#if defined(_GLFW_WAYLAND) && !defined(_GLFW_X11)
        if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI))
        {
            int winWidth = 0;
            int winHeight = 0;
            glfwGetWindowSize(platform.handle, &winWidth, &winHeight);

            if ((winWidth != width) || (winHeight != height))
            {
                CORE.Window.screen.width = winWidth;
                CORE.Window.screen.height = winHeight;
                float scaleX = (float)width/winWidth;
                float scaleY = (float)height/winHeight;

                CORE.Window.screenScale = MatrixScale(scaleX, scaleY, 1.0f);
            }
        }
#endif
    }"
    "    if (FLAG_IS_SET(CORE.Window.flags, FLAG_FULLSCREEN_MODE))
    {
        // PATCHED (cymaveil): keep HIGHDPI layout in logical units in fullscreen
        // too. Stock raylib lays fullscreen out in physical pixels, so on a 2x
        // display the whole UI shrank to half size; its Wayland branch was dead
        // code (_GLFW_WAYLAND is never defined for the raylib target).
        CORE.Window.screen.width = width;
        CORE.Window.screen.height = height;
        CORE.Window.screenScale = MatrixScale(1.0f, 1.0f, 1.0f);
        SetMouseScale(1.0f, 1.0f);

        if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI))
        {
            int winWidth = width;
            int winHeight = height;
            glfwGetWindowSize(platform.handle, &winWidth, &winHeight);

            if ((winWidth > 0) && (winHeight > 0) && ((winWidth != width) || (winHeight != height)))
            {
                // Wayland (GLFW_SCALE_FRAMEBUFFER) / macOS: the window size is
                // already logical and so are cursor coordinates
                CORE.Window.screen.width = winWidth;
                CORE.Window.screen.height = winHeight;
                CORE.Window.screenScale = MatrixScale((float)width/winWidth, (float)height/winHeight, 1.0f);
            }
            else
            {
                // X11 / Windows: window and framebuffer are both physical pixels,
                // derive the logical size from the content scale like window mode
                float scaleX = 1.0f;
                float scaleY = 1.0f;
                glfwGetWindowContentScale(platform.handle, &scaleX, &scaleY);

                if ((scaleX > 0.0f) && (scaleY > 0.0f))
                {
                    CORE.Window.screen.width = (int)((float)width/scaleX);
                    CORE.Window.screen.height = (int)((float)height/scaleY);
                    CORE.Window.screenScale = MatrixScale(scaleX, scaleY, 1.0f);
#if !defined(__APPLE__)
                    SetMouseScale(1.0f/scaleX, 1.0f/scaleY);
#endif
                }
            }
        }
    }"
    CONTENT "${CONTENT}")

# ToggleFullscreen: leaving fullscreen, restore the windowed size
string(REPLACE
    "#if !defined(__APPLE__) && !defined(_GLFW_WAYLAND)
        // Make sure to restore render size considering HighDPI scaling
        // NOTE: On Wayland, GLFW_SCALE_FRAMEBUFFER handles scaling, skip manual resize
        if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI))
        {"
    "#if !defined(__APPLE__)
        // Make sure to restore render size considering HighDPI scaling
        // NOTE: On Wayland, GLFW_SCALE_FRAMEBUFFER handles scaling, skip manual resize
        // PATCHED (cymaveil): runtime check, _GLFW_WAYLAND is never defined for the raylib target
        if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI) && (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND))
        {"
    CONTENT "${CONTENT}")

file(WRITE src/platforms/rcore_desktop_glfw.c "${CONTENT}")
