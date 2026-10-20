# Ccenter-Caelestia

A small control center for laptops: **fan control** and **keyboard lighting**, made with [Quickshell](https://quickshell.org) to fit right into [Caelestia dots](https://github.com/caelestia-dots).

> [!NOTE]
> I used AI assistance while building this project. I reviewed parts of the code myself and tested every feature on my own laptop before publishing it.

> [!WARNING]
> **This is an early project, use it at your own risk.**
>
> - I've only been able to test it on my own laptop, an **HP Victus 16-r0xxx** (i7-13700H, RTX 3050, Arch Linux, Hyprland). It may not work on other HP models or on other brands.
>   - Fan control needs a working NBFC config for your exact model.
>   - The keyboard part is only tested on HP's single-zone RGB keyboard. Other keyboards in the table below are supported based on the kernel drivers, but nobody has tried them on real hardware yet.
> - The fan part **writes fan speeds to your hardware** through NBFC. A bad setting can make your laptop run hot, so keep an eye on the temperatures.
> - In *Fixed* and *Curve* mode Ccenter drives the fans itself. Closing the window keeps it running in the background, and quitting hands the fans back to NBFC. If it gets killed some other way, the fans **stay at the last speed**; `nbfc set -a` gives them back to NBFC.
> - Things can still change or break. The name is temporary too.

## Screenshots

| Fan control | Curve editor | Keyboard |
|---|---|---|
| ![Fan control](docs/screenshots/fan.png) | ![Curve editor](docs/screenshots/curve.png) | ![Keyboard lighting](docs/screenshots/keyboard.png) |

Colors follow your Caelestia theme. Same app, different wallpaper:

| Fan control | Keyboard |
|---|---|
| ![Fan control, another theme](docs/screenshots/fan-theme2.png) | ![Keyboard, another theme](docs/screenshots/keyboard-theme2.png) |

## Features

### Fan control (through NBFC-Linux)
- Live temperatures, fan speeds and NBFC service status
- Three modes per fan: **Auto** (NBFC decides), **Fixed** (a constant %), **Curve** (temperature → speed)
- Curve editor: drag the points, fine-tune them, smooth or stepped curves
- A "Global" card to give every fan the same setting
- Profiles: NBFC Auto, Quiet, Balanced, Performance, plus your own
- Curve mode can follow the CPU, the GPU or whichever is hotter
- The fans don't rev up and down all the time: they speed up right away but slow down gently
- Shows your NBFC config's own curve, and you can copy it to edit it
- Max fan button (5 min, 15 min or until you turn it off)
- Graph of the last 10 minutes (temperatures and fan speed)
- Notifications through Caelestia: critical temperature, NBFC stopped, fan speed couldn't be set, max fan ended, profile changed from a keybind (you can turn them off)
- Tray icon in the Caelestia bar: click to show/hide, hover for profiles, max fan and quit
- Safety limit: above 90 °C, fans in Fixed/Curve mode go to 100%
- NBFC service controls (start, stop, restart, read-only mode, start on boot) and config picker
- Light on resources: around 0.5% CPU in the background while NBFC is in control

### Keyboard lighting
- **Color** (color wheel, presets or your Caelestia theme color) and **brightness**
- Finds your laptop and keyboard light step by step (maker → driver → light → permission) and only shows what your keyboard can actually do. `ccenter kbd info` prints the same thing
- **Breathing**: fades in and out with one color, up to 6 colors of your choice, or your Caelestia theme colors. Speed and lowest/highest level are adjustable. Keeps going when the window is closed
- **Color cycle**: flows smoothly between colors without dimming
- Caelestia colors: 3 to 6 colors picked from your current theme, and they update when the theme changes

#### Supported keyboards

Ccenter only uses the files a **kernel driver** offers for keyboard lights (in `/sys`). It never talks to the firmware or USB devices directly, so the driver checks everything it writes.

| Laptop | Kernel light | What works |
|---|---|---|
| HP Victus / OMEN, single-zone RGB | `hp::kbd_backlight` (needs an extra module, [see below](#hp-laptops)) | Color, brightness, effects (tested on my Victus 16-r0) |
| HP OMEN / Victus, four-zone RGB | `hp::kbd_zoned_backlight-*`, or `hp::kbd_backlight_zone0`–`3` in the module's 0.0.3 (extra module) | Color (same on all zones), brightness (not tested yet) |
| HP OMEN / Victus, four-zone RGB | `rgb_zones/zone00`–`zone03` (extra module, older copies) | Color only, same on all zones (not tested yet) |
| ASUS TUF with RGB | `asus::kbd_backlight` + `kbd_rgb_mode` | Static color and 4 brightness levels (not tested yet). Never saves anything to the BIOS |
| ASUS (other) | `asus::kbd_backlight` | Brightness levels |
| ASUS ROG (Aura N-KEY) | colors go through USB | Brightness only; for colors use [asusctl](https://gitlab.com/asus-linux/asusctl) |
| Lenovo IdeaPad / LOQ / ThinkPad | `platform::kbd_backlight` / `tpacpi::kbd_backlight` | Brightness levels |
| Lenovo Legion RGB | colors go through USB | Brightness if available; for colors use [L5P-Keyboard-RGB](https://github.com/4JX/L5P-Keyboard-RGB) or [OpenRGB](https://openrgb.org) |
| HP per-key RGB | not supported by the kernel or the module | — |
| HP with a white (single-color) backlight | not offered by the kernel or the module | — (the Fn key only) |
| HP with other OMEN keyboard modules (`omen-rgb-keyboard`, `hp-omen-wmi-dkms`, `hp-omen-gaming-wmi-dkms`) | their own files | Not supported (with `omen-rgb-keyboard` only its brightness shows, not tested) |
| Chromebooks with a backlit keyboard | `chromeos::kbd_backlight` | Brightness levels (not tested yet) |

The effects write to the light many times a second, so for now they're only turned on for the HP single-zone keyboard I could test them on.

#### HP laptops

Installing Ccenter alone isn't enough on HP:

- **Keyboard colors:** the official Linux kernel doesn't support HP keyboard lights yet (as of Linux 7.3). You need the extra kernel module [hp-wmi-fan-and-backlight-control](https://github.com/TUXOV/hp-wmi-fan-and-backlight-control). It covers single-zone and four-zone RGB keyboards and detects which one you have (its author still calls four-zone support experimental). Without it, Ccenter's keyboard tab tells you the light isn't available.
- **Not every HP:** the module only adds a light for RGB keyboards. HP laptops with a white (single-color) backlight and per-key RGB keyboards get nothing from it, so Ccenter can't control them; their light only changes with the Fn key. Don't install the module for those.
- **Fans:** NBFC with a config for your exact model (step 2 below). Configs that use BIOS (ACPI) methods also need the `acpi_call` module (`acpi_call-dkms` on Arch).

Installing the keyboard module (this is the module's own way, see its README for other distros and NixOS):

```sh
git clone https://github.com/TUXOV/hp-wmi-fan-and-backlight-control
cd hp-wmi-fan-and-backlight-control
make install-arch                  # Arch: builds and installs a DKMS package (asks for your password)
# other distros: make && sudo make install-dkms
```

Then reboot. Check that it worked: `ls /sys/class/leds | grep hp::` should show `hp::kbd_backlight` (single-zone) or four `hp::kbd_zoned_backlight-…` lights (four-zone; the module's 0.0.3 calls them `hp::kbd_backlight_zone0`–`3`).

- If an older copy of the module is already installed (for example through victus-control), remove it first so only one version is installed. Check with `dkms status`, then `sudo dkms remove hp-wmi-fan-and-backlight-control/<old version> --all`.
- To undo on Arch: `sudo pacman -R hp-wmi-fan-and-backlight-control-dkms` and reboot. Your laptop goes back to the kernel's own driver (no keyboard colors).
- Ccenter only uses the module's keyboard files, never its fan controls.

### Languages
- English and Turkish. Switch with the **TR / EN** button at the top (or `ccenter lang en`).
- It starts in your system language and remembers your choice.
- Terminal commands, notifications, the tray menu and the install scripts use the same language.
- Profile names work in either language from the terminal (`quiet` and `Sessiz` are the same profile).

### Caelestia integration
- Colors follow Caelestia's color scheme live.
- Rounding, spacing, fonts, animations and transparency come from your Caelestia settings, so it looks like the rest of your shell. With Hyprland blur on, the window is blurred like Caelestia's panels.
- Ccenter only **reads** Caelestia's files, it never changes them.

## Installation

### 1. Requirements

If you already use [Caelestia dots](https://github.com/caelestia-dots), you most likely have the first three.

| Needed | What for | Arch Linux package |
|---|---|---|
| [Quickshell](https://quickshell.org) (0.3.x) | the app runs on it | `quickshell` / `quickshell-git` (AUR) |
| [Caelestia shell](https://github.com/caelestia-dots/shell) | colors and styling; Ccenter **won't start without it** | `caelestia-shell` (AUR) |
| [Material Symbols](https://fonts.google.com/icons) font | icons | `ttf-material-symbols-variable` |
| [NBFC-Linux](https://github.com/nbfc-linux/nbfc-linux) with a config for your laptop | fan control (step 2) | `nbfc-linux` (AUR) |
| A polkit agent | the password dialog for admin actions (Caelestia/Hyprland setups usually have one) | e.g. `hyprpolkitagent` |
| Optional: `python-gobject` | tray icon | `python-gobject` |
| Optional: `nvidia-smi`, `lspci` | NVIDIA GPU temperature, GPU names | `nvidia-utils`, `pciutils` |

### 2. Set up NBFC (for fan control)

Ccenter doesn't control the fans on its own. It tells **NBFC-Linux** what to do, and NBFC talks to the laptop. So first NBFC needs a config that works for your laptop. The full guide is in the **[NBFC-Linux README](https://github.com/nbfc-linux/nbfc-linux#readme)**, but in short:

```sh
yay -S nbfc-linux                      # Arch (AUR). Other distros: see the NBFC-Linux README
sudo nbfc update                       # download the latest laptop configs
nbfc config -r                         # configs that may fit your laptop
sudo nbfc config -s "<config name>"    # pick one (or: sudo nbfc config -s auto)
sudo nbfc start --read-only            # try it safely first: it only reads, never sets fan speeds
nbfc status -a                         # do the fans and temperature look right?
sudo nbfc restart                      # then run it normally
sudo systemctl enable nbfc_service     # start NBFC on boot
```

If no config fits your laptop, **don't force one**, a wrong config can make the fans act weird. The keyboard part of Ccenter works without NBFC. You can also try configs later from Ccenter (Fan tab > NBFC config, with read-only mode).

### 3. Install Ccenter

```sh
git clone https://github.com/kerimozgurtukenmez/Ccenter-Caelestia.git
cd Ccenter-Caelestia
./install.sh
```

Then open **Ccenter** from your app launcher, or run `ccenter`.

**No `sudo` needed.** Everything goes into your home folder, and `install.sh` shows you the plan before it writes anything. Optional parts are asked one by one:

| What | Where |
|---|---|
| App, `ccenter` command and icon | `~/.local/share/ccenter/` |
| App launcher entry | `~/.local/share/applications/ccenter.desktop` |
| Background service (off until you turn it on) | `~/.local/share/systemd/user/ccenter.service` |
| Short `ccenter` command | `~/.local/bin/ccenter` (a link) |
| `ccenter` in your terminal's `PATH`, if it isn't already (asked) | a small marked block in `~/.bashrc` / `~/.zshrc`, or `~/.config/fish/conf.d/ccenter.fish` (terminals only, not keybinds or the app menu) |
| Keyboard light permission (asked, default no) | `/etc/udev/rules.d/90-ccenter.rules` (the only step that asks for your password) |

If something can't be written (for example a `~/.local/bin` owned by root), or a file with the same name belongs to another program, that part is skipped and the installer tells you why. It never overwrites your files.

To update: `git pull`, then `ccenter quit` and `./install.sh` again. Your settings stay.

Settings are saved in `~/.config/ccenter/settings.json`, only once you change something.

### Permissions: always asked, never taken silently

- **Keyboard light (optional).** To change the keyboard's color and brightness, your user needs write access to the light's own files. `install.sh` explains this and asks (default: no); you can also do it later with the **Grant permission** button in the Keyboard tab, and undo it there with **Remove permission**. Only your user gets access, nothing else on the system changes. (Versions before 0.3 gave every user access; the app offers to update that.)
- **NBFC service actions** (start/stop/restart, applying a config) ask for your password every time. Setting fan speeds doesn't.

### Uninstall

```sh
./uninstall.sh
```

It shows what it will delete, closes Ccenter (keyboard back to your static color, fans back to NBFC) and asks separately about the keyboard permission and your settings.

### System-wide install / packaging

The `Makefile` is for packagers (`make DESTDIR=… PREFIX=/usr install`) or a system-wide `sudo make install`. It doesn't add the keyboard permission; that's a separate, optional `sudo make udev`.

### Running in the background
- Closing the window doesn't quit Ccenter, it keeps controlling the fans. Open it again from the launcher, the tray icon or with `ccenter`.
- To quit for real: the "Quit" button, the tray menu or `ccenter quit`. The fans go back to NBFC first.
- "Start in the background on login" in the app turns on the systemd user service. If Ccenter crashes while running as a service, the fans are handed back to NBFC right away and the service starts Ccenter again a few seconds later.
- Started from a terminal (`ccenter` while it isn't running), it stays there like other programs and shows its log. Ctrl+C or closing that terminal quits it cleanly (the keyboard light and the fans go back to normal; Ctrl+C again forces it). If it crashes, the fans are handed back too. `ccenter -d` starts it without tying it to the terminal.
- If it's already running (for example the login service started it), `ccenter` just opens the window and returns. Its log: `ccenter log -f` (Ctrl+C there only stops following).
- Only one Ccenter runs at a time, two copies would fight over the fans.

### Terminal commands

```sh
ccenter                     # open the window; if it isn't running, start it here (Ctrl+C quits it)
ccenter -d                  # start it without tying it to this terminal
ccenter log -f              # follow the running app's log
ccenter status              # fans, temperatures, active profile
ccenter profiles            # list profiles (* = active)
ccenter profile quiet       # apply a profile (auto, quiet, balanced, performance or your own)
ccenter profile             # show the active profile
ccenter boost 15            # max fan for 15 minutes (also: on = until turned off, off; no argument: its state)
ccenter kbd color "#ff0000" # keyboard color
ccenter kbd brightness 50   # keyboard brightness in % (0 = off)
ccenter kbd effect cycle    # static | breathing | cycle
ccenter kbd source theme    # effect colors: single | multi | theme (Caelestia)
ccenter kbd speed 60        # effect speed 0-100
ccenter kbd range 20 80     # breathing: lowest and highest level in %
ccenter kbd themecount 5    # number of Caelestia colors (3-6)
ccenter kbd info            # what was detected: laptop, driver, light, permission
ccenter report              # make a report for bug reports (see below)
ccenter lang en             # language: en | tr (no argument: the current one)
ccenter hide                # also: open, toggle
ccenter quit                # quit; fans go back to NBFC
ccenter --help              # all commands; ccenter help kbd (or ccenter kbd --help) for one
```

Exit codes, for scripts: 0 done, 1 error, 2 wrong usage, 3 not started because Ccenter already runs, 4 the app isn't running. Errors go to stderr. The commands also find the app when the terminal has no Wayland display set.

Nice for Hyprland keybinds:

```lua
-- Hyprland with a Lua config (like mine: Hyprland 0.56 with Caelestia)
hl.bind("SUPER + F9", hl.dsp.exec_cmd("/home/you/.local/bin/ccenter profile performance"))
```

```ini
# hyprland.conf (older Hyprland)
bind = SUPER, F9, exec, /home/you/.local/bin/ccenter profile performance
```

Use the full path (with your user name): Hyprland doesn't always have `~/.local/bin` in its `PATH` (mine doesn't), even when your terminal does. The window's class (app ID) is `ccenter`, if you want a window rule for it.

## Reporting a problem

If something's wrong, please make a report and attach it to a [GitHub issue](https://github.com/kerimozgurtukenmez/Ccenter-Caelestia/issues):

```sh
ccenter report        # or ~/.local/share/ccenter/bin/ccenter report
```

or press **Create report** at the bottom of the Fan or Keyboard tab. It works even if Ccenter doesn't start.

You get one text file in your home folder (`~/ccenter-report-<date>.txt`) with the versions, your laptop model (no serial numbers), NBFC status, what Ccenter detected, your Ccenter settings and the logs of the last few runs, including crashed ones. Those logs are cleared when you reboot, so make the report before rebooting. Your home folder, user name and computer name are replaced with `~`, `<user>` and `<host>`. Nothing gets sent anywhere, just read it and share what you're okay with.

## Undoing things

- Give the fans back to NBFC: `nbfc set -a`
- Ccenter never edits NBFC config files, it only changes which config is selected. To go back: `sudo nbfc config --set "<config name>"` and `sudo nbfc restart`
- Remove the app: `./uninstall.sh`
- Remove just the keyboard permission: Keyboard tab > "Remove permission" (or `sudo rm /etc/udev/rules.d/90-ccenter.rules` and reboot)

## How it works

Ccenter is one Quickshell process that keeps running in the background. The window is just a view: it's created when you open it and thrown away when you close it to save memory, while the logic in `services/` keeps going.

```
            ┌──────────── Ccenter (one Quickshell process) ────────────┐
 you ──► window (only while open) ──────► services/ (always running)   │
 terminal ─ ccenter <cmd> ──────────────►  FanState  Nbfc  Sensors     │
 tray ─ scripts/tray.py ────────────────►  Keyboard  Alerts Settings   │
            └──────────────┬─────────────────────┬───────────────────────┘
                           │                     │
          nbfc status / nbfc set          /sys/class/hwmon (read)
          (NBFC service → fans)           /sys/class/leds (keyboard)
```

**Fans.** Ccenter reads `nbfc status -a` every 2 seconds (every 5 when the window is closed and NBFC is in charge). In *Auto* mode it just shows what NBFC does. In *Fixed* and *Curve* mode it works out the speed itself and sends it with `nbfc set`:

1. Take the temperature (CPU, GPU or the hotter one).
2. Find the speed on your curve.
3. Speed up right away, but only slow down once it's cooled a few degrees below the curve, and then at most 5% every 2 seconds.
4. Above the safety limit (90 °C by default, CPU or GPU) every fan it controls goes to 100%, no matter what.

**Temperatures** come from the kernel's sensors (`/sys/class/hwmon`). The NVIDIA driver doesn't share its temperature there, so `nvidia-smi` is used, but only when the GPU is already awake. A sleeping GPU is never woken up just to read it.

**Keyboard.** A small script reads which laptop, driver and keyboard light you have, and Ccenter picks what to do from that. Colors and effects are written to the light's files in `/sys/class/leds`.

**Settings** live in one file, `~/.config/ccenter/settings.json`. Fan settings are saved per NBFC config, so they never end up on the fans of a different config, and everything read from the file is checked first.

**Resource use:** about 0.5% CPU in the background with NBFC in charge, about 2% with a curve or with the window open; roughly 55–75 MB of memory with the window closed and 90 MB open (plus about 30 MB for the tray icon).

### Security

- **Day-to-day use needs no admin rights.** Fan speeds go through NBFC's own `nbfc set`, which doesn't need root.
- **Admin actions only happen when you click them**, through your system's own password dialog: NBFC service start/stop/restart, applying an NBFC config, granting or removing the keyboard permission. Ccenter never sees your password.
- **The keyboard is only touched through the kernel**, no direct firmware or USB access. On ASUS it never saves anything to the BIOS.
- **The keyboard permission is only for your user.** It just makes your user the owner of the light's brightness and color files; other users get nothing and no extra program runs. Before it's installed, the rule file is checked line by line and anything unexpected is refused.
- **Never changed:** NBFC config files and Caelestia's files (those are only read).
- Ccenter lives in your home folder and runs as your user, so anything that can already run as your user could also change it. Admin actions always still need your password.

## Testing

Everything was built and tested on my **HP Victus 16-r0xxx** (i7-13700H, RTX 3050 + Iris Xe), Arch Linux, kernel 7.2, Hyprland + Caelestia.

- **Fans:** every mode, profile and the max fan button, checked against what NBFC reports.
- **Interface:** both languages, every tab and card.
- **Resources:** CPU and memory with the window open and closed, plus a one-hour run to check for leaks (none).
- **Keyboard:** every color, brightness and effect on the real HP keyboard, read back from the hardware.
- **Other keyboards:** ASUS TUF, HP four-zone, Lenovo with a Legion USB controller and ASUS ROG were checked with simulated hardware only. **If you have one of these, a report would really help.**
- **Keyboard permission:** the rule and its checker were tested with good and tampered rules before installing it for real.
- **Installer:** install and uninstall were tested in a throwaway home folder, uninstall leaves nothing behind.
- **Not tested yet:** the critical temperature notification (I can't safely heat the laptop to 90 °C) and distros other than Arch.

## Development

Run it straight from the cloned folder:

```sh
./bin/ccenter
```

It uses the folder it's in, so all the commands work the same (`./bin/ccenter status`, …). Quit the installed copy first, only one can run at a time. Quickshell reloads the UI live when you save a file.

### Notes for contributors and forks

- **Structure:** the logic is in `services/` (keeps running when the window is closed), `modules/` is only the UI. `services/FanState.qml` is the one place fan settings live; the fan cards just show it and send changes back.
- **Style:** sizes, spacing, fonts and animations come from Caelestia (`Tokens.*`, `StyledText`, `Anim`/`CAnim`), colors from `services/Colours.qml`. Avoid hard-coded values so the app keeps matching the user's shell.
- **Text and translations:** write UI text in English and wrap it like `I18n.t("Fan %1").arg(n)`, then add the Turkish line to `services/translations.js` (the key is the exact English text). Missing translations just show English. To add a language, add a block to `translations.js` and an entry to `I18n.languages` in `services/I18n.qml`.
- **Performance:** the app is always running, so keep `scripts/sensors.sh` to shell built-ins and skip heavy work while the window is hidden (`Nbfc.uiVisible`).
- **Safety:** never write NBFC config files or Caelestia's files. Anything that changes the system should be opt-in and easy to undo.
- The terminal commands are handy for testing without clicking around (`./bin/ccenter status`, `profile …`, `kbd …`, `kbd info`).
- **Testing keyboards you don't have:** make a fake `/sys` folder (`sys/class/dmi/id/*`, `sys/class/leds/<light>/…`, `sys/class/leds/<light>/device/driver` → a folder named after the driver) and run `CCENTER_SYS=<that folder> XDG_CONFIG_HOME=<a test folder> ./bin/ccenter`. Ccenter then only reads and writes that fake folder, and your real settings stay untouched.

## Project layout

```
shell.qml           window, tabs, terminal commands, tray wiring
services/           always-running parts: NBFC, fan state, keyboard, settings, sensors, colors, notifications, tray
services/I18n.qml   language; translations in services/translations.js
modules/fan/        fan tab
modules/keyboard/   keyboard tab
components/         shared UI parts
utils/              helpers
scripts/            sensors.sh, hwinfo.sh, kbdinfo.sh (keyboard detection), kbd-rule-check.sh,
                    report.sh (bug report), tray.py (tray icon)
assets/             icons and images
docs/screenshots/   README images
bin/ccenter         launcher and terminal commands
dist/               app launcher entry, background service, keyboard permission rule
install.sh          install into your home folder (no sudo)
uninstall.sh        remove it again
Makefile            system-wide install / packaging
LICENSE             GPL-3.0
```

## Roadmap

- [x] Fan control through NBFC
- [x] Settings that survive restarts
- [x] Profiles, background mode, tray icon, notifications
- [x] Install without sudo, permissions only when you agree
- [x] Keyboard color, brightness, breathing and color cycle
- [x] Keyboard detection for HP, ASUS and Lenovo; keyboard permission for your user only
- [x] English and Turkish
- [ ] Testing on real ASUS / Lenovo / HP four-zone laptops (help wanted!)
- [ ] Separate colors per zone on HP four-zone keyboards, ASUS TUF hardware effects
- [ ] More keyboard effects (rainbow, …)

Issues and feedback are very welcome, just expect some rough edges.

## Thanks

Ccenter stands on other people's open-source work. Big thanks to the people behind:

- **[Caelestia](https://github.com/caelestia-dots)**, the shell this app is made for. Its colors and overall look come from there.
- **[Quickshell](https://quickshell.org)**, which the whole app is built with.
- **[NBFC-Linux](https://github.com/nbfc-linux/nbfc-linux)**, which does the actual fan control, and everyone who made laptop configs for it.
- **[Material Symbols](https://fonts.google.com/icons)** for the icons.

## License

Copyright (C) 2026 Kerim Özgür Tükenmez

Ccenter is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License version 3** as published by the Free Software Foundation. It comes **without any warranty**; see [`LICENSE`](LICENSE) for the full text.

Third-party parts:

- **[Caelestia shell](https://github.com/caelestia-dots/shell)** (GPL-3.0): Ccenter uses its QML plugin at runtime, and the transparency math in `services/Colours.qml` is adapted from Caelestia's `Colours.qml`. Caelestia itself is not included.
- **[Quickshell](https://quickshell.org)** (LGPL-3.0), **[NBFC-Linux](https://github.com/nbfc-linux/nbfc-linux)** (GPL-3.0) and the **[Material Symbols](https://fonts.google.com/icons)** font (Apache-2.0) are used as separate programs or fonts and are not included here.

Ccenter is made for Caelestia users but it's an independent project, not made or supported by the Caelestia, Quickshell or NBFC developers or by any laptop maker. Please report Ccenter problems here, not to them.
