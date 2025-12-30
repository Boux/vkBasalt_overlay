#include "keyboard_input_x11.hpp"

#include "logger.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>

#include <memory>
#include <functional>

#include <unistd.h>
#include <cstring>

namespace vkBasalt
{
    static Display* kbDisplay = nullptr;
    static int xiOpcode = 0;
    static bool xiInitialized = false;
    static std::string typedCharsAccumulator;
    static bool backspacePressed = false;
    static bool deletePressed = false;
    static bool enterPressed = false;
    static bool leftPressed = false;
    static bool rightPressed = false;
    static bool homePressed = false;
    static bool endPressed = false;

    static void initKeyboardX11()
    {
        if (kbDisplay)
            return;

        const char* disVar = getenv("DISPLAY");
        if (!disVar || !*disVar)
            return;

        kbDisplay = XOpenDisplay(disVar);
        if (!kbDisplay)
            return;

        int event, error;
        if (XQueryExtension(kbDisplay, "XInputExtension", &xiOpcode, &event, &error))
        {
            int major = 2, minor = 0;
            if (XIQueryVersion(kbDisplay, &major, &minor) == Success)
            {
                unsigned char mask[XIMaskLen(XI_RawKeyPress)] = {0};
                XISetMask(mask, XI_RawKeyPress);

                XIEventMask eventMask = {XIAllMasterDevices, sizeof(mask), mask};
                XISelectEvents(kbDisplay, DefaultRootWindow(kbDisplay), &eventMask, 1);
                xiInitialized = true;
            }
        }
    }

    uint32_t convertToKeySymX11(std::string key)
    {
        // TODO what if X11 isn't loaded?
        uint32_t result = (uint32_t) XStringToKeysym(key.c_str());
        if (!result)
        {
            Logger::err("invalid key");
        }
        return result;
    }

    bool isKeyPressedX11(uint32_t ks)
    {
        static int usesX11 = -1;

        static std::unique_ptr<Display, std::function<void(Display*)>> display;

        if (usesX11 < 0)
        {
            const char* disVar = getenv("DISPLAY");
            if (!disVar || !std::strcmp(disVar, ""))
            {
                usesX11 = 0;
                Logger::debug("no X11 support");
            }
            else
            {
                display = std::unique_ptr<Display, std::function<void(Display*)>>(XOpenDisplay(disVar), [](Display* d) { XCloseDisplay(d); });
                usesX11 = 1;
                Logger::debug("X11 support");
            }
        }

        if (!usesX11)
        {
            return false;
        }

        char keys_return[32];

        XQueryKeymap(display.get(), keys_return);

        KeyCode kc2 = XKeysymToKeycode(display.get(), (KeySym) ks);

        return !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));
    }

    // Helper to process a keycode and update state
    static void processKeycode(KeyCode keycode, unsigned int state)
    {
        KeySym keysym = XkbKeycodeToKeysym(kbDisplay, keycode, 0, 0);

        // Handle special keys
        if (keysym == XK_BackSpace) backspacePressed = true;
        else if (keysym == XK_Delete) deletePressed = true;
        else if (keysym == XK_Return || keysym == XK_KP_Enter) enterPressed = true;
        else if (keysym == XK_Left) leftPressed = true;
        else if (keysym == XK_Right) rightPressed = true;
        else if (keysym == XK_Home) homePressed = true;
        else if (keysym == XK_End) endPressed = true;
        else
        {
            // Check shifted state (from event state or keyboard query)
            bool shifted = (state & ShiftMask) != 0;
            if (!shifted)
            {
                char keys[32];
                XQueryKeymap(kbDisplay, keys);
                shifted = (keys[XKeysymToKeycode(kbDisplay, XK_Shift_L) >> 3] & (1 << (XKeysymToKeycode(kbDisplay, XK_Shift_L) & 7))) ||
                          (keys[XKeysymToKeycode(kbDisplay, XK_Shift_R) >> 3] & (1 << (XKeysymToKeycode(kbDisplay, XK_Shift_R) & 7)));
            }

            KeySym actualSym = XkbKeycodeToKeysym(kbDisplay, keycode, 0, shifted ? 1 : 0);
            if (actualSym >= 0x20 && actualSym <= 0x7E)
            {
                typedCharsAccumulator += (char)actualSym;
            }
        }
    }

    KeyboardState getKeyboardStateX11()
    {
        KeyboardState state;

        initKeyboardX11();
        if (!kbDisplay || !xiInitialized)
            return state;

        // Process keyboard events
        while (XPending(kbDisplay) > 0)
        {
            XEvent ev;
            XNextEvent(kbDisplay, &ev);

            // Handle regular KeyPress events (from grab)
            if (ev.type == KeyPress)
            {
                processKeycode(ev.xkey.keycode, ev.xkey.state);
            }
            // Handle XInput2 raw events (normal operation)
            else if (ev.xcookie.type == GenericEvent && ev.xcookie.extension == xiOpcode &&
                XGetEventData(kbDisplay, &ev.xcookie))
            {
                if (ev.xcookie.evtype == XI_RawKeyPress)
                {
                    XIRawEvent* rawEvent = (XIRawEvent*)ev.xcookie.data;
                    processKeycode(rawEvent->detail, 0);
                }
                XFreeEventData(kbDisplay, &ev.xcookie);
            }
        }

        state.typedChars = typedCharsAccumulator;
        state.backspace = backspacePressed;
        state.del = deletePressed;
        state.enter = enterPressed;
        state.left = leftPressed;
        state.right = rightPressed;
        state.home = homePressed;
        state.end = endPressed;

        // Reset accumulators
        typedCharsAccumulator.clear();
        backspacePressed = false;
        deletePressed = false;
        enterPressed = false;
        leftPressed = false;
        rightPressed = false;
        homePressed = false;
        endPressed = false;

        return state;
    }

    void* getKeyboardDisplay()
    {
        initKeyboardX11();
        return kbDisplay;
    }

} // namespace vkBasalt
