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

## CRITICAL: EffectRegistry is the Single Source of Truth

**THIS IS THE MOST IMPORTANT ARCHITECTURAL CONCEPT IN THE CODEBASE.**

`EffectRegistry` (`src/effects/effect_registry.hpp`) is the **ONLY** authoritative source for effect parameter values at runtime. All components read from and write to the registry.

### Data Flow (MUST follow this pattern)

```
Config file ──(parsed ONCE at startup)──► EffectRegistry ◄──► UI (reads/writes)
                                               │
                                               ▼
                                    Effects read from Registry
                                               │
                                               ▼
                                           Rendering
```

### Rules (NEVER violate these)

1. **Config is parsed ONCE** - At startup, config values populate EffectRegistry. After that, Config is NOT the source of truth.

2. **UI reads/writes EffectRegistry directly** - The overlay modifies `EffectRegistry` parameters in-place. No intermediate copies.

3. **Effects read from EffectRegistry** - When effects are created/recreated, they get parameter values from `EffectRegistry->getParameter()`, NOT from `pConfig`.

4. **Never sync Registry → pConfig for rendering** - The old pattern of syncing Registry to Config overrides is WRONG. Effects read directly from Registry.

5. **Save writes Registry → file** - When saving config, serialize values FROM EffectRegistry to the config file.

### For ReShade Effects

ReShade effects use specialization constants. The `ReshadeEffect` constructor takes `EffectRegistry*` and reads spec constant values like this:

```cpp
EffectParam* param = pEffectRegistry->getParameter(effectName, paramName);
if (auto* fp = dynamic_cast<FloatParam*>(param))
    value = fp->value;  // Read directly from registry
```

For vector types (float2/3/4, int2/3/4, uint2/3/4), components share the same parameter name but use an index:
```cpp
if (auto* fvp = dynamic_cast<FloatVecParam*>(param))
    value = fvp->value[vectorComponentIndex];  // 0, 1, 2, or 3
```

## SettingsManager: Single Source of Truth for Settings

Just like `EffectRegistry` for effect parameters, `SettingsManager` (`src/settings_manager.hpp`) is the **ONLY** source of truth for application settings (keybindings, overlay options, etc.).

### Data Flow

```
vkBasalt.conf ──(parsed ONCE at startup)──► SettingsManager ◄──► UI (reads/writes)
                                                  │
                                                  ▼
                                       basalt.cpp reads settings
                                                  │
                                                  ▼
                                         Layer behavior
```

### Rules

1. **Config is parsed ONCE** - At startup, `settingsManager.initialize()` loads from `vkBasalt.conf`. After that, the file is NOT the source of truth.

2. **UI reads/writes SettingsManager directly** - Use `settingsManager.getXxx()` and `settingsManager.setXxx()` in overlay code. No local copies of settings values.

3. **Layer code reads from SettingsManager** - `basalt.cpp` uses `settingsManager.getDepthCapture()`, `settingsManager.getMaxEffects()`, etc.

4. **Save writes SettingsManager → file** - Call `settingsManager.save()` to persist changes to `vkBasalt.conf`.

### ImGui Slider Pattern

When using sliders that read from SettingsManager, update immediately on change but save to file only on release:

```cpp
int value = settingsManager.getAutoApplyDelay();
if (ImGui::SliderInt("##delay", &value, 20, 1000))
    settingsManager.setAutoApplyDelay(value);  // Update immediately while dragging
if (ImGui::IsItemDeactivatedAfterEdit())
    settingsManager.save();  // Save to file only on release
```

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
- `effectRegistry` - Global registry for effect configurations and parameters

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
};
```

**Built-in effects** (inherit from `SimpleEffect`):
- `effect_cas.cpp` - Contrast Adaptive Sharpening
- `effect_dls.cpp` - Denoised Luma Sharpening
- `effect_fxaa.cpp` - Fast Approximate Anti-Aliasing
- `effect_smaa.cpp` - Subpixel Morphological AA (multi-pass)
- `effect_deband.cpp` - Color banding reduction
- `effect_lut.cpp` - 3D color lookup table

**ReShade FX support** (`effect_reshade.cpp`): Compiles .fx shader files using the embedded ReShade compiler (`src/reshade/`). Takes `EffectRegistry*` to read parameter values.

### Parameter System

**EffectParam** hierarchy (`src/effects/params/effect_param.hpp`):

| Type | Description | Storage |
|------|-------------|---------|
| `FloatParam` | Scalar float | `float value` |
| `FloatVecParam` | float2/3/4 vector | `float value[4]` + `componentCount` |
| `IntParam` | Scalar signed int | `int value` |
| `IntVecParam` | int2/3/4 vector | `int value[4]` + `componentCount` |
| `UintParam` | Scalar unsigned int | `uint32_t value` |
| `UintVecParam` | uint2/3/4 vector | `uint32_t value[4]` + `componentCount` |
| `BoolParam` | Boolean | `bool value` |

**Vector types use `componentCount`**: Instead of separate Float2Param/Float3Param/Float4Param classes, a single `FloatVecParam` class uses a `componentCount` field (2, 3, or 4) to track the vector size. Same pattern for IntVecParam and UintVecParam.

**Serialization format**: Vector parameters serialize to array-style keys: `ParamName[0]`, `ParamName[1]`, etc.

**Field editors** (`src/overlay/params/fields/`): Each param type has a corresponding field editor that renders the ImGui controls. Registered via `REGISTER_FIELD_EDITOR(ParamType, EditorClass)` macro.

Parameters are stored in `EffectConfig` within `EffectRegistry`. The UI renders these directly and modifies them in-place.

### Shaders

GLSL shaders in `src/shader/` are compiled to SPIR-V by glslangValidator at build time, then embedded as C headers.

To add a new shader:
1. Create `src/shader/myshader.frag.glsl`
2. Add to `src/shader/meson.build`
3. Include generated header in your effect

### Configuration

Config files are parsed at startup to populate EffectRegistry. After initialization, the registry is the source of truth.

Search order for `vkBasalt.conf`:
1. `$VKBASALT_CONFIG_FILE`
2. Game working directory
3. `~/.config/vkBasalt/`
4. `/etc/vkBasalt/`

### ImGui Overlay (imgui_overlay.cpp)

In-game UI toggled with End key (configurable). The overlay:
- Reads parameters directly from EffectRegistry
- Writes changes directly to EffectRegistry
- Triggers effect reload when Apply is clicked

### Key Structures

- **LogicalDevice** (`logical_device.hpp`): Wraps VkDevice with dispatch tables and queue info
- **LogicalSwapchain** (`logical_swapchain.hpp`): Per-swapchain state including effects vector, command buffers, and semaphores
- **EffectRegistry** (`effect_registry.hpp`): **THE** source of truth for effect configs and parameter values
- **SettingsManager** (`settings_manager.hpp`): **THE** source of truth for application settings (keybindings, options)
- **EffectConfig** (`effect_config.hpp`): Per-effect configuration including parameters

### Input Handling

X11-based keyboard/mouse input (`keyboard_input_x11.cpp`, `mouse_input.cpp`) compiled as separate static libraries to avoid symbol conflicts with X11 headers.

## Code Patterns

- **Thread safety**: Global mutex `globalLock` protects all maps
- **Memory management**: `std::shared_ptr` for LogicalDevice, LogicalSwapchain
- **Format handling**: Always consider SRGB vs UNORM variants (`format.cpp`)
- **Logging**: Use `Logger::debug()`, `Logger::info()`, `Logger::err()`
- **Registry access**: Always use `EffectRegistry` for parameter values, never read from Config at runtime

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
- **After meson reconfigure (debug only)**: During development, the `build/config/vkBasalt.json` library_path may revert to relative path. Manually edit `build/config/vkBasalt.json` to fix `library_path` to absolute path: `/home/boux/repo/vkBasalt/build/src/libvkbasalt.so`. This is not needed for actual builds/releases.
