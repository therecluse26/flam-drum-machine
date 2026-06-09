# FlamKit Build Instructions

## NixOS / Nix Package Manager

This project uses Nix for dependency management to ensure reproducible builds.

### Prerequisites

The `shell.nix` file in the project root includes all required dependencies:
- CMake, pkg-config, make, gcc (build tools)
- yaml-cpp (FlamKit file format parsing)
- JUCE dependencies (X11, ALSA, freetype, fontconfig, etc.)

### Building the Project

**Step 1: Enter the Nix Shell Environment**

```bash
cd /home/brad/Code/personal/flam-drum-machine
nix-shell
```

This will load all dependencies from `shell.nix`. You should see:
```
JUCE development environment loaded
Run 'cmake ..' from your build directory to configure the project
```

**Step 2: Configure with CMake**

```bash
mkdir -p build
cd build
cmake ..
```

**Step 3: Build**

Choose your target:

```bash
# Build all targets
make

# Or build specific targets:
make FLAM_VST3        # VST3 plugin
make FLAM_Standalone  # Standalone application
make FLAM_AU          # Audio Unit (macOS only)
```

### Build Output Locations

After building, the plugin/app will be located at:

- **VST3**: `build/FLAM_artefacts/VST3/FLAM.vst3/`
- **Standalone**: `build/FLAM_artefacts/Standalone/FLAM`
- **AU**: `build/FLAM_artefacts/AU/FLAM.component/` (macOS only)

---

## Building Without YAML Support (Not Recommended)

If you build outside the nix-shell, the project will compile **without yaml-cpp**, meaning:
- ✅ Plugin will build successfully
- ❌ FlamKit cannot load `.yaml` or `.yml` kit files
- ✅ JSON format still works (using JUCE's built-in JSON parser)

To build without YAML support:

```bash
mkdir -p build
cd build
cmake ..  # You'll see warnings about yaml-cpp not found
make
```

You will see warnings like:
```
CMake Warning:
  yaml-cpp not found via pkg-config. FlamKitLoader will only support JSON format.
  To enable YAML support on NixOS: run 'nix-shell' from the project root before building.
```

---

## Troubleshooting

### Error: `cannot find -lyaml-cpp`

**Cause**: CMake was configured inside nix-shell, but you're now building outside of it.

**Solution**:
```bash
rm -rf build  # Clean previous configuration
nix-shell     # Enter nix environment
mkdir build && cd build
cmake ..
make
```

### Error: `X11/Xlib.h: No such file or directory`

**Cause**: JUCE requires X11 headers, which are provided by the nix-shell environment.

**Solution**: Same as above - enter `nix-shell` before building.

### Error: `juceaide failed to build`

**Cause**: Usually means missing system dependencies (X11, freetype, etc.)

**Solution**: Always build inside `nix-shell`.

---

## Development Workflow

**Recommended workflow for development:**

```bash
# 1. Enter nix-shell ONCE per terminal session
nix-shell

# 2. Configure CMake (only needed once, or after CMakeLists.txt changes)
mkdir -p build && cd build
cmake ..

# 3. Build repeatedly as you make changes
make FLAM_VST3

# 4. Test your changes
./FLAM_artefacts/Standalone/FLAM
```

**Stay inside nix-shell** for the entire development session. You can run git commands, edit files, build, and test - all within the nix environment.

---

## Adding Dependencies

To add new system dependencies, edit `shell.nix`:

```nix
{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    # ... existing dependencies ...

    # Add new dependency here:
    my-new-library
  ];
}
```

Then reload the shell:
```bash
exit          # Exit current nix-shell
nix-shell     # Re-enter with updated dependencies
```

---

## CI/CD Notes

For automated builds (GitHub Actions, etc.), use:

```yaml
- name: Setup Nix
  uses: cachix/install-nix-action@v22

- name: Build
  run: |
    nix-shell --run "mkdir -p build && cd build && cmake .. && make"
```

---

## Platform-Specific Notes

### Linux
- Uses PipeWire for audio (fallback to ALSA)
- Requires X11 display server (Wayland support via XWayland)

### macOS
- VST3 and AU formats supported
- Requires Xcode Command Line Tools
- Use `shell.nix` with Homebrew yaml-cpp as alternative

### Windows
- Not currently tested with Nix
- Use vcpkg or manual dependency installation
- See JUCE documentation for Visual Studio setup
