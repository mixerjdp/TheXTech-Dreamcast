/*
 * TheXTech Dreamcast backend
 */

#include "core/window.h"

namespace XWindow
{

bool init()
{
    return true;
}

void quit() {}

void updateWindowIcon() {}

void show() {}
void hide() {}
int showCursor(int show) { return show; }
void setCursor(WindowCursor_t /*cursor*/) {}
WindowCursor_t getCursor() { return CURSOR_NONE; }
void placeCursor(int /*window_x*/, int /*window_y*/) {}
void textInputStart() {}
void textInputStop() {}
void textInputSetRect(int, int, int, int) {}
bool isFullScreen() { return true; }
int setFullScreen(bool /*fs*/) { return 1; }
void restoreWindow() {}
void setWindowSize(int /*w*/, int /*h*/) {}

void getWindowSize(int *w, int *h)
{
    // The minport layer halves this to get the real framebuffer, because it
    // addresses everything in half-res units (see FLOORDIV2 in
    // render_minport_shared.hpp). The Dreamcast scans out 640x480.
    *w = 1280;
    *h = 960;
}

bool hasWindowInputFocus() { return true; }
bool hasWindowMouseFocus() { return true; }
bool isMaximized() { return true; }
void setTitle(const char *) {}

} // namespace XWindow
