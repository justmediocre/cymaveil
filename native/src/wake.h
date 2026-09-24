#pragma once

// Idle wake-ups. In event-wait pacing the main loop blocks in glfwWaitEvents,
// but a return from that wait does not mean anything happened: on Wayland,
// Mesa's EGL swap syncs on its own queue and the wl_display.delete_id for that
// callback lands on the display queue GLFW waits on, so every frame's swap
// wakes the next wait. Drawing on each such wake re-arms it and the "idle"
// loop never stops rendering. Instead, anything that should produce a frame
// marks a wake here, and the loop goes back to waiting when nothing did.
namespace wake {

// Chain onto raylib's GLFW input and window callbacks so every real event
// (input, resize, focus, drop, expose, ...) marks a wake. Call once after
// InitWindow.
void Install();

// Mark a wake and break the main loop out of glfwWaitEvents. Safe from any
// thread; worker threads call it when they have something for the main loop.
void Post();

// Main thread: true if anything marked a wake since the last call.
bool Consume();

}  // namespace wake
