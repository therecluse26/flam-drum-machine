# Building FLAM

FLAM is built using CMake and requires JUCE and platform-specific dependencies.

## Prerequisites

### All Platforms
- CMake 3.22 or newer
- C++20 compatible compiler (GCC 10+, Clang 11+, MSVC 2019+)
- Git

### Linux
Install the required development packages:

#### Ubuntu/Debian
```bash
sudo apt-get install \
    build-essential \
    cmake \
    git \
    libasound2-dev \
    libfreetype6-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libgl1-mesa-dev \
    libcurl4-openssl-dev \
    pkg-config
```

#### Fedora/RHEL
```bash
sudo dnf install \
    gcc-c++ \
    cmake \
    git \
    alsa-lib-devel \
    freetype-devel \
    libX11-devel \
    libXext-devel \
    libXrandr-devel \
    libXinerama-devel \
    libXcursor-devel \
    mesa-libGL-devel \
    libcurl-devel \
    pkg-config
```

#### Arch Linux
```bash
sudo pacman -S \
    base-devel \
    cmake \
    git \
    alsa-lib \
    freetype2 \
    libx11 \
    libxext \
    libxrandr \
    libxinerama \
    libxcursor \
    mesa \
    curl \
    pkg-config
```

### macOS
- Xcode Command Line Tools: `xcode-select --install`
- CMake: `brew install cmake`

### Windows
- Visual Studio 2019 or newer (Community Edition is fine)
- CMake: Download from [cmake.org](https://cmake.org/download/)

## Building

### Configure and Build
```bash
# Clone the repository (if not already done)
git clone https://github.com/flam/flam-drum-machine.git
cd flam-drum-machine

# Create build directory
mkdir build
cd build

# Configure CMake
cmake ..

# Build all targets
cmake --build . --config Release
```

### Build Options

```bash
# Build in Debug mode
cmake --build . --config Debug

# Build with specific number of parallel jobs
cmake --build . --config Release -j8

# Build only specific target
cmake --build . --target FLAM_Standalone --config Release
```

## Build Targets

FLAM generates multiple targets:

- `FLAM_Standalone` - Standalone application
- `FLAM_VST3` - VST3 plugin
- `FLAM_AU` - Audio Unit plugin (macOS only)
- `FLAM_AAX` - AAX plugin (requires AAX SDK)

## Installing

### Linux
```bash
# Install system-wide (requires sudo)
sudo cmake --install .

# Or install to user directory
cmake --install . --prefix ~/.local
```

### macOS
Built plugins are automatically placed in:
- Standalone: `build/FLAM_artefacts/`
- VST3: `~/Library/Audio/Plug-Ins/VST3/`
- AU: `~/Library/Audio/Plug-Ins/Components/`

### Windows
Built plugins are in `build\FLAM_artefacts\Release\`:
- Standalone: `.exe`
- VST3: `.vst3` folder

## Troubleshooting

### JUCE Download Issues
If JUCE fails to download automatically:
```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
git submodule update --init --recursive
```

### Missing Dependencies (Linux)
CMake will report missing packages. Install them using your package manager.

### Build Errors
1. Clean build directory: `rm -rf build && mkdir build`
2. Verify all dependencies are installed
3. Check CMake output for specific errors

### Plugin Not Detected by DAW
1. Verify plugin is in the correct directory
2. Rescan plugins in your DAW
3. Check that plugin binary has correct permissions
4. On macOS, plugins may need code signing

## Development

### IDE Setup

#### CLion / Qt Creator
Open `CMakeLists.txt` as a project.

#### Visual Studio
Generate VS solution:
```bash
cmake -G "Visual Studio 17 2022" -S . -B build
```

#### Xcode
Generate Xcode project:
```bash
cmake -G Xcode -S . -B build
```

### Code Style
- Follow JUCE coding conventions
- Use C++20 features where appropriate
- Maintain real-time safety in audio callback paths
- Document public APIs

## Performance Targets

FLAM is designed for:
- <5 ms latency at 64-sample buffer
- 64+ voice polyphony
- Stable at 44.1/48 kHz sample rates
- Low CPU usage (<10% on modern systems)

## See Also

- [CLAUDE.md](CLAUDE.md) - Project architecture and design goals
- [Resources/Kits/README.md](Resources/Kits/README.md) - Kit format specification
- [JUCE Documentation](https://juce.com/learn/documentation)
