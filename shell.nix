{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    # Build tools
    cmake
    pkg-config
    gnumake
    gcc

    # YAML parsing
    yaml-cpp

    # JUCE dependencies
    alsa-lib
    alsa-lib.dev
    freetype
    freetype.dev
    fontconfig
    fontconfig.dev
    libGL
    libGL.dev
    curl
    curl.dev

    # X11 libraries
    xorg.libX11
    xorg.libX11.dev
    xorg.libXinerama
    xorg.libXinerama.dev
    xorg.libXext
    xorg.libXext.dev
    xorg.libXrandr
    xorg.libXrandr.dev
    xorg.libXcursor
    xorg.libXcursor.dev

    # GTK/WebKit (if using JUCE WebBrowserComponent)
    gtk3
    gtk3.dev
    webkitgtk_4_1
  ];

  shellHook = ''
    echo "JUCE development environment loaded"
    echo "Run 'cmake ..' from your build directory to configure the project"
  '';
}
