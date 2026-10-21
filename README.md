# Solwind

A small control center for laptops: **fan control** and **keyboard lighting**, made with [Quickshell](https://quickshell.org) to fit right into [Caelestia dots](https://github.com/caelestia-dots).

> [!NOTE]
> I used AI assistance while building this project. I reviewed parts of the code myself and tested every feature on my own laptop before publishing it.

> [!WARNING]
> **This is an early project, use it at your own risk.**
>
> - I've only tested it on my own **HP Victus 16-r0xxx** (Arch Linux, Hyprland). Other models and brands may not work.
> - The fan part **writes fan speeds to your hardware** through NBFC. A bad setting can make your laptop run hot, so keep an eye on the temperatures.
> - If Solwind crashes, its launcher or service hands the fans back to NBFC. Only if both are killed (for example with `kill -9`) do the fans stay at the last speed; `nbfc set -a` gives them back.

## Screenshots

| Fan control | Curve editor | Keyboard |
|---|---|---|
| ![Fan control](docs/screenshots/fan.png) | ![Curve editor](docs/screenshots/curve.png) | ![Keyboard lighting](docs/screenshots/keyboard.png) |

Colors follow your Caelestia theme:

| Fan control | Keyboard |
|---|---|
| ![Fan control, another theme](docs/screenshots/fan-theme2.png) | ![Keyboard, another theme](docs/screenshots/keyboard-theme2.png) |

## Features

**Fans** (through [NBFC-Linux](https://github.com/nbfc-linux/nbfc-linux)):
- Per fan: **Auto** (NBFC decides), **Fixed** or **Curve** (following the CPU, the GPU or the hotter one)
- Curve editor, profiles (Quiet, Balanced, Performance and your own), max fan button, a 10 minute graph
- Speeds up right away, slows down gently. Above 90 °C every fan it controls goes to 100%
- NBFC service controls and config picker, notifications, tray icon

**Keyboard:**
- Color, brightness, breathing and color cycle effects (also with your Caelestia theme colors)
- Detects your laptop and light, and only shows what your keyboard can do (`solwind kbd info`)

English and Turkish. Colors, rounding and fonts come from your Caelestia setup; Solwind only reads Caelestia's files.

### Supported keyboards

Solwind only uses the files a kernel driver offers in `/sys`, never the firmware or USB directly. Only the first row is tested on real hardware.

| Laptop | Kernel light | What works |
|---|---|---|
| HP Victus / OMEN, single-zone RGB | `hp::kbd_backlight` (extra module, [see below](#hp-laptops)) | Color, brightness, effects |
| HP OMEN / Victus, four-zone RGB | `hp::kbd_zoned_backlight-*` or `hp::kbd_backlight_zone0`–`3` (extra module) | Color (same on all zones), brightness |
| HP OMEN / Victus, four-zone RGB | `rgb_zones/zone00`–`zone03` (older copies of the module) | Color only, same on all zones |
| ASUS TUF with RGB | `asus::kbd_backlight` + `kbd_rgb_mode` | Static color, brightness. Never saves to the BIOS |
| ASUS (other), Lenovo, Chromebooks | `asus::`, `platform::`, `tpacpi::`, `chromeos::kbd_backlight` | Brightness |
| ASUS ROG, Lenovo Legion RGB | colors go through USB | Brightness only; use [asusctl](https://gitlab.com/asus-linux/asusctl) / [L5P-Keyboard-RGB](https://github.com/4JX/L5P-Keyboard-RGB) for colors |
| HP per-key RGB, HP white backlight, other OMEN modules | not offered by the kernel or the module | — |

Effects are only on for the HP single-zone keyboard, the one I could test them on.

### HP laptops

- **Keyboard colors** need the extra kernel module [hp-wmi-fan-and-backlight-control](https://github.com/TUXOV/hp-wmi-fan-and-backlight-control); the official kernel doesn't support HP keyboard lights yet (as of Linux 7.3). It only helps RGB keyboards.
- **Fans** need NBFC with a config for your exact model. Configs using ACPI methods also need `acpi_call` (`acpi_call-dkms` on Arch).

```sh
git clone https://github.com/TUXOV/hp-wmi-fan-and-backlight-control
cd hp-wmi-fan-and-backlight-control
make install-arch          # Arch (DKMS package); other distros: make && sudo make install-dkms
```

Reboot, then `ls /sys/class/leds | grep hp::` should list the light. Remove older copies of the module first (`dkms status`). Undo on Arch: `sudo pacman -R hp-wmi-fan-and-backlight-control-dkms`. Solwind never uses the module's fan controls.

## Installation

### Requirements

| Needed | Arch package |
|---|---|
| [Quickshell](https://quickshell.org) 0.3.x | `quickshell` / `quickshell-git` (AUR) |
| [Caelestia shell](https://github.com/caelestia-dots/shell) (Solwind won't start without it) | `caelestia-shell` (AUR) |
| Material Symbols font | `ttf-material-symbols-variable` |
| [NBFC-Linux](https://github.com/nbfc-linux/nbfc-linux) with a config for your laptop (fans) | `nbfc-linux` (AUR) |
| A polkit agent (password dialogs) | e.g. `hyprpolkitagent` |
| Optional: tray icon, NVIDIA temperature, GPU names | `python-gobject`, `nvidia-utils`, `pciutils` |

### NBFC

Solwind tells NBFC what to do, NBFC talks to the laptop. Set it up first ([full guide](https://github.com/nbfc-linux/nbfc-linux#readme)):

```sh
sudo nbfc update                       # download the laptop configs
nbfc config -r                         # configs that may fit your laptop
sudo nbfc config -s "<config name>"
sudo nbfc start --read-only            # try it safely: it only reads
nbfc status -a                         # do the fans and temperature look right?
sudo nbfc restart && sudo systemctl enable nbfc_service
```

If no config fits, don't force one. The keyboard part works without NBFC.

### Install

```sh
git clone https://github.com/kerimozgurtukenmez/Ccenter-Caelestia.git
cd Ccenter-Caelestia
sudo make install
```

It goes under `/usr/local` (command, app, menu entry, icon, background service). Then open **Solwind** from the app launcher or run `solwind`. On a first install the launcher may only show it after you log in again or restart the shell.

- **Update:** `git pull`, `solwind quit`, `sudo make install`, then `solwind` (`make install` prints the command if you use the background service).
- **Uninstall:** `solwind quit`, then `sudo make uninstall`. Turn the background service off first; it tells you if you forgot. Your settings stay in `~/.config/solwind`.
- **Packaging:** `make DESTDIR=/package/root PREFIX=/usr install`.

`make install` stops without changing anything while Solwind runs or when it finds an older install in your home folder.

**Coming from Ccenter** (Solwind's old name) or an older Solwind installed with `install.sh`: remove that copy first, without sudo, then install. Your settings are copied over on the first start.

```sh
ccenter quit        # or: solwind quit
make remove-old     # shows what it removes and asks
sudo make install
```

Keybinds or scripts that use `ccenter` need `solwind` now.

### Permissions

- **Keyboard light (optional):** your user needs write access to the light's files. Use **Grant permission** in the Keyboard tab (or `sudo make udev`); **Remove permission** undoes it. Only your user gets access.
- **NBFC service actions** (start, stop, applying a config) ask for your password every time. Setting fan speeds doesn't.

## Usage

- Closing the window doesn't quit Solwind, it keeps controlling the fans. Quit with the **Quit** button, the tray menu or `solwind quit`; the fans go back to NBFC first.
- **Start in the background on login** is a switch in the app (a systemd user service). If Solwind crashes there, the fans go back to NBFC and it starts again.
- Started from a terminal, it stays there and shows its log; Ctrl+C quits it cleanly. `solwind -d` starts it detached. If it's already running, `solwind` just opens the window.

```sh
solwind status              # fans, temperatures, active profile
solwind profile quiet       # apply a profile (no name: show the active one)
solwind boost 15            # max fan for 15 minutes (on / off)
solwind kbd color "#ff0000" # also: brightness, effect, source, speed, range, info
solwind log -f              # follow the log
solwind --help              # everything; solwind help kbd for one command
```

Exit codes: 0 done, 1 error, 2 wrong usage, 3 already running, 4 not running. For a Hyprland keybind: `hl.bind("SUPER + F9", hl.dsp.exec_cmd("solwind profile performance"))` (`/usr/local/bin` is on Hyprland's `PATH`, at least on mine). The window class is `solwind`.

## Reporting a problem

Run `solwind report` (or **Create report** at the bottom of a tab) and attach the file to a [GitHub issue](https://github.com/kerimozgurtukenmez/Ccenter-Caelestia/issues). It's one text file in your home folder with versions, your laptop model (no serial numbers), NBFC status, what Solwind detected, your settings and recent logs. Your user name and computer name are replaced, nothing is sent anywhere. Logs are cleared on reboot, so make it before rebooting.

## Undoing things

- Fans back to NBFC: `nbfc set -a`
- Solwind never edits NBFC config files, it only selects one. To go back: `sudo nbfc config --set "<config name>"` and `sudo nbfc restart`
- Keyboard permission: **Remove permission** in the app, or `sudo make uninstall-udev`

## Security

- Day-to-day use needs no admin rights; fan speeds go through `nbfc set`.
- Admin actions only happen when you click them, through your system's password dialog. Solwind never sees your password.
- The keyboard is only touched through the kernel. The keyboard permission is for your user only, and the rule is checked line by line before it's installed.
- NBFC config files and Caelestia's files are never changed.

## Development

Run it from the cloned folder with `./bin/solwind` (quit the installed copy first, only one runs at a time). The logic is in `services/`, `modules/` is only the UI. UI text is English in the code, wrapped in `I18n.t(…)`, with the Turkish text in `services/translations.js`.

## Thanks

[Caelestia](https://github.com/caelestia-dots), [Quickshell](https://quickshell.org), [NBFC-Linux](https://github.com/nbfc-linux/nbfc-linux) and everyone who made laptop configs for it, and [Material Symbols](https://fonts.google.com/icons).

## License

Copyright (C) 2026 Kerim Özgür Tükenmez

Solwind is free software under the **GNU General Public License version 3**, without any warranty; see [`LICENSE`](LICENSE).

The transparency math in `services/Colours.qml` is adapted from [Caelestia shell](https://github.com/caelestia-dots/shell) (GPL-3.0), whose QML plugin Solwind uses at runtime (not included). Quickshell (LGPL-3.0), NBFC-Linux (GPL-3.0) and Material Symbols (Apache-2.0) are separate and not included.

Solwind is an independent project, not made or supported by the Caelestia, Quickshell or NBFC developers or any laptop maker. Please report Solwind problems here.
