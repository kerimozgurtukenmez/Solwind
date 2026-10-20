# For system-wide installs and packaging. For a normal user the recommended way is ./install.sh
# (installs into the home folder, needs no admin password, asks about the keyboard permission).
#   sudo make install      installs under /usr (does NOT add the keyboard permission)
#   sudo make uninstall    removes it (does not touch user settings in ~/.config/ccenter)
#   sudo make udev         keyboard light permission (udev rule) for the user running sudo, optional and separate
# Packaging: make DESTDIR=/package/root PREFIX=/usr install

PREFIX  ?= /usr
DESTDIR ?=

APPDIR  = $(DESTDIR)$(PREFIX)/share/ccenter
BINDIR  = $(DESTDIR)$(PREFIX)/bin
APPSDIR = $(DESTDIR)$(PREFIX)/share/applications
ICONDIR = $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps
UNITDIR = $(DESTDIR)$(PREFIX)/lib/systemd/user
UDEVDIR = $(DESTDIR)$(PREFIX)/lib/udev/rules.d
LICDIR  = $(DESTDIR)$(PREFIX)/share/licenses/ccenter
# Keyboard lights (their files go back to root on uninstall)
KBD_LEDS = /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*
# HP four-zone colour files (older copies of the hp-wmi-fan-and-backlight-control module)
KBD_ZONES = /sys/devices/platform/hp-wmi/rgb_zones/zone*
# The rule gives access to one user: the one running "sudo make udev" (SUDO_UID), or UDEV_UID=<id>
UDEV_UID ?= $(SUDO_UID)

# The menu entry runs the short command where it is on every PATH (/usr/bin, /usr/local/bin), else the full path
DESKTOP_EXEC = $(if $(filter /usr /usr/local,$(PREFIX)),ccenter,$(PREFIX)/bin/ccenter)

# Files the app needs at runtime (README, dist etc. are not installed; the license goes to share/licenses)
APP_FILES = shell.qml VERSION
APP_DIRS  = components modules services utils scripts assets

.PHONY: install uninstall udev uninstall-udev

install:
	install -d $(APPDIR)
	install -m 644 $(APP_FILES) $(APPDIR)/
	cp -r --no-preserve=ownership $(APP_DIRS) $(APPDIR)/
	install -Dm 644 dist/90-ccenter.rules $(APPDIR)/dist/90-ccenter.rules
	find $(APPDIR) -type d -exec chmod 755 {} +
	find $(APPDIR) -type f -exec chmod 644 {} +
	chmod 755 $(APPDIR)/scripts/*.sh $(APPDIR)/scripts/*.py
	install -Dm 755 bin/ccenter $(BINDIR)/ccenter
	install -d $(APPSDIR) $(UNITDIR)
	sed 's|@BINDIR@/ccenter|$(DESKTOP_EXEC)|g' dist/ccenter.desktop > $(APPSDIR)/ccenter.desktop
	chmod 644 $(APPSDIR)/ccenter.desktop
	install -Dm 644 assets/ccenter.svg $(ICONDIR)/ccenter.svg
	sed 's|@BINDIR@|$(PREFIX)/bin|g' dist/ccenter.service > $(UNITDIR)/ccenter.service
	chmod 644 $(UNITDIR)/ccenter.service
	install -Dm 644 LICENSE $(LICDIR)/LICENSE
	@echo
	@echo "Ccenter installed. To start it: ccenter   (or from the app menu)"
	@echo "The keyboard light permission was not added. If you want it: sudo make udev   (or 'Grant permission' in the app)"

uninstall: uninstall-udev
	rm -rf $(APPDIR)
	rm -f $(BINDIR)/ccenter $(APPSDIR)/ccenter.desktop $(ICONDIR)/ccenter.svg $(UNITDIR)/ccenter.service
	rm -rf $(LICDIR)
	@echo
	@echo "Ccenter removed. Your settings are kept in ~/.config/ccenter (delete it by hand if you want)."
	@echo "If you enabled the background service: systemctl --user disable ccenter.service"

# Keyboard light permission: install the rule for one user and apply it right away (packagers: the rule needs a user
# id, so it can't be shipped in a package; users grant it from the app instead)
udev:
	@case "$(UDEV_UID)" in ""|*[!0-9]*) echo "Run it as: sudo make udev   (or UDEV_UID=<user id>)"; exit 1 ;; esac
	install -d $(UDEVDIR)
	sed 's/@UID@/$(UDEV_UID)/g' dist/90-ccenter.rules > $(UDEVDIR)/90-ccenter.rules.new
	sh scripts/kbd-rule-check.sh $(UDEVDIR)/90-ccenter.rules.new $(UDEV_UID) || { rm -f $(UDEVDIR)/90-ccenter.rules.new; exit 1; }
	chmod 644 $(UDEVDIR)/90-ccenter.rules.new
	mv $(UDEVDIR)/90-ccenter.rules.new $(UDEVDIR)/90-ccenter.rules
	@if [ -z "$(DESTDIR)" ] && command -v udevadm >/dev/null; then \
		udevadm control --reload-rules && udevadm trigger --action=change --subsystem-match=leds \
			&& udevadm trigger --action=change --subsystem-match=platform --sysname-match=hp-wmi; \
	fi

uninstall-udev:
	rm -f $(UDEVDIR)/90-ccenter.rules
	@if [ -z "$(DESTDIR)" ]; then \
		command -v udevadm >/dev/null && udevadm control --reload-rules; \
		for d in $(KBD_LEDS); do [ -d "$$d" ] || continue; \
			for f in brightness multi_intensity; do [ -e "$$d/$$f" ] && chown root:root "$$d/$$f" && chmod 0644 "$$d/$$f"; done; \
			[ -e "$$d/kbd_rgb_mode" ] && chown root:root "$$d/kbd_rgb_mode"; done; \
		for f in $(KBD_ZONES); do [ -e "$$f" ] && chown root:root "$$f" && chmod 0644 "$$f"; done; true; \
	fi
