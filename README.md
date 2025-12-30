## Fork Notice
This is a fork of ![vkBasalt](https://github.com/DadSchoorse/vkBasalt) with an experimental ImGui overlay for in-game effect configuration. Most of this fork was written with vibe-coding. I won't even pretend to own any of this code as I am not a C++ or Vulkan dev. I'm a webdev, I do CSS and I enjoy it. My monkey brain is too small for this low level stuff. I wanted these features in vkBasalt since forever so I just asked the AI to do it for me.

If you want to request features, feel free to do so, it's still pretty incomplete, and kind of buddy, it may or may not crash or freeze some games.

---

# vkBasalt Overlay

A Vulkan post-processing layer with an in-game GUI for real-time effect configuration.

Feature showcase: https://www.youtube.com/watch?v=_KJTToAynr0

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/16ff2926-9dbe-4d0b-83af-7552e3ed4c54" />


## Features

### In-Game Overlay
- Press `End` (configurable) to toggle the overlay GUI
- Configure all effects without leaving your game
- Changes apply in real-time

### Effect Management
- **Add/Remove Effects:** Choose from built-in effects and ReShade shaders
- **Reorder Effects:** Drag effects to change rendering order
- **Enable/Disable:** Toggle individual effects on/off without removing them
- **Parameter Editing:** Adjust effect parameters with sliders, checkboxes, and dropdowns
- **Auto-Apply:** Enable automatic application of changes (200ms debounce)

### Config Management
- **Save Configs:** Save your effect configurations with custom names
- **Load Configs:** Switch between saved configurations instantly
- **Set Default:** Choose which config loads on game startup

### Settings
- Configure ReShade texture and shader paths
- Set key bindings for toggle, reload, and overlay
- Adjust maximum number of effects
- Enable/disable effects on launch
- Toggle depth capture (experimental)

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
# Paths for ReShade shaders
reshadeTexturePath = ~/.config/vkBasalt-overlay/reshade/Textures
reshadeIncludePath = ~/.config/vkBasalt-overlay/reshade/Shaders

# Maximum effects (requires restart)
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
```

### Per-Game Configs

Save game-specific configs through the overlay GUI. They are stored in `~/.config/vkBasalt-overlay/configs/`.

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
