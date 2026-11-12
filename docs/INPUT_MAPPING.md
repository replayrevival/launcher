# Input mapping file (`input_mapping.json`)

Written by the launcher's Controllers tab, read by the game once it has SDL3 input. Format version 1.
Until the game reads it, the launcher shows "Remapping applies after the next game update" (profile.json `launch.input_map.supported = false`; set it to `true` when the build reads the file).

## How the game finds it
- Path: `<launcher data dir>\input_mapping.json` (portable mode: `<launcher folder>\data\`).
- The launcher passes the absolute path in the environment variable named by profile.json `launch.input_map.env` (now `STE_INPUT_MAP`) every time it starts the game. If the variable is missing or the file is absent/damaged, the game uses its built-in defaults.
- The file is rewritten atomically (temp file + rename) at every change in the launcher and again just before the game starts. The game reads it once at start.

## Sources (SDL names)
Bindings use SDL3 gamepad names, so one file works for Xbox, PlayStation, Switch and generic pads:

| Source | Meaning |
|---|---|
| `a` `b` `x` `y` | the four face buttons by **position** (`a` = south, `b` = east, `x` = west, `y` = north), as in `SDL_GetGamepadStringForButton` |
| `back` `start` | Back/Share/Create/−, Start/Options/+ |
| `leftshoulder` `rightshoulder` | LB RB / L1 R1 / L R |
| `leftstick` `rightstick` | stick clicks (L3 R3), and as a value for the `move` / `look` actions: which physical stick moves / looks |
| `dpup` `dpdown` `dpleft` `dpright` | d-pad |
| `lefttrigger` `righttrigger` | analog triggers (a trigger bound to a button action counts as pressed above 50 %; a button bound to a trigger action counts as fully pressed) |
| `key:<SDL key name>` | keyboard, e.g. `key:Z`, `key:Left Shift`, `key:Return` (names from `SDL_GetKeyName`) |

`guide` is never bound (reserved for the in-game menu, see OVERLAY.md). `Esc` cannot be bound on the keyboard (it cancels listening).

## Actions
The game's logical controls (original Xbox pad), defined in the launcher's `config/controls.json`: `a b x y black white left_trigger right_trigger start back left_thumb right_thumb dpad_up dpad_down dpad_left dpad_right move look`.
`move` and `look` take `leftstick` or `rightstick` (swapping them swaps the sticks). Stick axes follow the chosen physical stick; there is no per-axis remap.
Defaults (assumption, to confirm with the game session): Black = left shoulder, White = right shoulder.

## Layout
```json
{
  "format": 1,
  "menu_swap_confirm_back": false,
  "ports": [
    {
      "port": 1,
      "device": "auto",
      "guid": "0300fa675e040000ff02000000007801",
      "name": "Xbox One Controller",
      "profile": "0300fa675e040000ff02000000007801",
      "settings": {
        "name": "Xbox One Controller",
        "deadzone": 0.15,
        "rumble": 100,
        "bindings": { "a": "a", "b": "b", "...": "..." }
      }
    }
  ],
  "profiles": { "<guid>": { "name": "...", "deadzone": 0.15, "rumble": 100, "bindings": {} } }
}
```
- `ports`: always 4 entries, port 1..4.
  - `device`: what the user chose. `"auto"` (the next connected pad, in connection order, that no port claims by GUID), `"keyboard"`, `"none"`, or a device GUID (`SDL_GUIDToString` of the joystick).
  - `guid` / `name`: what `device` resolved to **when the file was written**. Empty when nothing was attached.
  - `profile`: the key in `profiles` that applies (`"keyboard"`, a GUID, `"default"` = no pad was attached; `""` for `none`).
  - `settings`: **the effective settings for this port, already resolved.** The game should use these directly. This is the contract.
- `profiles`: everything the launcher remembers per device (by GUID; `keyboard` and `default` are special). The game can ignore it unless it resolves `auto` itself at start (a pad plugged in after the launcher closed): then it takes the port's `device`, resolves it to the pad's GUID, and uses `profiles[guid]` (falling back to `profiles["default"]`, then to its defaults).
- `deadzone`: 0.0 – 0.5, fraction of full stick travel ignored around the centre (radial). Default 0.15.
- `rumble`: 0 (off) – 100 %, scales both motors.
- `menu_swap_confirm_back`: menus use the east button to confirm and the south button to go back (Nintendo layout). Applies to menus only (launcher and overlay); gameplay uses `bindings`.
- Unknown keys must be ignored (forward compatibility). New `format` numbers mean an incompatible change.

Two pads of the same model have the same GUID and so share a profile.

## Launcher behaviour worth knowing
- Remapping a source that another action already uses swaps the two, so no source is ever bound twice (a stick can only swap with the other stick).
- "Reset buttons" restores `controls.json` defaults for the profile shown (pad profile or keyboard profile).
- Keyboard defaults come from the game's `docs/KEYBOARD.md`.
