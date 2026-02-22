<p align="center">
  <img src="https://raw.githubusercontent.com/86Box/86Box/master/src/qt/images/86Box-gray.png" width="128" alt="86Box logo">
</p>

<h1 align="center">86Box</h1>

<p align="center">
  <b>A low-level x86 PC emulator</b>
</p>

<p align="center">
  <a href="COPYING"><img src="https://img.shields.io/github/license/86Box/86Box" alt="License"></a>
  <a href="https://github.com/86Box/86Box/releases/latest"><img src="https://img.shields.io/github/v/release/86Box/86Box" alt="Latest Release"></a>
  <a href="https://discord.gg/QXK9XTv"><img src="https://img.shields.io/discord/262614059009048590?label=Discord&logo=discord&logoColor=white" alt="Discord"></a>
</p>

---

**86Box** is an IBM PC system emulator that specializes in running older operating systems and software designed for IBM PC systems and compatibles from 1981 through fairly recent system designs based on the PCI bus.

Unlike higher-level emulators that approximate hardware behavior, 86Box performs low-level emulation of actual hardware — including cycle-accurate CPU timing, real chipset logic, and authentic peripheral behavior. The result is high compatibility with vintage software that depends on precise hardware quirks.

## Features

- **Accurate CPU emulation** — Intel 8088 through Pentium II / Celeron (Mendocino), AMD K5 through K6-III+, Cyrix 6x86/MII, VIA Cyrix III, IDT WinChip. Optional dynamic recompiler for x86-64 and ARM64 hosts.
- **450+ machine configurations** — from the original IBM PC 5150 (1981) to Socket 370 Celeron systems, including IBM PS/2 (MCA), Compaq, and dozens of third-party motherboards from ASUS, Gigabyte, MSI, Acer, Dell, and more.
- **3D accelerated graphics** — 3dfx Voodoo Graphics, Voodoo 2, Voodoo Banshee, and Voodoo 3 with JIT-compiled pixel pipelines. S3 ViRGE/Trio3D, ATI Mach64, Matrox Millennium/Mystique, Trident TGUI, Cirrus Logic GD5400-5480, Tseng ET4000/W32, and many more — from CGA and Hercules through SVGA.
- **Sound** — Sound Blaster (1.0 through AWE64 Gold), Gravis Ultrasound, Pro Audio Spectrum, Roland MPU-401, AdLib, ESS AudioDrive, Windows Sound System, Ensoniq AudioPCI, C-Media CMI8738. MIDI output via FluidSynth or emulated Roland MT-32/CM-32L (via munt).
- **Networking** — NE1000/NE2000, 3Com 3C501/3C503, AMD PCnet, DEC Tulip, Realtek RTL8139C+. SLiRP (NAT) or bridged networking.
- **Storage** — MFM/RLL/ESDI/IDE/SCSI hard disks, floppy drives (360K through 2.88M), CD-ROM, MO drives. SCSI controllers from Adaptec, BusLogic, NCR, and AMD. Hard disk images in raw, VHD, HDX formats.
- **Peripheral cards** — ISA, MCA, VLB, PCI, and AGP buses. Serial/parallel ports, game ports, joysticks, printers (ESC/P, PostScript), MIDI interfaces.
- **Cross-platform** — Windows, Linux, macOS (including Apple Silicon), and FreeBSD. Qt5/Qt6 or SDL user interface.

## Screenshots

<p align="center">
  <i>(Coming soon — contributions welcome!)</i>
</p>

## Getting Started

### Download

Pre-built binaries are available from the [Releases](https://github.com/86Box/86Box/releases) page for Windows, Linux, and macOS.

You will also need ROM files for the machines and devices you want to emulate. See the [documentation](https://86box.readthedocs.io/en/latest/) for details on ROM setup.

### Quick start

1. Download and extract the latest release for your platform
2. Place ROM files in the `roms/` directory alongside the executable
3. Run `86Box` — the settings dialog will appear to configure your first virtual machine
4. Alternatively, use `--vmpath`/`-P <directory>` to specify a VM directory from the command line

### VM Managers

For easier handling of multiple virtual machines:

| Manager | Platforms | Author |
|---------|-----------|--------|
| [Avalonia 86](https://github.com/notBald/Avalonia86) | Windows, Linux | notBald |
| [86Box Manager](https://github.com/86Box/86BoxManager) | Windows | Overdoze |
| [86Box Manager X](https://github.com/RetBox/86BoxManagerX) | Cross-platform (Avalonia) | xafero |
| [MacBox](https://github.com/Moonif/MacBox) | macOS | Moonif |
| [sl86](https://github.com/DDXofficial/sl86) | CLI (Python) | DDX |

## Building from Source

86Box uses CMake (3.15+). See the full [build guide](https://86box.readthedocs.io/en/latest/dev/buildguide.html) for detailed instructions.

### Dependencies

**macOS** (Homebrew):
```bash
brew install cmake sdl2 rtmidi openal-soft fluidsynth libslirp libserialport qt@5
```

**Ubuntu/Debian**:
```bash
sudo apt install build-essential cmake libsdl2-dev librtmidi-dev libopenal-dev \
    libfluidsynth-dev libslirp-dev libfreetype-dev libpng-dev libsndfile1-dev \
    qttools5-dev qtbase5-dev
```

**Fedora**:
```bash
sudo dnf install cmake gcc-c++ SDL2-devel rtmidi-devel openal-soft-devel \
    fluidsynth-devel libslirp-devel freetype-devel libpng-devel libsndfile-devel \
    qt5-qtbase-devel qt5-qttools-devel
```

### Build

```bash
cmake -S . -B build --preset regular -D QT=ON
cmake --build build -j$(nproc)
cmake --install build
```

### Key CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `QT` | `ON` | Qt user interface (OFF for SDL-only) |
| `NEW_DYNAREC` | `OFF` | PCem v15 dynamic recompiler — required and auto-forced on ARM64 |
| `DEV_BRANCH` | `OFF` | Enable experimental/in-development hardware |
| `GDBSTUB` | `OFF` | GDB stub for debugging guest code |

## System Requirements

| | Minimum | Recommended |
|---|---------|-------------|
| **CPU** | Any 64-bit x86 or ARM64 processor | Fast single-thread performance (IPC matters more than core count) |
| **RAM** | 4 GB | 8 GB+ |
| **OS** | Windows 10+, macOS 11+, Ubuntu 20.04+ / equivalent | Latest stable release |
| **Disk** | ~200 MB (app + ROMs) | SSD recommended for disk image performance |

Most emulation logic runs in a single thread, so processors with higher IPC (instructions per clock) will emulate faster guest clock speeds.

## Documentation

Full documentation is available at **[86box.readthedocs.io](https://86box.readthedocs.io/en/latest/)**, covering:

- User interface and configuration
- Machine-specific notes and quirks
- ROM management
- Networking setup
- Disk image formats
- Build guide and developer API

## Community

| | |
|---|---|
| **Discord** | [discord.gg/QXK9XTv](https://discord.gg/QXK9XTv) |
| **IRC** | [#86Box on irc.ringoflightning.net](https://kiwiirc.com/client/irc.ringoflightning.net/?nick=86box|?#86Box) |
| **Forum** | [forum.softhistory.org](https://forum.softhistory.org/) |
| **Wiki** | [wiki.softhistory.org](https://wiki.softhistory.org/) |
| **YouTube** | [youtube.com/c/86Box](https://youtube.com/c/86Box) |

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

86Box is released under the [GNU General Public License, version 2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) or later. See [COPYING](COPYING) for details.

The emulator optionally uses [munt](https://github.com/munt/munt), [FluidSynth](https://www.fluidsynth.org/), [Ghostscript](https://www.ghostscript.com/), and [Discord Game SDK](https://discord.com/developers/docs/game-sdk/sdk-starter-guide), distributed under their respective licenses.

## Support the Project

86Box is free and open source. If you'd like to support development:

- **PayPal**: [paypal.me/86Box](https://paypal.me/86Box)
- **Patreon**: [patreon.com/86box](https://www.patreon.com/86box)
