# Solwind

**Fan control and keyboard lighting for Linux laptops**, in a window made with [Quickshell](https://quickshell.org) that fits right into [Caelestia dots](https://github.com/caelestia-dots).

A small background service, **solwindd**, drives the fans and the keyboard light. The window only shows and changes what it does, so the fans don't need the window open.

| Fan control | Keyboard | Plugins |
|---|---|---|
| ![Fan control](docs/screenshots/fan.png) | ![Keyboard lighting](docs/screenshots/keyboard.png) | ![Plugins](docs/screenshots/plugins.png) |

> [!WARNING]
> **This is an early project, and by using it you accept the risks below.**
>
> I did everything I could to keep it from causing harm: every fan Solwind drives goes to 100% above a safety limit, fans go back to automatic when it quits or crashes, speeds have a lower limit, and a plugin that can't be trusted isn't loaded. That still isn't a guarantee. I only tested it on my own HP Victus 16-r0xxx (Arch Linux, Hyprland), and the MSI and OpenRGB plugins have never run on real hardware.
>
> What could go wrong:
>
> - A bad fan setting, or a driver or firmware that behaves differently from mine, can make your laptop run hot or slow down.
> - Fans can stay at a fixed speed, or not spin when they should, until you quit Solwind or restart.
> - Your laptop can freeze or restart. One person reported this on an MSI with custom fan curves in another tool.
> - Heat over time can shorten the life of your hardware, and in the worst case damage it.
>
> Keep an eye on your temperatures at first. Solwind comes without any warranty (sections 15 and 16 of the [license](LICENSE)): if something breaks, I can't take responsibility for it. If that's not okay with you, don't use it.

> [!NOTE]
> I used AI assistance while building this project. I reviewed parts of the code myself and tested the features my own laptop has before publishing it.

## What it does

- **Fans:** Auto, Fixed or Curve per fan, following the CPU, the GPU or the hotter one. Curve editor, profiles, max fan and a 10 minute graph.
- **Safety:** every fan Solwind drives goes to 100% above the limit you set (90 °C by default). If Solwind quits or crashes, the fans go back to automatic.
- **Keyboard:** color, brightness and effects, also with your Caelestia theme colors. It only shows what your keyboard can do.
- **The rest:** temperatures and usage, notifications, a tray icon, terminal commands, English and Turkish. Colors, rounding and fonts come from your Caelestia setup.

## Supported laptops

What Solwind drives comes from plugins, one for each kind of hardware. Pick yours here.

| Laptop | Fans | Keyboard | Tested |
|---|---|---|---|
| Any laptop with an [NBFC](https://github.com/nbfc-linux/nbfc-linux) config | `NBFC/nbfc` | | Yes, on my HP |
| HP Victus / OMEN, single-zone RGB | `NBFC/nbfc` or `HP/hp-wmi-fan` | `HP/hp-wmi-keyboard`: color, brightness, effects | Yes, on my HP |
| HP Victus / OMEN, four-zone RGB | `NBFC/nbfc` or `HP/hp-wmi-fan` | `HP/hp-wmi-keyboard-zones` or `HP/hp-wmi-rgb-zones` (older module): one color on all zones | Keyboard no |
| MSI, single color | `MSI/msi-fan` | `MSI/msi-ec-keyboard`: brightness | No |
| MSI RGB (SteelSeries) | `MSI/msi-fan` | `OpenRGB/openrgb`: one color, brightness | No |
| Other RGB keyboards [OpenRGB](https://openrgb.org) supports | | `OpenRGB/openrgb` | No |

ASUS and Lenovo have no plugins yet. For their USB keyboards use [asusctl](https://gitlab.com/asus-linux/asusctl) or [L5P-Keyboard-RGB](https://github.com/4JX/L5P-Keyboard-RGB). Effects are only on for the HP single-zone keyboard, the one I could test them on.

## Install

### 1. Requirements

| Needed | Arch package |
|---|---|
| [Quickshell](https://quickshell.org) 0.3.x | `quickshell` or `quickshell-git` (AUR) |
| [Caelestia shell](https://github.com/caelestia-dots/shell) | `caelestia-shell` (AUR) |
| Material Symbols font | `ttf-material-symbols-variable` |
| A polkit agent, for password dialogs | e.g. `hyprpolkitagent` |
| CMake, Qt 6.5 or newer, a C++20 compiler, git | `cmake`, `qt6-base`, `qt6-declarative`, `base-devel`, `git` |
| systemd | `systemd` |
| Optional: tray icon, NVIDIA readings, GPU names | `python-gobject`, `nvidia-utils`, `pciutils` |

### 2. Solwind

```sh
git clone https://github.com/kerimozgurtukenmez/Solwind.git
cd Solwind
make
sudo make install
```

It goes under `/usr/local`. No plugin comes with it: without one Solwind only shows the temperatures.

### 3. The plugins for your laptop

```sh
solwind plugins                   # lists them, and which fit this computer
solwind plugin install NBFC/nbfc  # the names are in the table above
```

Then follow the part for your laptop below.

### 4. Start it

```sh
solwind quit; solwind
```

Solwind loads plugins when it starts. You can also open **Solwind** from the app launcher; on a first install it may only appear after you log in again. **Start in the background on login** is a switch in Settings.

## Setup for your laptop

### Fans through NBFC

Set NBFC up first ([full guide](https://github.com/nbfc-linux/nbfc-linux#readme)). On Arch it's `nbfc-linux` from the AUR.

```sh
sudo nbfc update                       # download the laptop configs
nbfc config -r                         # configs that may fit your laptop
sudo nbfc config -s "<config name>"
sudo nbfc start --read-only            # try it safely: it only reads
nbfc status -a                         # do the fans and temperature look right?
sudo nbfc restart && sudo systemctl enable nbfc_service
```

If no config fits, don't force one. Configs using ACPI methods also need `acpi_call` (`acpi_call-dkms` on Arch).

### HP keyboard lights

These need the extra module [hp-wmi-fan-and-backlight-control](https://github.com/TUXOV/hp-wmi-fan-and-backlight-control), the official kernel doesn't support them yet. Remove older copies of it first (`dkms status`).

```sh
git clone https://github.com/TUXOV/hp-wmi-fan-and-backlight-control
cd hp-wmi-fan-and-backlight-control
make install-arch                      # other distros: make && sudo make install-dkms
```

Reboot, then `ls /sys/class/leds | grep hp::` should list the light. Then use **Grant permission** on the Keyboard page.

### HP fans without NBFC

`HP/hp-wmi-fan` uses `pwm1` and `pwm2` of the same module, on the boards the module has them. I tested one board (8BBD, with the module patched for it, as it isn't on the module's list). If your firmware ignores the command, the fans just stay on automatic.

```sh
solwind plugin disable NBFC/nbfc       # if you had it: it comes first
solwind quit
sudo nbfc stop                         # both can't drive the same fans
sudo systemctl disable nbfc_service
solwind plugin install HP/hp-wmi-fan
solwind fan-permission                 # lets your user write the fan speed
solwind
```

- The driver has one mode for both fans: a fan on Auto next to a Fixed or Curve one runs at that fan's speed.
- The lowest speed is about 2000 RPM. Only automatic control stops the fans.
- Fan settings are kept apart from NBFC's.
- Back to NBFC: `solwind quit`, `solwind fan-permission off`, `solwind plugin remove HP/hp-wmi-fan`, `solwind plugin enable NBFC/nbfc`, `sudo systemctl enable --now nbfc_service`, `solwind`.

### MSI

Not tested on hardware. Solwind never writes the embedded controller itself, so it depends on what your kernel offers:

| You have | You get |
|---|---|
| The out-of-tree [msi-ec](https://github.com/BeardOverflow/msi-ec) module, on a laptop on its firmware list | Fan readings, Auto, max fan, single-color keyboard brightness. `msi-ec` can't set a speed. The one in the official kernel only has battery limits |
| A kernel with the `msi-wmi-platform` fan curve patches | Fixed, Curve and profiles too, never below 30%. These aren't in the official kernel, and the plugin was written from their 2025 version |
| Neither | The plugins do nothing |

```sh
solwind plugin install MSI/msi-fan
solwind plugin install MSI/msi-ec-keyboard
solwind fan-permission
solwind quit; solwind
```

Stop NBFC and turn its plugin off first if you had it, as for HP. One person reported hard resets on one MSI with custom curves outside the high performance profile. The cause isn't confirmed, so go carefully.

### RGB keyboards through OpenRGB

Install [OpenRGB](https://openrgb.org) and make sure it sees the keyboard itself: `openrgb --list-devices`. Its udev rules give your user wide hardware access.

```sh
solwind plugin install OpenRGB/openrgb
solwind quit; solwind
```

It's only used when no plugin takes a keyboard light of the kernel. If nothing answers, Solwind starts OpenRGB's server on 127.0.0.1, port 6742. That server has no password, so any program on this computer can use it. Solwind sets one color on the whole keyboard. Brightness dims that color and isn't kept when Solwind restarts.

## Usage

Closing the window doesn't stop solwindd, it keeps controlling the fans. Quit with **Quit** in Settings, the tray menu or `solwind quit`: the fans go back to automatic and the keyboard stays on its color.

```sh
solwind status              # fans, temperatures, active profile
solwind profile quiet       # apply a profile (no name: show the active one)
solwind boost 15            # max fan for 15 minutes (on / off)
solwind kbd color "#ff0000" # also: brightness, effect, source, speed, range, info
solwind log -f              # follow solwindd's log
solwind --help              # everything; solwind help kbd for one command
```

Exit codes: 0 done, 1 error, 2 wrong usage, 3 not started, 4 not running. For a Hyprland keybind: `hl.bind("SUPER + F9", hl.dsp.exec_cmd("solwind profile performance"))`. The window class is `solwind`.

Fans back to automatic by hand: `nbfc set -a` with NBFC; with the HP or MSI plugin quit Solwind.

## Update and uninstall

**Update:**

```sh
git pull
make
solwind quit
sudo make install
solwind plugin update
solwind
```

A plugin installed from a local folder isn't updated by `solwind plugin update`: install it from its folder again.

**Coming from Solwind 0.5 or older:** nothing is built in any more, so `solwind plugin update` has nothing to do the first time: install the [plugins for your laptop](#supported-laptops) instead (`solwind plugin install NBFC/nbfc` for fans through NBFC). Your settings stay. If you had **Start in the background on login** on, run `systemctl --user daemon-reload` once; `make install` reminds you.

**Uninstall:** turn **Start in the background on login** off, then:

```sh
solwind quit
sudo make uninstall
```

It removes the permissions too, then asks whether to delete your settings, plugins and build cache. The answer is no unless you say yes.

**Packaging:** `make DESTDIR=/package/root PREFIX=/usr install`.

**Coming from Ccenter (Solwind's old name) or an `install.sh` install:** your settings are copied over on the first start. Keybinds and scripts that use `ccenter` need `solwind` now.

```sh
ccenter quit        # or: solwind quit
make remove-old     # shows what it removes and asks
sudo make install
```

## Plugins and permissions

- **Plugins** live in their own repository, [Solwind-plugins](https://github.com/kerimozgurtukenmez/Solwind-plugins). Installing one downloads only its folder at the exact commit in `plugin-catalog.json`, builds it on your computer and puts it in `~/.local/share/solwind/plugins`, without sudo. Its build steps run as you.
- **Commands:** `solwind plugin update`, `solwind plugin remove <name>`, `solwind plugin disable <name>`, `enable <name>`. Or use the Plugins page. Changes take effect when Solwind starts again.
- **What's loaded:** solwindd only loads a plugin built for this Solwind, and only if no one but you or root can change it. It runs inside solwindd, as your user. A catalog is only as trustworthy as my GitHub account, there's no signature.
- **Keyboard permission:** **Grant permission** on the Keyboard page or `sudo make udev`. Only your user gets it. **Remove permission** undoes it.
- **Fan permission (HP, MSI):** `solwind fan-permission` adds `/etc/udev/rules.d/91-solwind-fans.rules`, so any program of yours can set the fan speed. To remove it: `solwind quit`, then `solwind fan-permission off`.
- **NBFC actions** (start, stop, config) ask for your password every time. Setting fan speeds doesn't. Solwind never sees your password.
- Solwind never edits NBFC config files or Caelestia's files. To change the NBFC config back: `sudo nbfc config --set "<config name>"` and `sudo nbfc restart`.

## Reporting a problem

Run `solwind report` or **Create report** in Settings, and attach the file to a [GitHub issue](https://github.com/kerimozgurtukenmez/Solwind/issues). It has versions, your laptop model (no serial numbers), what solwindd detected and recent logs. Your user and computer names are replaced, and nothing is sent anywhere. Logs are cleared on reboot, so make it before rebooting.

## Your own tools

Everything the window does goes through D-Bus (session bus, `solwind.Daemon`), so a widget or a script can do the same. solwindd checks everything it's sent, and the safety limit stays its own. See [docs/DBUS.md](docs/DBUS.md).

```sh
busctl --user get-property solwind.Daemon /solwind/Fans solwind.Fans Temp
busctl --user call solwind.Daemon /solwind/Fans solwind.Fans ApplyProfile sb quiet false
```

## Development

`backend/` is solwindd and `solwind-plugin` (C++/Qt, CMake), and `backend/src/plugins/` has the interfaces plugins implement. `services/` passes solwindd's state to the pages in `modules/`. UI text is English in the code, with the Turkish in `services/translations.js`.

For the tests, clone Solwind-plugins next to this repository:

```sh
cmake -S backend -B <dir> -DSOLWIND_PLUGINS_DIR=../Solwind-plugins
cmake --build <dir> && ctest --test-dir <dir>
```

To run from the cloned folder: `make`, then `./bin/solwind` (quit the installed copy first).

## Thanks

Solwind only asks; these projects do the hard part of talking to the hardware:

- [NBFC-Linux](https://github.com/nbfc-linux/nbfc-linux) and everyone who made laptop configs for it
- [hp-wmi-fan-and-backlight-control](https://github.com/TUXOV/hp-wmi-fan-and-backlight-control) by TUXOV, and the developers of the kernel's `hp-wmi` driver it's based on, for HP fans and keyboard lights
- [msi-ec](https://github.com/BeardOverflow/msi-ec) by BeardOverflow and its contributors, who map MSI's firmwares one by one
- Armin Wolf and Antheas Kapenekakis, for the `msi-wmi-platform` fan curve patches
- [OpenRGB](https://openrgb.org), for RGB keyboards the kernel doesn't reach

And [Caelestia](https://github.com/caelestia-dots), [Quickshell](https://quickshell.org), [Qt](https://www.qt.io) and [Material Symbols](https://fonts.google.com/icons), for the window.

## License

Copyright (C) 2026 Kerim Özgür Tükenmez

Solwind is free software under the **GNU General Public License version 3**, without any warranty; see [`LICENSE`](LICENSE).

The transparency math in `services/Colours.qml` is adapted from [Caelestia shell](https://github.com/caelestia-dots/shell) (GPL-3.0), whose QML plugin Solwind uses at runtime (not included). Quickshell (LGPL-3.0), Qt (LGPL-3.0), NBFC-Linux (GPL-3.0) and Material Symbols (Apache-2.0) are separate and not included, and so are OpenRGB, msi-ec and the HP module.

Solwind is an independent project, not made or supported by the developers of any project named here, or by any laptop maker. Please report Solwind problems here.
