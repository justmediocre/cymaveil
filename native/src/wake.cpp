#include "wake.h"

#include <atomic>

// raylib's GLFW backend. Declared by hand like elsewhere in the app; the
// callback signatures are GLFW 3's stable C API.
extern "C" {
struct GLFWwindow;
using GLFWkeyfun = void (*)(GLFWwindow*, int, int, int, int);
using GLFWcharfun = void (*)(GLFWwindow*, unsigned int);
using GLFWmousebuttonfun = void (*)(GLFWwindow*, int, int, int);
using GLFWcursorposfun = void (*)(GLFWwindow*, double, double);
using GLFWscrollfun = void (*)(GLFWwindow*, double, double);
using GLFWcursorenterfun = void (*)(GLFWwindow*, int);
using GLFWwindowsizefun = void (*)(GLFWwindow*, int, int);
using GLFWwindowposfun = void (*)(GLFWwindow*, int, int);
using GLFWframebuffersizefun = void (*)(GLFWwindow*, int, int);
using GLFWwindowfocusfun = void (*)(GLFWwindow*, int);
using GLFWwindowiconifyfun = void (*)(GLFWwindow*, int);
using GLFWwindowmaximizefun = void (*)(GLFWwindow*, int);
using GLFWwindowcontentscalefun = void (*)(GLFWwindow*, float, float);
using GLFWwindowrefreshfun = void (*)(GLFWwindow*);
using GLFWdropfun = void (*)(GLFWwindow*, int, const char**);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow*, GLFWkeyfun);
GLFWcharfun glfwSetCharCallback(GLFWwindow*, GLFWcharfun);
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow*, GLFWmousebuttonfun);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow*, GLFWcursorposfun);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow*, GLFWscrollfun);
GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow*, GLFWcursorenterfun);
GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow*, GLFWwindowsizefun);
GLFWwindowposfun glfwSetWindowPosCallback(GLFWwindow*, GLFWwindowposfun);
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow*, GLFWframebuffersizefun);
GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow*, GLFWwindowfocusfun);
GLFWwindowiconifyfun glfwSetWindowIconifyCallback(GLFWwindow*, GLFWwindowiconifyfun);
GLFWwindowmaximizefun glfwSetWindowMaximizeCallback(GLFWwindow*, GLFWwindowmaximizefun);
GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow*, GLFWwindowcontentscalefun);
GLFWwindowrefreshfun glfwSetWindowRefreshCallback(GLFWwindow*, GLFWwindowrefreshfun);
GLFWdropfun glfwSetDropCallback(GLFWwindow*, GLFWdropfun);
GLFWwindow* glfwGetCurrentContext(void);
void glfwPostEmptyEvent(void);  // safe to call from any thread, wakes WaitEvents
}

namespace {

std::atomic<bool> g_woken{true};  // draw the first frame

// Release/acquire: whatever a worker hands over before Post() is visible to
// the main loop once Consume() has seen the wake.
void Mark() { g_woken.store(true, std::memory_order_release); }

// Each wrapper marks the wake, then hands the event on to raylib's own
// callback (captured at Install) so its input state keeps working.
GLFWkeyfun g_key = nullptr;
GLFWcharfun g_char = nullptr;
GLFWmousebuttonfun g_button = nullptr;
GLFWcursorposfun g_cursor = nullptr;
GLFWscrollfun g_scroll = nullptr;
GLFWcursorenterfun g_enter = nullptr;
GLFWwindowsizefun g_size = nullptr;
GLFWwindowposfun g_pos = nullptr;
GLFWframebuffersizefun g_fbsize = nullptr;
GLFWwindowfocusfun g_focus = nullptr;
GLFWwindowiconifyfun g_iconify = nullptr;
GLFWwindowmaximizefun g_maximize = nullptr;
GLFWwindowcontentscalefun g_scale = nullptr;
GLFWwindowrefreshfun g_refresh = nullptr;
GLFWdropfun g_drop = nullptr;

void OnKey(GLFWwindow* w, int key, int scancode, int action, int mods) {
    Mark();
    if (g_key) g_key(w, key, scancode, action, mods);
}
void OnChar(GLFWwindow* w, unsigned int cp) {
    Mark();
    if (g_char) g_char(w, cp);
}
void OnButton(GLFWwindow* w, int button, int action, int mods) {
    Mark();
    if (g_button) g_button(w, button, action, mods);
}
void OnCursor(GLFWwindow* w, double x, double y) {
    Mark();
    if (g_cursor) g_cursor(w, x, y);
}
void OnScroll(GLFWwindow* w, double x, double y) {
    Mark();
    if (g_scroll) g_scroll(w, x, y);
}
void OnEnter(GLFWwindow* w, int entered) {
    Mark();
    if (g_enter) g_enter(w, entered);
}
void OnSize(GLFWwindow* w, int width, int height) {
    Mark();
    if (g_size) g_size(w, width, height);
}
void OnPos(GLFWwindow* w, int x, int y) {
    Mark();
    if (g_pos) g_pos(w, x, y);
}
void OnFbSize(GLFWwindow* w, int width, int height) {
    Mark();
    if (g_fbsize) g_fbsize(w, width, height);
}
void OnFocus(GLFWwindow* w, int focused) {
    Mark();
    if (g_focus) g_focus(w, focused);
}
void OnIconify(GLFWwindow* w, int iconified) {
    Mark();
    if (g_iconify) g_iconify(w, iconified);
}
void OnMaximize(GLFWwindow* w, int maximized) {
    Mark();
    if (g_maximize) g_maximize(w, maximized);
}
void OnScale(GLFWwindow* w, float x, float y) {
    Mark();
    if (g_scale) g_scale(w, x, y);
}
void OnRefresh(GLFWwindow* w) {
    Mark();
    if (g_refresh) g_refresh(w);
}
void OnDrop(GLFWwindow* w, int count, const char** paths) {
    Mark();
    if (g_drop) g_drop(w, count, paths);
}

}  // namespace

namespace wake {

void Install() {
    // raylib's GetWindowHandle() returns the native (X11/Wayland/Win32)
    // handle, not the GLFWwindow; its context is current on this thread.
    GLFWwindow* w = glfwGetCurrentContext();
    if (w == nullptr) return;
    g_key = glfwSetKeyCallback(w, OnKey);
    g_char = glfwSetCharCallback(w, OnChar);
    g_button = glfwSetMouseButtonCallback(w, OnButton);
    g_cursor = glfwSetCursorPosCallback(w, OnCursor);
    g_scroll = glfwSetScrollCallback(w, OnScroll);
    g_enter = glfwSetCursorEnterCallback(w, OnEnter);
    g_size = glfwSetWindowSizeCallback(w, OnSize);
    g_pos = glfwSetWindowPosCallback(w, OnPos);
    g_fbsize = glfwSetFramebufferSizeCallback(w, OnFbSize);
    g_focus = glfwSetWindowFocusCallback(w, OnFocus);
    g_iconify = glfwSetWindowIconifyCallback(w, OnIconify);
    g_maximize = glfwSetWindowMaximizeCallback(w, OnMaximize);
    g_scale = glfwSetWindowContentScaleCallback(w, OnScale);
    g_refresh = glfwSetWindowRefreshCallback(w, OnRefresh);
    g_drop = glfwSetDropCallback(w, OnDrop);
}

void Post() {
    Mark();
    glfwPostEmptyEvent();
}

bool Consume() { return g_woken.exchange(false, std::memory_order_acq_rel); }

}  // namespace wake
