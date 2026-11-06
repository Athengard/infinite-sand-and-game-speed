# RoadCraft Infinite Sand and Game Speed

A standalone Windows mod for RoadCraft. It adds an F10 menu with four game speed presets and a switch that keeps dump-truck beds at 70% sand while they unload.

## Requirements

This mod was tested with 64-bit RoadCraft for Windows, build `8.0.PATCH.772884`, in single-player. Other game builds may require a mod update. The compiled DLL runs directly with the game.

## Build

Use a 64-bit Windows build of TinyCC with its Windows headers and libraries. Open PowerShell in this repository and run `./build.ps1` if `tcc.exe` is on your PATH. Otherwise, pass the compiler path:

```powershell
./build.ps1 -Compiler 'path/to/tcc.exe'
```

The build produces `build/dinput8.dll`.

## Install or uninstall

1. Close RoadCraft.
2. Open the game's `bin/pc` directory beside `Roadcraft - Retail.exe`.
3. Back up any existing `dinput8.dll`. If it belongs to another mod, check that mod's loader instructions before replacing it.
4. Copy `build/dinput8.dll` into the game directory and start RoadCraft.

To uninstall, close the game, remove the DLL you copied, and restore your original `dinput8.dll` if you backed one up.

## Controls

- Press **F10** to open or hide the menu.
- Select **x1**, **x2**, **x3**, or **x5** to change the game speed. **x1** restores normal speed.
- Toggle **Sand 70%** to keep unloading without emptying the bed. The setting can affect other dump trucks on the map. Toggle it off to resume normal depletion.
- On an Xbox-style controller, hold **L3** (press in the left stick) and tap **Y** to step through the speed presets up to **x5**. Hold **L3** and tap **A** to reset to **x1**.
