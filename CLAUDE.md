# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Setup build directory (debug)
meson setup --buildtype=debug builddir

# Setup build directory (release)
meson setup --buildtype=release --prefix=/usr builddir

# Build
ninja -C builddir

# Install
ninja -C builddir install

# 32-bit build
ASFLAGS=--32 CFLAGS=-m32 CXXFLAGS=-m32 PKG_CONFIG_PATH=/usr/lib32/pkgconfig \
meson setup --prefix=/usr --buildtype=release --libdir=lib32 -Dwith_json=false builddir.32
ninja -C builddir.32
```

**Dependencies**: GCC >= 9, X11 development files, glslang, SPIR-V Headers, Vulkan Headers

## Testing

Run any Vulkan game/application with:
```bash
ENABLE_VKBASALT=1 VKBASALT_LOG_LEVEL=debug ./game
```

Use `VKBASALT_CONFIG_FILE=/path/to/config.conf` to test specific configurations.

## Architecture Overview

vkBasalt is a **Vulkan implicit layer** that intercepts Vulkan API calls to apply post-processing effects to game graphics.

### Layer System (basalt.cpp)

The layer intercepts these key Vulkan functions:
- `vkCreateInstance` / `vkDestroyInstance` - Layer initialization
- `vkCreateDevice` / `vkDestroyDevice` - LogicalDevice setup
- `vkCreateSwapchainKHR` / `vkDestroySwapchainKHR` - Create intermediate images for effect processing
- `vkQueuePresentKHR` - **Main entry point**: applies effects before presentation
- `vkGetSwapchainImagesKHR` - Returns wrapped images

**Dispatch tables** (`vkdispatch.hpp`): Store Vulkan function pointers per-instance and per-device. Functions are listed via macros in `vkfuncs.hpp`.

**Global maps** track state by handle:
- `instanceDispatchMap`, `deviceMap` - Dispatch tables
- `swapchainMap` - Per-swapchain effect state (LogicalSwapchain)

### Effect Processing Flow

```
Original Swapchain Image
    → Effect 1 (CAS, FXAA, etc.)
    → Effect 2
    → ...
    → Final image presented
```

Effects read from one image and write to another. "Fake images" are intermediate buffers created in `vkCreateSwapchainKHR`.

### Effect System

**Base class** (`effect.hpp`):
```cpp
class Effect {
    virtual void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) = 0;
    virtual void updateEffect() {}
    virtual std::vector<EffectParameter> getParameters() const { return {}; }
};
```

**Built-in effects** (inherit from `SimpleEffect`):
- `effect_cas.cpp` - Contrast Adaptive Sharpening
- `effect_dls.cpp` - Denoised Luma Sharpening
- `effect_fxaa.cpp` - Fast Approximate Anti-Aliasing
- `effect_smaa.cpp` - Subpixel Morphological AA (multi-pass)
- `effect_deband.cpp` - Color banding reduction
- `effect_lut.cpp` - 3D color lookup table

**ReShade FX support** (`effect_reshade.cpp`): Compiles .fx shader files using the embedded ReShade compiler (`src/reshade/`).

### Shaders

GLSL shaders in `src/shader/` are compiled to SPIR-V by glslangValidator at build time, then embedded as C headers.

To add a new shader:
1. Create `src/shader/myshader.frag.glsl`
2. Add to `src/shader/meson.build`
3. Include generated header in your effect

### Configuration (config.hpp)

Searches for `vkBasalt.conf` in order:
1. `$VKBASALT_CONFIG_FILE`
2. Game working directory
3. `~/.config/vkBasalt/`
4. `/etc/vkBasalt/`

**Hot-reload**: Config changes detected via `hasConfigChanged()`, effects rebuilt automatically.

**Template-based parsing**: `pConfig->getOption<float>("casSharpness", 0.4f)`

### ImGui Overlay (imgui_overlay.cpp)

In-game UI toggled with End key (configurable). Shows:
- Active effects list
- Effect parameters with current values
- Config file path

Parameters collected via `collectEffectParameters()` in `effect_params.cpp`.

### Key Structures

- **LogicalDevice** (`logical_device.hpp`): Wraps VkDevice with dispatch tables and queue info
- **LogicalSwapchain** (`logical_swapchain.hpp`): Per-swapchain state including effects vector, command buffers, and semaphores
- **EffectParameter** (`imgui_overlay.hpp`): UI-displayable parameter with name, type, value, and range

### Input Handling

X11-based keyboard/mouse input (`keyboard_input_x11.cpp`, `mouse_input.cpp`) compiled as separate static libraries to avoid symbol conflicts with X11 headers.

## Code Patterns

- **Thread safety**: Global mutex `globalLock` protects all maps
- **Memory management**: `std::shared_ptr` for LogicalDevice, LogicalSwapchain, Config
- **Format handling**: Always consider SRGB vs UNORM variants (`format.cpp`)
- **Logging**: Use `Logger::debug()`, `Logger::info()`, `Logger::err()`

## Code Style

- **Avoid nested ifs**: Keep code flat with single-level indentation where possible. Use early returns, early continues, and guard clauses instead of nesting conditions.
  ```cpp
  // Bad - deeply nested
  if (condition1) {
      if (condition2) {
          if (condition3) {
              doSomething();
          }
      }
  }

  // Good - flat with early returns
  if (!condition1)
      return;
  if (!condition2)
      return;
  if (!condition3)
      return;
  doSomething();
  ```

## Environment Variables

- `ENABLE_VKBASALT=1` - Enable the layer
- `DISABLE_VKBASALT=1` - Force disable
- `VKBASALT_LOG_LEVEL=debug` - Log levels: trace, debug, info, warn, error, none
- `VKBASALT_LOG_FILE=/path/to/log` - Output to file instead of stderr
- `VKBASALT_CONFIG_FILE=/path/to/conf` - Override config location

## User Preferences

- **File organization**: Proactively suggest splitting code into separate files when a file gets too large or a distinct responsibility emerges. Always inform the user before refactoring.
- **After meson reconfigure (debug only)**: During development, the `vkBasalt.json` library_path may revert to relative path. For local testing, fix to absolute path: `/home/boux/repo/vkBasalt/build/src/libvkbasalt.so`. This is not needed for actual builds/releases.
