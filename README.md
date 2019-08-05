[![Build Status](https://travis-ci.org/libretro/melonDS.svg?branch=master)](https://travis-ci.org/libretro/melonDS)
[![Build status](https://ci.appveyor.com/api/projects/status/alninic8dnrw8cph/branch/master?svg=true)](https://ci.appveyor.com/project/bparker06/melonds/branch/master)

# melonDS

DS emulator, sorta

The goal is to do things right and fast, akin to blargSNES (but hopefully better). But also to, you know, have a fun challenge :)
<hr>

## How to use

melonDS requires BIOS/firmware copies from a DS. Files required:
 * bios7.bin, 16KB: ARM7 BIOS
 * bios9.bin, 4KB: ARM9 BIOS
 * firmware.bin, 128/256/512KB: firmware

Firmware boot requires a firmware dump from an original DS or DS Lite.
DS firmwares dumped from a DSi or 3DS aren't bootable and only contain configuration data, thus they are only suitable when booting games directly.

### Possible firmware sizes

 * 128KB: DSi/3DS DS-mode firmware (reduced size due to lacking bootcode)
 * 256KB: regular DS firmware
 * 512KB: iQue DS firmware

DS BIOS dumps from a DSi or 3DS can be used with no compatibility issues. DSi BIOS dumps (in DSi mode) are not compatible. Or maybe they are. I don't know.

As for the rest, the interface should be pretty straightforward. If you have a question, don't hesitate to ask, though!

## How to build

### Linux:

* Install dependencies:

```sh
sudo apt-get install gtk+-3.0 libcurl4-gnutls-dev libpcap0.8-dev libsdl2-dev
```

* Compile:

```sh
mkdir -p build
cd build
cmake ..
make -j$(nproc --all)
```

### Windows:

 * use CodeBlocks

#### MSYS2 and CMake

1. Install [MSYS2](https://www.msys2.org/)
2. Open the **MSYS2 MinGW 64-bit** terminal
3. Update the packages using `pacman -Syu` and reopen the terminal if it asks you to
4. Install dependencies: `pacman -S git make mingw-w64-x86_64-{cmake,mesa,SDL2,toolchain}`
5. Run the following commands
   ```bash
   git clone https://github.com/Arisotura/melonDS.git
   cd melonDS
   mkdir build
   cd build
   cmake .. -G "MSYS Makefiles"
   make -j$(nproc --all)
   ../msys-dist.sh
   ```

If everything went well, melonDS and the libraries it needs should now be in the `dist` folder.

## TODO LIST

 * DSi emulation
 * the impossible quest of pixel-perfect 3D graphics
 * improve libui and the emulator UI
 * support for rendering screens to separate windows
 * emulating some fancy addons
 * other non-core shit (debugger, graphics viewers, cheat crapo, etc)

### TODO LIST FOR LATER

 * better wifi
 * maybe emulate flashcarts or other fancy hardware
 * big-endian compatibility (Wii, etc)
 * LCD refresh time (used by some games for blending effects)
 * any feature you can eventually ask for that isn't outright stupid

## Credits

 * Martin for GBAtek, a good piece of documentation
 * Cydrak for the extra 3D GPU research
 * All of you comrades who have been testing melonDS, reporting issues, suggesting shit, etc

## License
[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

melonDS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
