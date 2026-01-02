# vkBasalt Config System

This document explains how configuration loading and handling works in vkBasalt.

## Overview

vkBasalt uses a two-config architecture:

```
┌─────────────────────────────────────────────────────────────┐
│                      pBaseConfig                            │
│              (always vkBasalt.conf)                         │
│                                                             │
│  Contains: reshadeIncludePath, reshadeTexturePath,          │
│            effect definitions (*.fx mappings),              │
│            system-wide defaults                             │
└─────────────────────────────────────────────────────────────┘
                           ▲
                           │ fallback
                           │
┌─────────────────────────────────────────────────────────────┐
│                       pConfig                               │
│              (current active config)                        │
│                                                             │
│  Can be: vkBasalt.conf (same as base)                       │
│          OR a user config from ~/.config/vkBasalt/configs/  │
│                                                             │
│  Contains: effects list, effect parameters                  │
│  Falls back to pBaseConfig for missing options              │
└─────────────────────────────────────────────────────────────┘
```

## Config Class (`config.hpp` / `config.cpp`)

The `Config` class represents a single configuration file.

### Constructors

```cpp
Config();                        // Finds and loads vkBasalt.conf from standard paths
Config(const std::string& path); // Loads a specific config file
Config(const Config& other);     // Copy constructor
```

### Standard Config Paths (checked in order)

1. `$XDG_CONFIG_HOME/vkBasalt/vkBasalt.conf`
2. `$XDG_DATA_HOME/vkBasalt/vkBasalt.conf`
3. `/etc/vkBasalt.conf`
4. `/etc/vkBasalt/vkBasalt.conf`
5. `/usr/share/vkBasalt/vkBasalt.conf`

### Config File Format

```ini
# Comments start with #
reshadeIncludePath = /path/to/shaders
reshadeTexturePath = /path/to/textures

# Effect definitions (name = path to .fx file)
Clarity = /path/to/Clarity.fx
SMAA = /path/to/SMAA.fx

# Parameters
casSharpness = 0.4
debandRange = 16.0

# Effects list (colon-separated)
effects = cas:deband:Clarity
```

### Key Methods

| Method | Description |
|--------|-------------|
| `getOption<T>(name, default)` | Get option value with fallback support |
| `setFallback(Config*)` | Set fallback config for missing options |
| `setOverride(name, value)` | Set in-memory override (doesn't modify file) |
| `clearOverrides()` | Clear all in-memory overrides |
| `reload()` | Reload config from file |
| `hasConfigChanged()` | Check if file modified since last load |
| `getEffectDefinitions()` | Get all effect name -> .fx path mappings |

### Option Resolution Order

When `getOption()` is called:

```
1. Check overrides map (in-memory values from overlay)
   ↓ not found
2. Check options map (values from config file)
   ↓ not found
3. Check fallback config (pBaseConfig)
   ↓ not found
4. Return default value
```

## Initialization (`basalt.cpp`)

### Global Config Pointers

```cpp
std::shared_ptr<Config> pBaseConfig = nullptr;  // Always vkBasalt.conf
std::shared_ptr<Config> pConfig = nullptr;      // Current active config
```

### `initConfigs()` Function

Called when the Vulkan layer initializes:

```
1. Load pBaseConfig (vkBasalt.conf from standard paths)

2. Determine current config path:
   a. Check VKBASALT_CONFIG_FILE environment variable
   b. Check ~/.config/vkBasalt/default_config file
   c. If neither, use pBaseConfig

3. Load pConfig from determined path

4. Set pConfig->setFallback(pBaseConfig) so missing options
   fall back to base config
```

### `switchConfig(path)` Function

Called when user selects a different config from the overlay:

```cpp
void switchConfig(const std::string& configPath)
{
    pConfig = std::make_shared<Config>(configPath);
    pConfig->setFallback(pBaseConfig.get());
    cachedParams.dirty = true;  // Invalidate parameter cache
}
```

## Effect Discovery

Effects available in the overlay come from three sources:

### 1. Config Definitions

Effects explicitly defined in config files (key = path.fx):

```ini
Clarity = /path/to/Clarity.fx
SMAA = /path/to/SMAA.fx
```

### 2. Base Config Definitions

Effects defined in vkBasalt.conf that aren't in the current config.

### 3. Auto-Discovery

The overlay automatically scans `reshadeIncludePath` for .fx files:

```
reshadeIncludePath = /home/user/.config/vkBasalt/reshade/Shaders
```

All .fx files in this directory are available as effects, using the filename (without .fx) as the effect name.

**Discovery order:**
1. Current config definitions (highest priority)
2. Base config definitions
3. Auto-discovered .fx files (sorted alphabetically)

Effects already defined in config are not duplicated from auto-discovery.

## User Configs (`config_serializer.hpp` / `config_serializer.cpp`)

User configs are stored in `~/.config/vkBasalt/configs/`.

### ConfigSerializer Methods

| Method | Description |
|--------|-------------|
| `getConfigsDir()` | Returns `~/.config/vkBasalt/configs` |
| `listConfigs()` | Lists all .conf files in configs dir |
| `saveConfig(name, effects, params)` | Saves a config file |
| `deleteConfig(name)` | Deletes a config file |
| `setDefaultConfig(name)` | Sets default config in `default_config` file |
| `getDefaultConfig()` | Gets default config name from file |

### Saved Config Format

```ini
# effect_name
paramName = value

# cas
casSharpness = 0.800000

# Clarity
ClarityRadius = 3

effects = cas:Clarity:deband
disabledEffects = deband
```

- `effects`: All effects in the list (enabled + disabled), preserves order
- `disabledEffects`: Effects that are unchecked (in list but not rendered)
- Parameters are saved for all effects, including disabled ones

### Default Config File

The file `~/.config/vkBasalt/default_config` contains just the name of the default config (without .conf extension):

```
tunic
```

This config will be loaded automatically instead of vkBasalt.conf.

## Caching

To avoid parsing configs every frame, vkBasalt uses caching:

### Effects Cache (`cachedEffects`)

```cpp
struct CachedEffectsData {
    std::vector<std::string> currentConfigEffects;  // From pConfig
    std::vector<std::string> defaultConfigEffects;  // From pBaseConfig
    std::map<std::string, std::string> effectPaths; // Effect -> .fx path
    std::string configPath;
    bool initialized = false;
};
```

### Parameters Cache (`cachedParams`)

```cpp
struct CachedParametersData {
    std::vector<EffectParameter> parameters;
    std::vector<std::string> effectNames;
    std::string configPath;
    bool dirty = true;  // Set true to force recollection
};
```

Parameters are only recollected when:
- `dirty` flag is set (after config switch/reload)
- Effect list changes
- Config path changes

## Overlay Interaction

The ImGui overlay can modify config behavior:

### In-Memory Overrides

When user adjusts a slider in the overlay:
```cpp
pConfig->setOverride("casSharpness", "0.8");
```

This doesn't modify the config file - it sets an in-memory override that takes precedence over file values.

### Applying Changes

When "Apply" is clicked, overrides are set and effects are reloaded to pick up the new values.

### Saving Config

When "Save" is clicked, `ConfigSerializer::saveConfig()` writes the current effects list and modified parameters to a file.

## Hot Reload

vkBasalt supports hot-reloading configs:

1. Each frame, `pConfig->hasConfigChanged()` checks file modification time
2. If changed, `pConfig->reload()` re-reads the file
3. Cache is invalidated: `cachedEffects.initialized = false`, `cachedParams.dirty = true`
4. Effects are reloaded with new parameters

## Flow Diagram

```
Game Launch
    │
    ▼
initConfigs()
    │
    ├─► Load pBaseConfig (vkBasalt.conf)
    │
    ├─► Check VKBASALT_CONFIG_FILE env var
    │   OR check default_config file
    │
    ├─► Load pConfig (user config or base)
    │
    └─► Set pConfig fallback to pBaseConfig

    │
    ▼
Each Frame (vkQueuePresentKHR)
    │
    ├─► Check for config file changes (hot reload)
    │
    ├─► If overlay visible:
    │   ├─► Update overlay state
    │   ├─► Collect parameters (if cache dirty)
    │   └─► Render overlay
    │
    ├─► If overlay requests config switch:
    │   └─► switchConfig(newPath)
    │
    └─► Apply effects with current config values
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `VKBASALT_CONFIG_FILE` | Override config file path |
| `VKBASALT_LOG_LEVEL` | Set logging level |
| `ENABLE_VKBASALT_OVERLAY` | Enable/disable vkBasalt-overlay (0 or 1) |
