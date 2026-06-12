# raylib never sets a Wayland app_id / X11 WM_CLASS, so the window can't be
# matched to a desktop file — KDE/GNOME then show a generic icon and refuse to
# attach taskbar media controls to the MPRIS player (the task's launcher URL
# stays empty). Inject the identity hints right after the defaults are set.
# Run as PATCH_COMMAND from the raylib source dir. Idempotent.
file(READ src/platforms/rcore_desktop_glfw.c CONTENT)
string(REPLACE
    "glfwDefaultWindowHints();                       // Set default windows hints"
    "glfwDefaultWindowHints();                       // Set default windows hints
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, \"cymaveil\");   // match cymaveil.desktop
    glfwWindowHintString(GLFW_X11_CLASS_NAME, \"cymaveil\");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, \"cymaveil\");"
    CONTENT "${CONTENT}")
file(WRITE src/platforms/rcore_desktop_glfw.c "${CONTENT}")
