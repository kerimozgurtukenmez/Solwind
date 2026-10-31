# solwindd's D-Bus interface

solwindd (Solwind's background service) is on the **session bus** as `solwind.Daemon`. Everything Solwind's window and the `solwind` command do goes through this interface, so your own tools can do the same: a bar module, a widget, a script.

- **State** is in properties. Every change is sent as `org.freedesktop.DBus.Properties.PropertiesChanged`, so you don't need to poll.
- **Changes** are methods. solwindd checks every argument. The safety limit, handing the fans back and the rest stay its own, whatever a client sends.
- **Texts** (status texts, messages, profile labels) are in the user's Solwind language (`Language`).
- Numbers that aren't known are `NaN` (a temperature while a sensor sleeps, a fan speed the driver doesn't report).
- solwindd isn't D-Bus activated. If nobody runs it, the name has no owner; `solwind` starts it.

Signatures below are D-Bus types: `s` string, `b` boolean, `i` int32, `x` int64, `d` double, `as` string array, `a{sv}` a map, `aa{sv}` a list of maps.

## Errors

| Name | Meaning | `solwind` exit code |
|---|---|---|
| `solwind.Error.Usage` | A wrong argument (the message says which) | 2 |
| `solwind.Error.Failed` | Refused now (no fan plugin, NBFC not running or read-only, no keyboard light, no permission, quitting, ...) | 1 |
| `solwind.Error.Observe` | solwindd was started with `--observe` and only reads | 1 |
| `org.freedesktop.DBus.Error.InvalidArgs` | `Watch` / `Unwatch` with an unknown topic | 2 |

## `/solwind` — `solwind.Daemon`

| Property | Type | |
|---|---|---|
| `Version` | s | solwindd's version |
| `Mode` | s | `control`, or `observe` (only reads, refuses every change) |
| `Language` | s | The language texts are in: `tr` or `en` |
| `SavedLanguage` | s | The saved choice: `tr`, `en`, or `""` (the system's) |
| `NotifyEnabled`, `NotifyProfile` | b | Notifications on; notify when a profile is changed from a terminal or keybind |
| `TrayEnabled` | b | The window app shows a tray icon |
| `Plugins` | aa{sv} | The plugins solwindd loaded at start: `id` (`HP/hp-wmi-keyboard` ...), `brand`, `module`, `kind` (`fan` or `keyboard`), `version`, `description`, `file`, `used` (it drives the fans - only one fan plugin does - or it has keyboard lights to offer) |
| `PluginFailures` | aa{sv} | Plugin files it didn't load: `file`, `why` (built for another Solwind, others could change it, a link, ...) |

| Method | Arguments → answer | |
|---|---|---|
| `Status` | → a{sv} | `version`, `pid`, `mode`, `settingsPath`, `settingsLoaded`, `sysRoot`, `nbfcPollMs`, `sensorsActive`, `sensorsFast` |
| `Watch` / `Unwatch` | s | What your client shows: `window` (solwindd reads faster) or `fans` (it reads the sensors). Forgotten by itself when your connection closes. |
| `SetNotify` | s key, b on | key: `enabled` or `profile` |
| `SetLanguage` | s | `tr`, `en`, or `""` |
| `SetUi` | s key, b on | key: `tray` |
| `Quit` | | Quits cleanly: fans back to NBFC's automatic control, the keyboard light on its static color. The answer comes first. |

## `/solwind/Fans` — `solwind.Fans`

The fans of the fan plugin solwindd loaded (one at a time; with none, there are no fans and every change is refused with `No fan plugin is installed`). NBFC's service and configs (the properties and methods marked *service*) are there only when `HasService` is true; without it those methods are refused and those properties stay empty.

| Property | Type | |
|---|---|---|
| `Plugin` | s | The fan plugin's id (`NBFC/nbfc`); `""` = none is installed |
| `HasService` | b | It has a service and configs (NBFC) |
| `Running`, `ReadOnly` | b | NBFC's service runs; it's in read-only mode |
| `Temp` | d | NBFC's temperature (°C) |
| `Fans` | aa{sv} | One per fan: `name`, `temp`, `auto`, `critical`, `current`, `target` (%), `rpm` |
| `ConfigId`, `FileConfigId` | s | The config the fans belong to; *service:* the one selected in `/etc/nbfc/nbfc.json` |
| `NoConfig`, `ConfigPending` | b | *service:* none selected yet; selected, but used only after NBFC restarts |
| `Configs`, `Recommended` | as | *service:* known configs; the ones `Recommend` found |
| `ConfigList` | a{sv} | *service:* where the list came from: `dirs`, `fromDirs`, `fromCli`, `cliCode`, `cliLine` |
| `ConfigCurves` | aa{sv} | *service:* the selected config's threshold table per fan: `points` [{t, s}], `hyst`; `{}` if it has none |
| `BootEnabled` | b | *service:* NBFC's service starts at boot |
| `FanSettings` | aa{sv} | Solwind's setting per fan (see *A fan setting* below) |
| `Global`, `GlobalOn` | a{sv}, b | The setting written to every fan while `GlobalOn` |
| `Restored` | b | The saved settings of this config are applied |
| `Profiles` | aa{sv} | `name` (the id for built-ins), `label`, `builtin`, `icon` |
| `ActiveProfile` | s | Its name, `""` for own settings |
| `ReservedNames` | as | Names a user profile can't take (lower case) |
| `BoostUntil` | x | Max fan: 0 off, -1 until turned off, else its end (ms since the epoch) |
| `BoostBlocked` | s | Why max fan can't be turned on now (`""` = it can) |
| `Safety` | d | Fans in Fixed/Curve mode go to 100% at this temperature |
| `SafetyHeld` | ai | Fans held at 100% by it right now |
| `Controlling`, `Holding`, `AdminBusy` | b | Solwind drives fans; an NBFC action that handed them back runs; *service:* an action waits for the password dialog |
| `Message`, `MessageError` | s, b | *service:* what that action waits for, or why it failed |

| Method | Arguments → answer | |
|---|---|---|
| `SetFan` | i fan, a{sv} setting | |
| `SetGlobal` | a{sv} setting | |
| `SetGlobalOn` | b | |
| `ApplyProfile` | s name, b notify → s | Any name a profile answers to, in any case or language; notify: say so in a notification (if that's on). Answers `Applied: …` |
| `SaveProfile` / `RemoveProfile` | s | Save the current settings under a name (1–32 characters) / remove a user profile |
| `SetBoost` | x minutes | > 0 for that long, < 0 until turned off, 0 off |
| `SetSafety` | i | 70–100 °C |
| `StartService`, `StopService`, `RestartService` | | *service:* NBFC's service; asks for the admin password (pkexec) |
| `SetReadOnly`, `SetBoot` | b | *service:* same |
| `ApplyConfig` | s | *service:* selects a listed config and restarts NBFC (password) |
| `Recommend` | → b ok, as | *service:* `nbfc config -r` (can take a while: allow 30 s); without a service `false, []` |
| `History` | → aa{sv} | The last 10 minutes: `t` (ms), `cpu`, `gpu`, `fan` (average %) |
| `StatusText`, `ProfileList`, `CurrentProfile`, `BoostStatus` | → s | The texts of `solwind status`, `profiles`, `profile`, `boost` |

| Signal | | |
|---|---|---|
| `Sampled` | a{sv} | A new graph sample (`t`, `cpu`, `gpu`, `fan`) after every NBFC read |

**A fan setting** (a{sv}): `mode` 0 auto / 1 fixed / 2 curve, `fixed` 0–1, `curve` 2–12 points `{t, s}` with whole `t` 30–100 °C rising by at least 1 and whole `s` 0–100 %, `smooth` (b), `src` `cpu` / `gpu` / `max`, `hyst` 0–10 °C (how far it must cool down before slowing). Missing fields get defaults; unknown ones are refused.

## `/solwind/Sensors` — `solwind.Sensors`

Read while a client watches `fans`, while Solwind drives fans, or while notifications are on.

| Property | Type | |
|---|---|---|
| `Readings` | aa{sv} | One per tile: `label` (CPU, GPU, iGPU), `model`, `threads`, `value` (°C), `note` (`asleep`, `error`, `nosensor` or `""`), `usage` (%, NaN if not known) |
| `Cpu`, `Gpu`, `HasGpu` | d, d, b | |
| `Hardware` | a{sv} | `cpu`, `cores`, `gpu`, `igpu` (names) |
| `Rpms`, `RpmWorks` | ad, b | The first fan driver's fan speeds; it has reported one above 0 |

## `/solwind/Keyboard` — `solwind.Keyboard`

| Property | Type | |
|---|---|---|
| `Available`, `Ready` | b | A light was found; the first look is done |
| `Backend` | s | The kind of light (the keyboard plugin's): `led-rgb`, `hp-zoned`, `hp-rgbzones`, or `""` (none, or no plugin takes it) |
| `Name`, `Driver`, `Kernel`, `Zones` | s, s, s, i | |
| `Dmi` | a{sv} | `vendor`, `product`, `family`, `board` |
| `Notes` | as | `legion-usb`, `rog-usb`, `hp-none`, `no-plugin` (a light was found, but no installed plugin takes it; `NoteTexts` names it) |
| `Rgb`, `HasBrightness`, `EffectsAllowed`, `Tested` | b | What this light can do; effects only where tested |
| `Writable`, `RuleAt`, `RuleLegacy`, `RuleOld`, `PermBusy` | b, s, b, b, b | The write permission and its udev rule |
| `Error` | s | The last write error |
| `MaxBrightness`, `Brightness` | i | In the light's own steps |
| `Color`, `ColorF` | s, ad | The chosen color: `#RRGGBB`, and unrounded r, g, b (0–1) |
| `Effect`, `Source` | s | `static` / `breathing` / `cycle`; `single` / `multi` / `theme` |
| `Colors`, `ThemeColors`, `ThemeCount` | as, as, i | The color list; the Caelestia colors the effect uses; their number (3–6) |
| `Speed`, `Period`, `MinLevel`, `MaxLevel` | d | 0–1; seconds per breath or color; breathing levels 0–1 |
| `EffectActive`, `Paused`, `CycleIndex` | b, b, i | |
| `KindText`, `SupportText`, `NoPermissionText`, `NoteTexts` | s, s, s, as | Texts for a UI |

| Method | Arguments → answer | |
|---|---|---|
| `SetColor` | s → s | `#rrggbb` |
| `SetBrightness`, `SetSpeed` | i → s | Percent |
| `SetRange` | i low, i high → s | Percent |
| `SetEffect`, `SetSource` | s → s | |
| `SetThemeCount` | i → s | 3–6 |
| `SetColors` | as | 1–6 colors `#rrggbb` |
| `SetColorF`, `SetBrightnessRaw`, `SetSpeedF`, `SetRangeF` | ddd, d, d, dd | A UI's controls, unrounded (no answer text) |
| `GrantPermission`, `RevokePermission` | | The udev rule for the user; asks for the admin password |
| `Refresh` | | Look for the light again |
| `StatusText`, `InfoText`, `DetectionText` | → s | `solwind kbd`, `solwind kbd info`, the detection's raw lines |

## Examples

```sh
# Follow the fans
busctl --user monitor --match "type=signal,sender=solwind.Daemon,path=/solwind/Fans,member=PropertiesChanged"
# Read one value
busctl --user get-property solwind.Daemon /solwind/Fans solwind.Fans Temp
# Change something
busctl --user call solwind.Daemon /solwind/Fans solwind.Fans SetBoost x 15
busctl --user call solwind.Daemon /solwind/Keyboard solwind.Keyboard SetColor s "#ff0000"
busctl --user call solwind.Daemon /solwind/Fans solwind.Fans SetFan 'ia{sv}' 0 2 mode i 1 fixed d 0.4
```

The interface can grow; I try not to change what's here in a way that breaks a client. `Version` says which solwindd answers.
