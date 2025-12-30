#pragma once

#include <cstdint>
#include <string>
#include "keyboard_input.hpp"

namespace vkBasalt
{
    uint32_t convertToKeySymX11(std::string key);
    bool     isKeyPressedX11(uint32_t ks);
    KeyboardState getKeyboardStateX11();

    // For input blocking - returns the keyboard display connection
    void* getKeyboardDisplay();
} // namespace vkBasalt
