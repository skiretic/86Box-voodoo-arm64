# Voodoo 4 4500 ROM Analysis

Date: 2026-03-11

Artifact: `V4_4500_AGP_SD_1.18.rom`

Method: `xxd`, `strings`, `ndisasm`, and targeted comparison against the current 86Box Banshee/Voodoo3 implementation.

## ROM Header and PCIR Data

### Header bytes

```text
00000000: 55 aa 50 eb 4d ...
00000018: 20 7c 00 00
00000020: "IBM VGA Compatible"
00000038: "08/18/03"
00000052: e9 96 00
00000057: "Voodoo4 4500 BIOS - Version 1.18"
```

- `Verified:` bytes `0x00-0x01` are `55 aa`, the standard legacy option ROM signature.
- `Verified:` byte `0x02` is `0x50`, which declares a ROM image length of `0x50 * 512 = 0xa000` bytes.
- `Verified:` the header contains a PCIR pointer at file offset `0x18`, pointing to `0x7c20`.
- `Verified:` the header contains the strings `IBM VGA Compatible`, `08/18/03`, and `Voodoo4 4500 BIOS - Version 1.18`.
- `Verified:` execution first jumps from the ROM header into file offset `0x0052`, then near-jumps into the main body at file offset `0x00eb`.
- `Verified:` the file on disk is `65536` bytes, larger than the declared image size.
- `Inferred:` bytes after the declared `0xa000` image are likely padding and/or appended tables preserved in the dump; they should not be assumed executable just because they are present in the file.

### PCIR block at `0x7c20`

```text
00007c20: 50 43 49 52 1a 12 09 00 a0 00 18 00 00 00 03 50
00007c30: 00 00 00 80 00 00 ...
```

Interpretation based on the PCI Firmware Specification layout:

- `Verified:` signature = `PCIR`
- `Verified:` vendor ID = `0x121a`
- `Verified:` device ID = `0x0009`
- `Verified:` structure length = `0x0018`
- `Verified:` class code bytes decode as `0x03 00 00` (`display controller`, VGA-compatible class family)
- `Verified:` image length = `0x0050` blocks = `0xa000` bytes
- `Verified:` code type = `0x00` (PC-AT compatible x86 image)
- `Verified:` indicator = `0x80` (last image)
- `Unknown:` why this dump is padded to `0x10000` bytes while the header and `PCIR` both declare `0xa000`

## Entry Path

### Main entry sequence

At normalized execution entry, the ROM does this:

```asm
push bp
call 0x3e6e
push cs
pop es
call 0x865c
jc   fail
call 0x83b2
call 0x8891
retf
```

- `Verified:` the first helper sets `DS` from a ROM data pointer stored at `cs:0x7710`.
- `Verified:` the second helper probes hardware state before continuing.
- `Verified:` the entry path is short and immediately delegates to larger initialization routines, which is consistent with a standard VGA option ROM structure.

### INT 10h / VBE dispatch

Later in the entry body, the ROM installs a standard BIOS service dispatcher:

```asm
mov si, ax
mov ds, [cs:0x7710]
shr si, 8
cmp ah, 0x4f
jz  vbe_handler
cmp ah, 0x1d
jnc iret
jmp [cs:si+0xfe]
```

- `Verified:` `AH=0x4f` is dispatched to a VBE handler.
- `Verified:` other low `INT 10h` functions are table-dispatched.
- `Verified:` this ROM is implementing classic VGA/VBE BIOS services, not just board-private startup.
- `Inferred:` any first-pass 86Box Voodoo 4 model that diverges from existing VGA/VBE infrastructure too early is high-risk.

## Important Helper Routines

### Segment and VGA helper routines

- File offset `0x3e6e`
  - `Verified:` loads `DS` from `cs:0x7710`
- File offset `0x3e74`
  - `Verified:` loads `ES` from `cs:0x7710`
- File offset `0x3e98`
  - `Verified:` sets bit `0x20` in sequencer register `0x01` via ports `0x3c4/0x3c5`
- File offset `0x3ea5`
  - `Verified:` clears the same sequencer bit
- File offset `0x3eb2`
  - `Verified:` generic indexed VGA port read helper
- File offset `0x3fd8`
  - `Verified:` chooses CRT controller port base `0x3b4` vs `0x3d4` using `0x3cc`
- File offset `0x3c20`
  - `Verified:` reads CRTC extension index `0x1c` and returns a `DX` base derived from it

### PCI BIOS helper routines

- File offsets `0x3c35`, `0x3dde`, `0x3e08`
  - `Verified:` use PCI BIOS `INT 1Ah` functions such as `B102h`, `B108h`, `B109h`, and `B10Ch`
- File offset `0x3462`
  - `Verified:` starts with `AX=B102h`, `CX=0x0009`, `DX=0x121a`
  - `Verified:` the ROM explicitly asks PCI BIOS to find a `3dfx` device with device ID `0x0009`

## Likely Initialization Routines

### File offset `0x3462`

This is the clearest early hardware-programming path found so far.

Observed behavior:

- `Verified:` performs PCI BIOS device discovery for `121a:0009`
- `Verified:` touches offsets `+0x1a`, `+0x1d`, `+0x40`, `+0x4c`, `+0x5c`, `+0x70`, `+0x98`, `+0xe4`, and `+0xe8` from the base returned by the CRTC-extension helper path
- `Verified:` reads-modifies-writes `+0x28`, `+0x2c`, `+0x5c`, `+0x1c`, and `+0x70`
- `Verified:` writes zero to `+0x4c` and `+0xe4`, and later programs `+0x98` and `+0xe8`

### File offset `0x36a3`

- `Verified:` reads a 32-bit value from `base + 0x28`, ORs bit `0x40`, and writes it back
- `Inferred:` this is a `VGAINIT0`-style enable path, not a command FIFO operation, because the access is through the ROM’s port-derived extended register base

### File offset `0x3af3`

- `Verified:` uses `base + 0x28` and `base + 0x2c` together
- `Inferred:` this is likely paired VGA register programming for mode/bank/display behavior

### File offsets `0x86f0` and `0x8733`

- `Verified:` these later routines also rediscover the device through PCI BIOS and touch offsets including `+0x10`, `+0x18`, `+0x1c`, `+0x24`, `+0x28`, `+0x34`, and `+0x5c`
- `Verified:` constants used by this logic live near file offset `0x7be0`
- `Unknown:` whether these later paths are mode-set-time only, POST-only, or both

## Register Access Patterns Observed in the ROM

| Offset | Observed ROM behavior | Status |
| --- | --- | --- |
| `0x1c` | Read-modify-write; later ORs bit `0x01` | `Verified` |
| `0x28` | Read-modify-write; ORs bit `0x40` in one path | `Verified` |
| `0x2c` | Written after packing bits from mode/state info | `Verified` |
| `0x40` | Written during early init sequence | `Verified` |
| `0x4c` | Zeroed during early init sequence | `Verified` |
| `0x5c` | Read-masked-written during early init sequence | `Verified` |
| `0x70` | Read-masked-written during early init sequence | `Verified` |
| `0x98` | Programmed during early init sequence | `Verified` |
| `0xe4` | Zeroed or written during init and helper paths | `Verified` |
| `0xe8` | Programmed during init and helper paths | `Verified` |

## What the ROM Clearly Proves

- `Verified:` Voodoo 4 4500 BIOS initialization is not detached from legacy VGA and VBE behavior.
- `Verified:` the ROM expects PCI identity `121a:0009`.
- `Verified:` the ROM uses a Banshee/Voodoo3-style extended register block shape with offsets that closely match public `tdfx` documentation.
- `Inferred:` early VGA/display bring-up should be investigated as an extension of Banshee/Voodoo3 behavior, not as a clean-sheet VSA-100-only path.

## What the ROM Does Not Yet Prove

- `Unknown:` exact semantic meaning of every touched offset, especially `0x70` in the Voodoo 4 path
- `Unknown:` which VSA-100-specific behaviors are required after BIOS POST for protected-mode drivers
- `Unknown:` whether any of the touched registers need different reset values than current 86Box Banshee/Voodoo3 defaults
