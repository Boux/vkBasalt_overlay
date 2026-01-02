## Fork Notice
This is a fork of [vkBasalt](https://github.com/DadSchoorse/vkBasalt) with an experimental ImGui overlay for in-game effect configuration. Most of this fork was written with vibe-coding. I won't even pretend to own any of this code as I am not a C++ or Vulkan dev. I'm a webdev, I do CSS and I enjoy it. My monkey brain is too small for this low level stuff. I wanted these features in vkBasalt since forever so I just asked the AI to do it for me.

If you want to request features, feel free to do so, it's still pretty incomplete, and kind of buddy, it may or may not crash or freeze some games. One such case is adding a GPU hungry shader, like those CRT-Guest shaders, while your game is already using almost 100% of your GPU. This is a good plan to hard-freeze any game where the only solution is to kill it in a TTY or hold the power button. If you're gonna do that, limit your framerate.

---

# vkBasalt Overlay

A Vulkan post-processing layer with an in-game GUI for real-time effect configuration.

Feature showcase (slightly outdated): https://www.youtube.com/watch?v=_KJTToAynr0

<details>
  <summary>Click to view screenshots</summary>
  <img width="1920" height="1080" alt="Screenshot_20251231_184224" src="https://github.com/user-attachments/assets/06f05dfd-b429-4f1d-bb5d-b9d49a1719b1" />
  <img width="1920" height="1080" alt="Screenshot_20251231_183856" src="https://github.com/user-attachments/assets/3ba85dc9-d3de-4795-bd3a-6bbc2028e0dd" />
  <img width="1920" height="1080" alt="Screenshot_20251231_183700" src="https://github.com/user-attachments/assets/195e44df-1cd6-47bd-b543-5ee431b53483" />
</details>

## Bugs and Jank

- Mouse input can be weird in some games (especially first person shooters or games that hide your cursor). It will sometimes lock the cursor to the middle of the screen and you have to pause the game or open a menu to be able to use the mouse. Also be careful not to accidentally click things behind the overlay! I am not sure how to fix this issue, there is a really bad workaround that you can enable in the settings where it just force-grabs the cursor with X11 calls, but it's janky as hell. If anybody got any ideas I'm listening.

## Features

Upstream requires editing config files and restarting. This fork adds:

- **In-game overlay** (`End` key) with dockable windows
- **Add/remove/reorder effects** without restart
- **Parameter sliders** for all types (float, int, bool, vectors)
- **Preprocessor definitions** - edit `#define` values directly
- **Multiple effect instances** - use the same effect multiple times (e.g., cas.1, cas.2)
- **Save/load named configs**
- **Shader manager** - browse and test ReShade shaders
- **Diagnostics** - FPS, frame time, GPU/VRAM usage (AMD)
- **Debug window** - effect state, log viewer, error display
- **Auto-apply** - changes apply after configurable delay
- **Up to 200 effects** with VRAM estimates
- **Graceful error handling** - failed effects show errors instead of crashing

### ReShade Support

Try downloading shaders from these sources
- https://github.com/crosire/reshade-shaders
- https://github.com/HelelSingh/CRT-Guest-ReShade
- https://github.com/kevinlekiller/reshade-steam-proton

### Feature wishlist

- Effect injection without affecting game UI/HUD
- Native wayland support (for `PROTON_ENABLE_WAYLAND=1` and just native linux games under wayland)
- Per-game profiles instead of just have one global default config
- Fixing input grabbing to prevent click-throughs when the overlay is visible
- Reshade preset importation

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

## Known Limitations

- X11 only for keyboard input (Wayland not fully supported)
- Some ReShade shaders with multiple techniques may not work
- Depth buffer access is experimental
- Input blocking feature may cause freezes in some games

## Credits

- Original vkBasalt by [@DadSchoorse](https://github.com/DadSchoorse)
- ReShade shader compiler by [@crosire](https://github.com/crosire)
- ImGui by [@ocornut](https://github.com/ocornut)
