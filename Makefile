.PHONY: all configure build clean rebuild install run help \
        nix-configure nix-build nix-install \
        test test-unit test-golden test-determinism golden-update \
        test-pluginval test-sanitize test-all standalone

# ---------------------------------------------------------------------------
# Platform indirection
#
#   NixOS (default): every recipe runs inside `nix-shell --run "<cmd>"`, which
#   provides JUCE's system deps (ALSA, X11, FreeType, ...).
#
#   Other platforms (Ubuntu/macOS) where the deps are already on PATH: run the
#   commands directly by overriding RUN, e.g.
#       make test     RUN='sh -c'
#       make run      RUN='sh -c'
#
# Both `nix-shell --run` and `sh -c` take a single quoted command string, so
# every recipe below is written as  $(RUN) "…".
# ---------------------------------------------------------------------------
RUN       ?= nix-shell --run
BUILD_DIR ?= build

# Built test binaries (note the case: L1 lives under Tests/, Catch2 under tests/)
TESTS_BIN  = $(BUILD_DIR)/tests/flam-tests_artefacts/flam-tests

# Default target
all: build

# ---------------------------------------------------------------------------
# Configure / build
# ---------------------------------------------------------------------------

# Configure once. CMakeCache.txt acts as the stamp so `make test`/`make build`
# don't re-invoke CMake every time; CMake self-reconfigures when CMakeLists.txt
# changes, so correctness is preserved without the per-call cost.
$(BUILD_DIR)/CMakeCache.txt:
	$(RUN) "cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON"

configure: $(BUILD_DIR)/CMakeCache.txt

# Build everything (plugin + tests)
build: configure
	$(RUN) "cmake --build $(BUILD_DIR) --parallel"

# ---------------------------------------------------------------------------
# Test layers (all headless — no DAW required). See docs/testing.md.
# ---------------------------------------------------------------------------

# L1 + L2: the everyday no-DAW loop (unit + golden render). Excludes pluginval.
test: configure
	$(RUN) "cmake --build $(BUILD_DIR) --target flam-tests FlamL1Tests --parallel"
	$(RUN) "ctest --test-dir $(BUILD_DIR) --exclude-regex FLAM_L3 --output-on-failure"

# L1 only: pure-logic units (FlamKitLoader, VoiceManager, DSP).
test-unit: configure
	$(RUN) "cmake --build $(BUILD_DIR) --target FlamL1Tests --parallel"
	$(RUN) "ctest --test-dir $(BUILD_DIR) -R FlamL1UnitTests --output-on-failure"

# L2 only: bit-exact golden render null-test.
test-golden: configure
	$(RUN) "cmake --build $(BUILD_DIR) --target flam-tests --parallel"
	$(RUN) "$(TESTS_BIN) '[golden_render]'"

# L2: determinism (seeded RNG / round-robin / humanization).
test-determinism: configure
	$(RUN) "cmake --build $(BUILD_DIR) --target flam-tests --parallel"
	$(RUN) "$(TESTS_BIN) '[determinism]'"

# Re-bless the golden reference after an *intentional* DSP change. Review the
# audio diff before committing (see docs/testing.md §7).
golden-update: configure
	$(RUN) "cmake --build $(BUILD_DIR) --target flam-tests --parallel"
	$(RUN) "FLAM_UPDATE_GOLDEN=1 $(TESTS_BIN) '[golden_render]'"

# L3: pluginval host-contract on the built VST3. Reconfigures with the
# pluginval option enabled (downloads pluginval on first run).
test-pluginval:
	$(RUN) "cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DFLAM_PLUGINVAL=ON"
	$(RUN) "cmake --build $(BUILD_DIR) --parallel"
	$(RUN) "ctest --test-dir $(BUILD_DIR) -R FLAM_L3_PluginvalHostContract --output-on-failure"

# L4: real-time-safety under ASan+UBSan (uses its own build_sanitized/ tree).
test-sanitize:
	$(RUN) "./scripts/run-sanitized-tests.sh"

# Full local gate (mirrors CI): L1+L2, then L3, then L4. Run serially via
# recursive make so a parallel `-j` can't corrupt the shared CMake cache.
test-all:
	$(MAKE) test
	$(MAKE) test-pluginval
	$(MAKE) test-sanitize

# ---------------------------------------------------------------------------
# Standalone app (L5 — manual / interactive, no DAW)
# ---------------------------------------------------------------------------

# Build only the Standalone target.
standalone: configure
	$(RUN) "cmake --build $(BUILD_DIR) --target FLAM_Standalone --parallel"

# Build + launch the Standalone app. Linux needs a live display; for headless
# use an Xvfb session, e.g.
#   Xvfb :99 & DISPLAY=:99 make run
run: standalone
	$(RUN) "$(BUILD_DIR)/FLAM_artefacts/Standalone/FLAM"

# ---------------------------------------------------------------------------
# Housekeeping / back-compat
# ---------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) build_sanitized build_verify

rebuild: clean build

# Legacy Nix-prefixed targets (kept for back-compat with existing workflows)
nix-configure:
	$(RUN) "cmake -B $(BUILD_DIR) -S ."

nix-build:
	$(RUN) "cmake --build $(BUILD_DIR) -j$$(nproc)"

nix-install install:
	$(RUN) "cmake --install $(BUILD_DIR) --prefix ~/.local"

help:
	@echo "FLAM Makefile targets"
	@echo ""
	@echo "  Default (Nix): recipes run inside 'nix-shell --run'."
	@echo "  Non-Nix: append RUN='sh -c', e.g.  make test RUN='sh -c'"
	@echo ""
	@echo "  Build:"
	@echo "    make configure        Configure build/ with tests enabled (Debug)"
	@echo "    make build            Build plugin + tests (default)"
	@echo "    make standalone       Build only the Standalone app"
	@echo "    make run              Build + launch the Standalone app (no DAW)"
	@echo "    make clean            Remove build/, build_sanitized/, build_verify/"
	@echo "    make rebuild          clean + build"
	@echo ""
	@echo "  Test (all headless, no DAW):"
	@echo "    make test             L1+L2: unit + golden render (everyday loop)"
	@echo "    make test-unit        L1 only: pure-logic unit tests"
	@echo "    make test-golden      L2 only: bit-exact golden render null-test"
	@echo "    make test-determinism L2: seeded-RNG determinism"
	@echo "    make golden-update    Re-bless the golden reference (intentional DSP change)"
	@echo "    make test-pluginval   L3: pluginval host-contract (VST3)"
	@echo "    make test-sanitize    L4: ASan+UBSan real-time-safety"
	@echo "    make test-all         Full local gate: L1+L2, L3, L4 (mirrors CI)"
	@echo ""
	@echo "  Legacy:"
	@echo "    make nix-configure / nix-build / install"
	@echo "    make help             Show this message"
