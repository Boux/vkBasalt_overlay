## Fork Notice
This is a fork of [vkBasalt](https://github.com/DadSchoorse/vkBasalt) with an experimental ImGui overlay for in-game effect configuration. Most of this fork was written with vibe-coding. I won't even pretend to own any of this code as I am not a C++ or Vulkan dev. I'm a webdev, I do CSS and I enjoy it. My monkey brain is too small for this low level stuff. I wanted these features in vkBasalt since forever so I just asked the AI to do it for me.

If you want to request features, feel free to do so, it's still pretty incomplete, and kind of buddy, it may or may not crash or freeze some games.

---

# vkBasalt Overlay

A Vulkan post-processing layer with an in-game GUI for real-time effect configuration.

Feature showcase: https://www.youtube.com/watch?v=_KJTToAynr0

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/16ff2926-9dbe-4d0b-83af-7552e3ed4c54" />

## Bugs and Jank

- Default config is sometimes not loaded properly and requires Apply to be clicked manually at least once (will fix soon).
- Slider values for some uniform types do not yet exist and will not properly reflect values needed by the shader (such as float3 values)

## Features

- **In-Game Overlay** - Press `End` to configure effects without leaving your game
- **Dockable Windows** - Drag tabs out to create separate floating windows
- **Effect Management** - Add, remove, reorder, and configure effects in real-time
- **Config Management** - Save and load named effect configurations
- **Shader Manager** - Manage ReShade shader paths and test shader compatibility
- **Diagnostics** - FPS, frame time graphs, GPU usage, and memory stats (AMD)
- **Up to 200 Effects** - Large effect chains with VRAM usage estimates

### Built-in Effects
- **CAS** - Contrast Adaptive Sharpening
- **DLS** - Denoised Luma Sharpening
- **FXAA** - Fast Approximate Anti-Aliasing
- **SMAA** - Enhanced Subpixel Morphological Anti-Aliasing
- **Deband** - Debanding filter
- **LUT** - 3D Color Lookup Table

### ReShade Support
Use ReShade FX shaders from the [reshade-shaders repository](https://github.com/crosire/reshade-shaders) or custom shaders.

## Installation

**Warning!** Make sur you uninstall the original vkBasalt if you want to use this fork, they both use the same env variables and will cause some collisions.

**AUR**
```
yay -S vkbasalt-overlay-git
```

**From source**
```bash
git clone https://github.com/Boux/vkBasalt_overlay.git
cd vkBasalt_overlay
meson setup --buildtype=release --prefix=/usr build-release
sudo ninja -C build-release install
```

## Usage

### Test with vkgears
```bash
ENABLE_VKBASALT=1 vkgears
```

### Steam
Add to launch options:
```
ENABLE_VKBASALT=1 %command%
```

### Lutris
1. Right-click game → Configure
2. System options → Environment variables
3. Add `ENABLE_VKBASALT` = `1`

## Configuration

Configuration is stored in `~/.config/vkBasalt-overlay/`. All required config files and subfolders will be generated when vkBasalt_overlay is executed at least once. Note that configs from the original vkBasalt are not compatible with this fork, which is why I've changed the name of the config folder.

### Key Bindings

| Key | Default | Description |
|-----|---------|-------------|
| Toggle Effects | `Home` | Enable/disable all effects |
| Reload Config | `F10` | Reload configuration file |
| Toggle Overlay | `End` | Show/hide the overlay GUI |

### Settings File

The main settings are stored in `~/.config/vkBasalt-overlay/vkBasalt.conf`:

```ini
# Maximum effects (requires restart, 1-200)
# Higher values use more VRAM (~8 bytes × width × height per slot)
maxEffects = 10

# Key bindings
toggleKey = Home
reloadKey = F10
overlayKey = End

# Startup behavior
enableOnLaunch = true
depthCapture = false

# Overlay options
overlayBlockInput = false
autoApplyDelay = 200  # ms delay before auto-applying changes
```

ReShade shader and texture paths are managed through the Shader Manager tab in the overlay.

### Saved Configs

Save named configurations through the overlay GUI. They are stored in `~/.config/vkBasalt-overlay/configs/`. You can set any saved config as the default.

## ReShade Shaders Setup

1. Download shaders from [reshade-shaders](https://github.com/crosire/reshade-shaders)
2. Copy `Shaders` folder to `~/.config/vkBasalt-overlay/reshade/Shaders`
3. Copy `Textures` folder to `~/.config/vkBasalt-overlay/reshade/Textures`
4. Open the overlay and add effects from the "ReShade" sections

## Debug Output

Set log level with environment variable:
```bash
VKBASALT_LOG_LEVEL=debug ENABLE_VKBASALT=1 %command%
```

Levels: `trace`, `debug`, `info`, `warn`, `error`, `none`

Output to file:
```bash
VKBASALT_LOG_FILE="vkBasalt.log"
```

## Known Limitations

- X11 only for keyboard input (Wayland not fully supported)
- Some ReShade shaders with multiple techniques may not work
- Depth buffer access is experimental
- Input blocking feature may cause freezes in some games

## Credits

- Original vkBasalt by [@DadSchoorse](https://github.com/DadSchoorse)
- ReShade shader compiler by [@crosire](https://github.com/crosire)
- ImGui by [@ocornut](https://github.com/ocornut)
