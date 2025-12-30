#include "input_blocker.hpp"
#include "keyboard_input_x11.hpp"
#include "logger.hpp"

#include <X11/Xlib.h>

namespace vkBasalt
{
    static bool blockingEnabled = false;  // From config
    static bool blocked = false;
    static bool grabbed = false;

    static void grabInput()
    {
        if (grabbed)
            return;

        // Use the same display as keyboard input so grabbed events are processed
        Display* display = (Display*)getKeyboardDisplay();
        if (!display)
            return;

        Window root = DefaultRootWindow(display);

        // Grab both keyboard and mouse
        int kbResult = XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        int ptrResult = XGrabPointer(display, root, False,
                                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

        if (kbResult == GrabSuccess && ptrResult == GrabSuccess)
        {
            grabbed = true;
            Logger::debug("Input grabbed for overlay");
        }
        else
        {
            if (kbResult == GrabSuccess)
                XUngrabKeyboard(display, CurrentTime);
            if (ptrResult == GrabSuccess)
                XUngrabPointer(display, CurrentTime);
            Logger::debug("Failed to grab input");
        }

        XFlush(display);
    }

    static void ungrabInput()
    {
        if (!grabbed)
            return;

        Display* display = (Display*)getKeyboardDisplay();
        if (!display)
            return;

        XUngrabKeyboard(display, CurrentTime);
        XUngrabPointer(display, CurrentTime);
        XFlush(display);

        grabbed = false;
        Logger::debug("Input released from overlay");
    }

    void initInputBlocker(bool enabled)
    {
        blockingEnabled = enabled;
        Logger::debug(std::string("Input blocking ") + (enabled ? "enabled" : "disabled"));
    }

    void setInputBlocked(bool shouldBlock)
    {
        if (!blockingEnabled)
            return;

        if (shouldBlock == blocked)
            return;

        blocked = shouldBlock;

        if (blocked)
            grabInput();
        else
            ungrabInput();
    }

    bool isInputBlocked()
    {
        return blocked;
    }
}
