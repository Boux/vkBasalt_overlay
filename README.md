## Fork Notice
This is a fork with an experimental ImGui overlay for in-game effect configuration. Most of this fork was written with vibe-coding. I won't even pretend to own any of this code as I am not a C++ or Vulkan dev. I'm a webdev, I do CSS and I enjoy it. My monkey brain is too small for this low level stuff. I wanted these features in vkBasalt since forever so I just asked the AI to do it for me.

### How to try out the overlay GUI branch (without replacing system vkBasalt)
```
git clone https://github.com/Boux/vkBasalt_overlay.git
cd vkBasalt_overlay
git checkout feature/imgui
meson setup --buildtype=debug ./build
ninja -C ./build
```
If it fails to build, you are probably missing dependencies.

Edit `./build/config/vkBasalt.json` and set `library_path` to the absolute path of the built library:
```json
"library_path": "/path/to/vkBasalt_overlay/build/src/libvkbasalt.so"
```

Test with vkgears to see if it works
```
VK_ADD_IMPLICIT_LAYER_PATH=/path/to/vkBasalt_overlay/build/config ENABLE_VKBASALT=1 vkgears
```

Steam game launch options (I have only tested with Tunic as of now)
```
VK_ADD_IMPLICIT_LAYER_PATH=/path/to/vkBasalt_overlay/build/config ENABLE_VKBASALT=1 %command%
```

You should then be able to press `F11` to show the overlay GUI in-game.
you can change this keybinding in your `vkBasalt.conf` with `overlayKey = <key>`.

**Warning** I have not tested with Nvidia, only AMD.

<img width="1920" height="961" alt="image" src="https://github.com/user-attachments/assets/cc2ff254-03a5-455b-8896-efedc0b28cdd" />

## Fork-specific features

### On master branch
**Real-time config reloading**
- Any modifications you do to your config file should apply instantly in-game without needing to re-open the game (too many modifications may or may not crash your game).

### On feature/imgui or feature/ingui_stable branches
**Toggle Overlay**
- Press `F11` (default) to show/hide the overlay
- Configurable via `overlayKey` in vkBasalt.conf

**Effect Selection**
- Click "Select Effects..." to open the effect picker
- Effects are organized into three categories:
  - **Built-in:** cas, dls, fxaa, smaa, deband, lut
  - **ReShade (current config):** effects defined in your game-specific config
  - **ReShade (all):** effects from your default vkBasalt.conf
- Select up to 10 effects (configurable via `maxEffects`)

**Effect Management**
- **Reorder effects:** Drag the `::` handle to change effect order (affects rendering order)
- **Enable/disable effects:** Toggle the checkbox next to each effect
- **Edit parameters:** Expand an effect to see and modify its parameters (sliders, checkboxes, dropdowns)

**Apply Changes**
- Click "Apply" to apply your changes
- Or enable "Apply automatically" for live preview (changes apply after 200ms debounce)

#### Overlay Config Options

Add these to your `vkBasalt.conf`:

| Option | Default | Description |
|--------|---------|-------------|
| `overlayKey` | `F11` | Key to toggle the overlay |
| `maxEffects` | `10` | Maximum number of effects that can be selected |

---
# vkBasalt

vkBasalt is a Vulkan post processing layer to enhance the visual graphics of games.

Currently, the build in effects are:
- Contrast Adaptive Sharpening
- Denoised Luma Sharpening
- Fast Approximate Anti-Aliasing
- Enhanced Subpixel Morphological Anti-Aliasing
- 3D color LookUp Table

It is also possible to use Reshade Fx shaders.

## Disclaimer
This is one of my first projects ever, so expect it to have bugs. Use it at your own risk.

## Building from Source

### Dependencies
Before building, you will need:
- GCC >= 9
- X11 development files
- glslang
- SPIR-V Headers
- Vulkan Headers

### Building

**These instructions use `--prefix=/usr`, which is generally not recommened since vkBasalt will be installed in directories that are meant for the package manager. The alternative is not setting the prefix, it will then be installed in `/usr/local`. But you need to make sure that `ld` finds the library since /usr/local is very likely not in the default path.**

In general, prefer using distro provided packages.

```
git clone https://github.com/DadSchoorse/vkBasalt.git
cd vkBasalt
```

#### 64bit

```
meson setup --buildtype=release --prefix=/usr builddir
ninja -C builddir install
```
#### 32bit

Make sure that `PKG_CONFIG_PATH=/usr/lib32/pkgconfig` and `--libdir=lib32` are correct for your distro and change them if needed. On Debian based distros you need to replace `lib32` with `lib/i386-linux-gnu`, for example.
```
ASFLAGS=--32 CFLAGS=-m32 CXXFLAGS=-m32 PKG_CONFIG_PATH=/usr/lib32/pkgconfig meson setup --prefix=/usr --buildtype=release --libdir=lib32 -Dwith_json=false builddir.32
ninja -C builddir.32 install
```

## Packaging status

[Debian](https://tracker.debian.org/pkg/vkbasalt) `sudo apt install vkbasalt`

[Fedora](https://src.fedoraproject.org/rpms/vkBasalt) `sudo dnf install vkBasalt`

[Void Linux](https://github.com/void-linux/void-packages/blob/master/srcpkgs/vkBasalt/template) `sudo xbps-install vkBasalt`

## Usage
Enable the layer with the environment variable.

### Standard
When using the terminal or an application (.desktop) file, execute:
```ini
ENABLE_VKBASALT=1 yourgame
```

### Lutris
With Lutris, follow these steps below:
1. Right click on a game, and press `configure`.
2. Go to the `System options` tab and scroll down to `Environment variables`.
3. Press on `Add`, and add `ENABLE_VKBASALT` under `Key`, and add `1` under `Value`.

### Steam
With Steam, edit your launch options and add:
```ini
ENABLE_VKBASALT=1 %command%
```

## Configure

Settings like the CAS sharpening strength can be changed in the config file.
The config file will be searched for in the following locations:
* a file set with the environment variable`VKBASALT_CONFIG_FILE=/path/to/vkBasalt.conf`
* `vkBasalt.conf` in the working directory of the game
* `$XDG_CONFIG_HOME/vkBasalt/vkBasalt.conf` or `~/.config/vkBasalt/vkBasalt.conf` if `XDG_CONFIG_HOME` is not set
* `$XDG_DATA_HOME/vkBasalt/vkBasalt.conf` or `~/.local/share/vkBasalt/vkBasalt.conf` if `XDG_DATA_HOME` is not set
* `/etc/vkBasalt.conf`
* `/etc/vkBasalt/vkBasalt.conf`
* `/usr/share/vkBasalt/vkBasalt.conf`

If you want to make changes for one game only, you can create a file named `vkBasalt.conf` in the working directory of the game and change the values there.

#### Reshade Fx shaders

To run reshade fx shaders e.g. shaders from the [reshade repo](https://github.com/crosire/reshade-shaders), you have to set `reshadeTexturePath` and `reshadeIncludePath` to the matching dirctories from the repo. To then use a specific shader you need to set a custom effect name to the shader path and then add that effect name to `effects` like every other effect.

```ini
effects = colorfulness:denoise

colorfulness = /home/user/reshade-shaders/Shaders/Colourfulness.fx
denoise = /home/user/reshade-shaders/Shaders/Denoise.fx
reshadeTexturePath = /home/user/reshade-shaders/Textures
reshadeIncludePath = /home/user/reshade-shaders/Shaders
```

#### Ingame Input

The [HOME key](https://en.wikipedia.org/wiki/Home_key) can be used to disable and re-enable the applied effects, the key can also be changed in the config file. This is based on X11 so it won't work on pure wayland. It **should** however at least not crash without X11.


#### Debug Output

The amount of debug output can be set with the `VKBASALT_LOG_LEVEL` env var, e.g. `VKBASALT_LOG_LEVEL=debug`. Possible values are: `trace, debug, info, warn, error, none`.

By default the logger outputs to stderr, a file as output location can be set with the `VKBASALT_LOG_FILE` env var, e.g. `VKBASALT_LOG_FILE="vkBasalt.log"`.


## FAQ

#### Why is it called vkBasalt?
It's a joke: vulkan post processing &#8594; after vulcan &#8594; basalt
#### Does vkBasalt work with dxvk and vkd3d?
Yes.
#### Will vkBasalt get me banned?
Maybe. To my knowledge this hasn't happened yet but don't blame me if your frog dies.
#### Will there be a openGl version?
No. I don't know anything about openGl and I don't want to either. Also openGl has no layer system like vulkan.
#### Will there be a GUI in the future?
Maybe, but not soon.
#### So is vkBasalt just a reshade port for linux?
Not really, most of the code was written from scratch. vkBasalt directly uses reshade source code for the shader compiler (thanks [@crosire](https://github.com/crosire)), but that's about it.
#### Does every reshade shader work?
No. Shaders that need multiple techniques do not work, there might still be problems with stencil and blending and depth buffer access isn't ready yet.
#### You said that "depth buffer access isn't ready yet", what does this mean?
There is a wip version that you can enable with `depthCapture = on`. It will lead to many problems especially on non nvidia hardware. Also the selected depth buffer isn't always the one you would want.
#### Is there a way to change settings for reshade shaders?
There is some support for it [#46](https://github.com/DadSchoorse/vkBasalt/pull/46). One easy way so to simply edit the shader file.
