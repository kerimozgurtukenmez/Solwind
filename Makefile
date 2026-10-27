# Solwind is built and installed with make, for the whole system (the only way to install it):
#   make                       builds the background service solwindd and the window's QML module (CMake, Qt 6;
#                              without sudo, into build/)
#   sudo make install          installs under /usr/local (somewhere else: PREFIX=...)
#   sudo make uninstall        removes it, and the keyboard permission (your settings in ~/.config/solwind stay)
#   sudo make udev             keyboard light permission for you (optional; the app's "Grant permission" does the same)
#   sudo make uninstall-udev   removes that permission
#                              No plugin comes with Solwind (without one it only reads the sensors): solwind plugins
#                              lists them, solwind plugin install <name> downloads, builds and installs one (no sudo)
#   make remove-old            (without sudo) removes an install in your home folder made by the old install.sh
#                              (Solwind 0.4.0, or Ccenter, its old name); make install asks for it when it finds one
# Updating: git pull, make, solwind quit, sudo make install, solwind plugin update (a plugin is loaded only by the
# Solwind it was built for), then start it again (make install says how).
# Packaging: make DESTDIR=/package/root PREFIX=/usr install   (the checks against the running system are skipped)

PREFIX  ?= /usr/local
DESTDIR ?=
# A trailing slash would end up in the paths written into the menu entry and the service ("DESTDIR=/" = none)
override PREFIX  := $(patsubst %/,%,$(PREFIX))
override DESTDIR := $(patsubst %/,%,$(DESTDIR))
# The recipes use the paths unquoted, so only plain absolute ones are accepted: a space would make "rm -rf" remove
# another folder. Checked in the shell, through the environment (exactly as given).
export SW_PREFIX  := $(PREFIX)
export SW_DESTDIR := $(DESTDIR)
CHECK_PATHS = case "$$SW_PREFIX" in /?*) ;; *) echo "PREFIX must be an absolute path (got: '$$SW_PREFIX')" >&2; exit 1 ;; esac; \
	case "$$SW_DESTDIR" in ""|/?*) ;; *) echo "DESTDIR must be empty or an absolute path (got: '$$SW_DESTDIR')" >&2; exit 1 ;; esac; \
	case "$$SW_PREFIX$$SW_DESTDIR" in *[!A-Za-z0-9_./+-]*) \
		echo "PREFIX and DESTDIR may only use letters, digits and _ . / + - (no spaces)" >&2; exit 1 ;; esac

# The build (make): solwindd and the QML module Solwind. build/, or BUILD=<folder> on the command line (not from the
# environment: BUILD is a common name)
BUILD := $(if $(filter command line,$(origin BUILD)),$(BUILD),build)
APPDIR  = $(DESTDIR)$(PREFIX)/share/solwind
# solwindd, its plugin SDK library, solwind-plugin and the QML module (the launcher finds them next to share/solwind:
# <prefix>/lib/solwind)
LIBDIR  = $(DESTDIR)$(PREFIX)/lib/solwind
# The plugin SDK for building plugins (solwind plugin install): its headers and its CMake package
INCDIR  = $(DESTDIR)$(PREFIX)/include/solwind
SDKDIR  = $(DESTDIR)$(PREFIX)/lib/cmake/SolwindSdk
BINDIR  = $(DESTDIR)$(PREFIX)/bin
APPSDIR = $(DESTDIR)$(PREFIX)/share/applications
ICONDIR = $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps
UNITDIR = $(DESTDIR)$(PREFIX)/lib/systemd/user
LICDIR  = $(DESTDIR)$(PREFIX)/share/licenses/solwind
# The keyboard rule has one place, whatever the prefix: the app's grant/remove buttons use the same file
UDEVDIR = $(DESTDIR)/etc/udev/rules.d
# Keyboard lights (their files go back to root when the permission is removed)
KBD_LEDS = /sys/class/leds/*::kbd_backlight /sys/class/leds/*::kbd_backlight_* /sys/class/leds/*::kbd_zoned_backlight-*
# HP four-zone colour files (older copies of the hp-wmi-fan-and-backlight-control module)
KBD_ZONES = /sys/devices/platform/hp-wmi/rgb_zones/zone*
# The rule gives access to one user: the one running "sudo make udev" (SUDO_UID), or UDEV_UID=<id>
UDEV_UID ?= $(SUDO_UID)
# Old Makefiles (Ccenter) put the rule here; it is removed when the permission is set again or removed
OLD_RULES = /usr/lib/udev/rules.d/90-ccenter.rules /usr/local/lib/udev/rules.d/90-ccenter.rules

# /usr and /usr/local are on every PATH and XDG_DATA_DIRS: the menu entry uses the short command and the theme icon;
# anywhere else full paths
STD          = $(filter /usr /usr/local,$(PREFIX))
DESKTOP_EXEC = $(if $(STD),solwind,$(PREFIX)/bin/solwind)
DESKTOP_ICON = $(if $(STD),solwind,$(PREFIX)/share/icons/hicolor/scalable/apps/solwind.svg)

# Did the menu folder exist before this install? (read when make starts, before any recipe runs) A launcher that was
# already running doesn't look at a folder that appeared after it started (Quickshell's, which Caelestia uses: checked in
# its source, and seen here), so the message after the install says when Solwind shows up in the menu.
APPS_NEW := $(if $(wildcard $(APPSDIR)),,1)

# Files the app needs at runtime (README, dist etc. are not installed; the license goes to share/licenses)
APP_FILES = shell.qml VERSION plugin-catalog.json
APP_DIRS  = components modules services utils scripts assets

# The light files back to root (owner root, 0644; kbd_rgb_mode keeps its write-only mode); only as root, for a real
# system (no DESTDIR). Only files our rule (owner changed) or an older one (0666) touched: a permission another rule
# gave (a group, root-owned) stays.
RESET_PERMS = rp() { [ -e "$$1" ] || return 0; case "$$(stat -c "%u %a" "$$1")" in "0 666") ;; "0 "*) return 0 ;; esac; \
		chown root:root "$$1"; [ -z "$${2:-}" ] || chmod 0644 "$$1"; }; \
	for d in $(KBD_LEDS); do [ -d "$$d" ] || continue; rp "$$d/brightness" m; rp "$$d/multi_intensity" m; rp "$$d/kbd_rgb_mode"; done; \
	for f in $(KBD_ZONES); do rp "$$f" m; done; true
LIVE = [ -z "$(DESTDIR)" ] && [ "$$(id -u)" = 0 ]


.PHONY: all build help install uninstall udev uninstall-udev remove-old

all: build

# Release build, without the tests. RPATH: solwindd only $ORIGIN (its libsolwind-sdk.so lies next to it); the QML
# module none (the system's Qt). Not as
# root: the build folder would belong to root.
build:
	@command -v cmake >/dev/null 2>&1 || { echo "cmake is needed to build Solwind (and Qt 6: base, declarative)" >&2; exit 1; }
	@[ "$$(id -u)" != 0 ] || { echo "Build without sudo: make   (then: sudo make install)" >&2; exit 1; }
	@# (CMAKE_SKIP_RPATH=OFF: a build folder of Solwind 1.0 has ON in its cache, and solwindd wouldn't find its SDK)
	cmake -S backend -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DSOLWIND_BUILD_TESTS=OFF -DCMAKE_SKIP_RPATH=OFF \
		$(if $(wildcard $(BUILD)/CMakeCache.txt),,$(if $(shell command -v ninja),-G Ninja,))
	cmake --build $(BUILD)
	@# (what make install compares the sources with: a build that had nothing to do still counts as up to date)
	@touch $(BUILD)/.solwind-built
	@echo; echo "Built. To install it: sudo make install   (then the plugins you need: solwind plugins)"

help:
	@echo "Solwind $$(cat VERSION)"
	@echo "  make                       build it (without sudo)"
	@echo "  sudo make install          install under $(PREFIX)"
	@echo "  sudo make uninstall        remove it (your settings stay)"
	@echo "  sudo make udev             keyboard light permission for you (optional)"
	@echo "  sudo make uninstall-udev   remove that permission"
	@echo "  make remove-old            (without sudo) remove an install in your home folder made by the old install.sh"

install:
	@[ -x $(BUILD)/solwindd ] && [ -f $(BUILD)/libsolwind-sdk.so ] && [ -x $(BUILD)/solwind-plugin ] && [ -f $(BUILD)/sdk-install/SolwindSdkConfig.cmake ] \
		&& [ -f $(BUILD)/qml/Solwind/qmldir ] && [ -f $(BUILD)/qml/Solwind/libsolwindqml.so ] \
		|| { echo "Not built yet: run make first (without sudo), then sudo make install" >&2; exit 1; }
	@# a build older than this tree (git pull without make) would put an old solwindd next to the new window app
	@v=$$($(BUILD)/solwindd --version 2>/dev/null); [ "$$v" = "solwindd $$(cat VERSION)" ] \
		|| { echo "The build is of another version ($$v; this tree: $$(cat VERSION)): run make first (without sudo)" >&2; exit 1; }
	@[ -f $(BUILD)/.solwind-built ] || { echo "Not built with make yet: run make first (without sudo)" >&2; exit 1; }
	@n=$$([ ! -d backend ] || find backend -path backend/tests -prune -o -newer $(BUILD)/.solwind-built -type f -print 2>/dev/null | head -n 1); \
		[ -z "$$n" ] || { echo "The build is older than the source ($$n changed): run make first (without sudo)" >&2; exit 1; }
	@$(CHECK_PATHS)
	@[ -n "$(DESTDIR)" ] || sh dist/install-check.sh install "$(PREFIX)"
	@# the new copy is built next to the old one and swapped in only when complete (files of older versions go too)
	rm -rf $(APPDIR).new $(APPDIR).old
	install -d $(APPDIR).new/dist
	cp -r --no-preserve=ownership,mode $(APP_FILES) $(APP_DIRS) $(APPDIR).new/
	install -m 644 dist/90-solwind.rules $(APPDIR).new/dist/
	find $(APPDIR).new -type d -exec chmod 755 {} +
	find $(APPDIR).new -type f -exec chmod 644 {} +
	chmod 755 $(APPDIR).new/scripts/*.sh $(APPDIR).new/scripts/*.py
	if [ -e $(APPDIR) ]; then mv $(APPDIR) $(APPDIR).old; fi
	mv $(APPDIR).new $(APPDIR)
	rm -rf $(APPDIR).old
	@# solwindd and the QML module, swapped in the same way
	rm -rf $(LIBDIR).new $(LIBDIR).old
	install -d $(LIBDIR).new/qml/Solwind
	install -m 755 $(BUILD)/solwindd $(LIBDIR).new/solwindd
	install -m 644 $(BUILD)/libsolwind-sdk.so $(LIBDIR).new/libsolwind-sdk.so
	install -m 755 $(BUILD)/solwind-plugin $(LIBDIR).new/solwind-plugin
	install -m 755 $(BUILD)/qml/Solwind/libsolwindqml.so $(LIBDIR).new/qml/Solwind/
	install -m 644 $(BUILD)/qml/Solwind/qmldir $(BUILD)/qml/Solwind/solwindqml.qmltypes $(LIBDIR).new/qml/Solwind/
	if [ -e $(LIBDIR) ]; then mv $(LIBDIR) $(LIBDIR).old; fi
	mv $(LIBDIR).new $(LIBDIR)
	rm -rf $(LIBDIR).old
	@# the plugin SDK: the headers (by their path under backend/src) and the CMake package (SolwindSdkConfig.cmake)
	rm -rf $(INCDIR) $(SDKDIR)
	while read -r h; do install -Dm 644 backend/$$h $(INCDIR)/$${h#src/} || exit 1; done < $(BUILD)/sdk-install/headers.txt
	install -Dm 644 -t $(SDKDIR) $(BUILD)/sdk-install/SolwindSdkConfig.cmake $(BUILD)/sdk-install/SolwindPlugin.cmake
	install -Dm 755 bin/solwind $(BINDIR)/solwind
	install -d $(APPSDIR)
	sed -e 's|@BINDIR@/solwind|$(DESKTOP_EXEC)|g' -e 's|^Icon=solwind$$|Icon=$(DESKTOP_ICON)|' dist/solwind.desktop > $(APPSDIR)/solwind.desktop
	chmod 644 $(APPSDIR)/solwind.desktop
	install -Dm 644 assets/solwind.svg $(ICONDIR)/solwind.svg
	install -d $(UNITDIR)
	sed -e 's|@BINDIR@|$(PREFIX)/bin|g' -e 's|@LIBDIR@|$(PREFIX)/lib/solwind|g' -e 's|@APPDIR@|$(PREFIX)/share/solwind|g' \
		dist/solwind.service > $(UNITDIR)/solwind.service
	sed 's|@BINDIR@|$(PREFIX)/bin|g' dist/solwind-tray.service > $(UNITDIR)/solwind-tray.service
	chmod 644 $(UNITDIR)/solwind.service $(UNITDIR)/solwind-tray.service
	@# the tray icon starts with the service (whoever starts it: login, solwind); it ends by itself when it's off
	install -d $(UNITDIR)/solwind.service.wants
	ln -sfn ../solwind-tray.service $(UNITDIR)/solwind.service.wants/solwind-tray.service
	install -Dm 644 LICENSE $(LICDIR)/LICENSE
	@[ -n "$(DESTDIR)" ] || sh dist/install-check.sh installed "$(PREFIX)" "$(APPS_NEW)"

uninstall:
	@$(CHECK_PATHS)
	@[ -n "$(DESTDIR)" ] || sh dist/install-check.sh uninstall "$(PREFIX)"
	rm -rf $(APPDIR) $(APPDIR).new $(APPDIR).old $(LIBDIR) $(LIBDIR).new $(LIBDIR).old $(LICDIR) $(INCDIR) $(SDKDIR)
	@# the command only if it is Solwind's launcher (a 'solwind' of another program stays)
	if [ -f $(BINDIR)/solwind ] && head -n 2 $(BINDIR)/solwind | grep -q '^# Solwind launcher'; then rm -f $(BINDIR)/solwind; fi
	rm -f $(APPSDIR)/solwind.desktop $(ICONDIR)/solwind.svg $(UNITDIR)/solwind.service $(UNITDIR)/solwind-tray.service
	rm -f $(UNITDIR)/solwind.service.wants/solwind-tray.service
	rmdir $(UNITDIR)/solwind.service.wants 2>/dev/null || true
	@# the keyboard permission (one rule in /etc for every install): only as root, and not while another Solwind
	@# install is left that uses it
	@if [ -n "$(DESTDIR)" ]; then $(MAKE) --no-print-directory uninstall-udev; \
	elif [ "$$(id -u)" != 0 ]; then echo "(not root: the keyboard permission in $(UDEVDIR) is not touched)"; \
	elif [ -f /usr/share/solwind/shell.qml ] || [ -f /usr/local/share/solwind/shell.qml ]; then \
		echo "(the keyboard permission stays: another Solwind install uses it)"; \
	else $(MAKE) --no-print-directory uninstall-udev; fi
	@[ -n "$(DESTDIR)" ] || sh dist/install-check.sh removed "$(PREFIX)"

# Keyboard light permission: the rule for one user, applied right away (packagers: the rule needs a user id, so it
# can't be shipped in a package; users grant it from the app instead). The rendered rule is checked line by line
# before it is put in place; the rule under the old name (Ccenter's) goes.
udev:
	@$(CHECK_PATHS)
	@case "$(UDEV_UID)" in ""|*[!0-9]*) echo "Run it as: sudo make udev   (or UDEV_UID=<user id>)" >&2; exit 1 ;; esac
	install -d $(UDEVDIR)
	sed 's/@UID@/$(UDEV_UID)/g' dist/90-solwind.rules > $(UDEVDIR)/90-solwind.rules.new
	sh scripts/kbd-rule-check.sh $(UDEVDIR)/90-solwind.rules.new $(UDEV_UID) || { rm -f $(UDEVDIR)/90-solwind.rules.new; exit 1; }
	chmod 644 $(UDEVDIR)/90-solwind.rules.new
	@# earlier versions made the files writable for everyone (0666): reset them first
	@if $(LIVE); then $(RESET_PERMS); fi
	mv $(UDEVDIR)/90-solwind.rules.new $(UDEVDIR)/90-solwind.rules
	rm -f $(UDEVDIR)/90-ccenter.rules
	@if $(LIVE); then for f in $(OLD_RULES); do \
		if [ -f "$$f" ] && head -n 1 "$$f" | grep -q '^# Ccenter: '; then echo "rm -f $$f"; rm -f "$$f"; fi; done; fi
	@if $(LIVE) && command -v udevadm >/dev/null; then \
		udevadm control --reload-rules && udevadm trigger --action=change --subsystem-match=leds \
			&& udevadm trigger --action=change --subsystem-match=platform --sysname-match=hp-wmi && udevadm settle; \
	fi

uninstall-udev:
	@$(CHECK_PATHS)
	rm -f $(UDEVDIR)/90-solwind.rules $(UDEVDIR)/90-ccenter.rules
	@if $(LIVE); then for f in $(OLD_RULES); do \
		if [ -f "$$f" ] && head -n 1 "$$f" | grep -q '^# Ccenter: '; then echo "rm -f $$f"; rm -f "$$f"; fi; done; fi
	@if $(LIVE); then command -v udevadm >/dev/null && udevadm control --reload-rules; $(RESET_PERMS); fi

# Runs as you (not root): it changes only your home folder
remove-old:
	@sh dist/remove-old.sh
