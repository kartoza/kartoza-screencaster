{
  description = "Kartoza Screencaster - Screen recording tool for Wayland";

  # Allow unfree packages (required for Claude Code)
  nixConfig = {
    extra-substituters = [
      "https://numtide.cachix.org"
    ];
    extra-trusted-public-keys = [
      "numtide.cachix.org-1:2ps1kLBUWjxIneOy1Ik6cQjb41X0iXVXeHigGmycPPE="
    ];
  };

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # Sandboxing for AI agents
    jail-nix.url = "sourcehut:~alexdavid/jail.nix";

    # Claude Code CLI (uses its own nixpkgs for electron compatibility)
    llm-agents.url = "github:numtide/llm-agents.nix";

    # Google Antigravity IDE - disabled due to electron version mismatch
    # antigravity-nix.url = "github:jacopone/antigravity-nix";
    # antigravity-nix.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = { self, nixpkgs, flake-utils, jail-nix, llm-agents, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;  # Required for Claude Code
        };
        version = "0.8.2";

        # Initialize jail.nix for sandboxing AI agents
        jail = jail-nix.lib.init pkgs;

        # Project root for sandboxed access (update if project moves)
        projectRoot = builtins.toString ./.;
        homeDir = builtins.getEnv "HOME";

        # Jailed Claude Code - restricted to this project folder only
        jailedClaude = jail "claude-code" llm-agents.packages.${system}.claude-code [
          jail.combinators.network
          (jail.combinators.rw-bind projectRoot projectRoot)
          # Only Claude's own config directory
          (jail.combinators.rw-bind "${homeDir}/.claude" "${homeDir}/.claude")
        ];

        # Jailed Antigravity - disabled due to electron version mismatch
        # jailedAntigravity = jail "antigravity" antigravity-nix.packages.${system}.default [
        #   jail.combinators.network
        #   jail.combinators.gui
        #   jail.combinators.gpu
        #   (jail.combinators.rw-bind projectRoot projectRoot)
        #   # Only Antigravity's own config directory (capital A)
        #   (jail.combinators.rw-bind "${homeDir}/.config/Antigravity" "${homeDir}/.config/Antigravity")
        # ];

        # MkDocs with Material theme for documentation
        mkdocsEnv = pkgs.python3.withPackages (ps: with ps; [
          mkdocs
          mkdocs-material
          mkdocs-minify-plugin
          pygments
          pymdown-extensions
        ]);

        # Helper function for cross-compilation (CGO disabled - no systray support)
        mkCrossPackage = { pkgs, system, GOOS, GOARCH }:
          pkgs.buildGoModule {
            pname = "kartoza-screencaster";
            inherit version;
            src = ./.;

            vendorHash = null;

            CGO_ENABLED = 0;
            inherit GOOS GOARCH;

            ldflags = [
              "-s"
              "-w"
              "-X main.version=${version}"
            ];

            tags = [ "release" ];

            # Platform-specific binary name
            postInstall = ''
              cd $out/bin
              if [ "${GOOS}" = "windows" ]; then
                mv kartoza-screencaster kartoza-screencaster.exe
              fi

              # Create release tarball
              mkdir -p $out/release
              if [ "${GOOS}" = "windows" ]; then
                tar -czf $out/release/kartoza-screencaster-${GOOS}-${GOARCH}.tar.gz kartoza-screencaster.exe
              else
                tar -czf $out/release/kartoza-screencaster-${GOOS}-${GOARCH}.tar.gz kartoza-screencaster
              fi

              # Install desktop file and icon (Linux only)
              if [ "${GOOS}" = "linux" ]; then
                mkdir -p $out/share/applications
                cp ${./resources/kartoza-screencaster.desktop} $out/share/applications/kartoza-screencaster.desktop

                # Install icon to hicolor theme
                mkdir -p $out/share/icons/hicolor/scalable/apps
                cp ${./resources/icon_ready.svg} $out/share/icons/hicolor/scalable/apps/kartoza-screencaster.svg
              fi
            '';

            meta = with pkgs.lib; {
              description = "Screen recording tool for Wayland with audio processing";
              homepage = "https://github.com/kartoza/kartoza-screencaster";
              license = licenses.mit;
              maintainers = [ ];
              platforms = platforms.unix ++ platforms.windows;
            };
          };

        # Runtime dependencies for the application
        runtimeDeps = with pkgs; [
          # Core recording tools
          wl-screenrec         # Wayland screen recording
          ffmpeg               # Video/audio processing (includes ffprobe)
          pipewire             # Audio recording (pw-record)

          # Optional but recommended
          libnotify            # Desktop notifications (notify-send)
          pulseaudio           # Audio playback for countdown beeps (paplay)
        ];

        # Native package with CGO enabled for systray support (Linux only)
        mkNativePackage = { pkgs }:
          pkgs.buildGoModule {
            pname = "kartoza-screencaster";
            inherit version;
            src = ./.;

            # Use proxy mode for dependencies
            proxyVendor = true;
            vendorHash = "sha256-hudvYKdRjWTftQvtX40meJalnHukYV7LSFdz8562wTM=";

            # Required for systray (fyne.io/systray uses libayatana-appindicator)
            nativeBuildInputs = with pkgs; [
              pkg-config
              makeWrapper
            ];

            buildInputs = with pkgs; [
              # GTK and GLib for systray
              gtk3
              glib
              # AppIndicator support
              libayatana-appindicator
              # X11 libs (needed by some systray backends)
              xorg.libX11
              xorg.libXcursor
              xorg.libXrandr
              xorg.libXinerama
              xorg.libXi
              xorg.libXxf86vm
              # OpenGL (sometimes needed)
              libGL
            ];

            # Enable CGO for systray support
            preBuild = ''
              export CGO_ENABLED=1
            '';

            ldflags = [
              "-s"
              "-w"
              "-X main.version=${version}"
            ];

            tags = [ "release" ];

            postInstall = ''
              # Install desktop file
              mkdir -p $out/share/applications
              cp ${./resources/kartoza-screencaster.desktop} $out/share/applications/kartoza-screencaster.desktop

              # Install icon to hicolor theme
              mkdir -p $out/share/icons/hicolor/scalable/apps
              cp ${./resources/icon_ready.svg} $out/share/icons/hicolor/scalable/apps/kartoza-screencaster.svg

              # Wrap the binary with runtime dependencies in PATH
              wrapProgram $out/bin/kartoza-screencaster \
                --prefix PATH : ${pkgs.lib.makeBinPath runtimeDeps}
            '';

            meta = with pkgs.lib; {
              description = "Screen recording tool for Wayland with audio processing and systray support";
              homepage = "https://github.com/kartoza/kartoza-screencaster";
              license = licenses.mit;
              maintainers = [ ];
              platforms = platforms.linux;
            };
          };

      in
      {
        packages = {
          # Default package uses native CGO build with systray support on Linux
          default = if pkgs.stdenv.isLinux then
            mkNativePackage { inherit pkgs; }
          else
            mkCrossPackage {
              inherit pkgs system;
              GOOS = if pkgs.stdenv.isDarwin then "darwin" else "linux";
              GOARCH = if pkgs.stdenv.hostPlatform.isAarch64 then "arm64" else "amd64";
            };

          kartoza-screencaster = self.packages.${system}.default;

          # Native Linux package with CGO/systray support
          linux-native = mkNativePackage { inherit pkgs; };

          # Cross-compiled packages (no systray support)
          linux-amd64 = mkCrossPackage {
            inherit pkgs system;
            GOOS = "linux";
            GOARCH = "amd64";
          };

          linux-arm64 = mkCrossPackage {
            inherit pkgs system;
            GOOS = "linux";
            GOARCH = "arm64";
          };

          darwin-amd64 = mkCrossPackage {
            inherit pkgs system;
            GOOS = "darwin";
            GOARCH = "amd64";
          };

          darwin-arm64 = mkCrossPackage {
            inherit pkgs system;
            GOOS = "darwin";
            GOARCH = "arm64";
          };

          windows-amd64 = mkCrossPackage {
            inherit pkgs system;
            GOOS = "windows";
            GOARCH = "amd64";
          };

          # All releases combined
          all-releases = pkgs.symlinkJoin {
            name = "kartoza-screencaster-all-releases";
            paths = [
              self.packages.${system}.linux-amd64
              self.packages.${system}.linux-arm64
              self.packages.${system}.darwin-amd64
              self.packages.${system}.darwin-arm64
              self.packages.${system}.windows-amd64
            ];
          };

          # Jailed AI agents (sandboxed to project folder)
          claude-jailed = jailedClaude;
          # antigravity-jailed = jailedAntigravity;  # disabled due to electron version mismatch
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            # C++ build toolchain
            gcc
            pkg-config
            gnumake

            # Qt6
            qt6.qtbase
            qt6.qtmultimedia
            qt6.qtsvg
            qt6.qtwayland
            qt6.wrapQtAppsHook
            cmake
            ninja
            ccache

            # C++ development tools
            clang-tools          # clangd, clang-format, clang-tidy
            gdb
            valgrind
            doxygen
            graphviz             # for doxygen call graphs
            cppcheck

            # CLI utilities
            ripgrep
            fd
            eza
            bat
            fzf
            tree
            jq
            yq

            # Recording dependencies (for testing)
            wl-screenrec
            ffmpeg
            pipewire
            libnotify
            kooha              # GNOME-compatible screen recorder

            # Documentation
            mkdocsEnv

            # Nix tools
            nil
            nixpkgs-fmt
            nixfmt-classic

            # Git
            git
            gh

            # Security
            trivy

            # Jailed AI Agents (sandboxed to project folder only)
            jailedClaude
            # jailedAntigravity  # disabled due to electron version mismatch
          ];

          shellHook = ''
            export EDITOR=nvim

            # Ensure build directory exists
            mkdir -p build

            # Symlink compile_commands.json for clangd
            [ -f build/compile_commands.json ] && ln -sf build/compile_commands.json compile_commands.json

            # C++ development aliases
            alias cb='cd build && cmake .. -G Ninja && ninja && cd ..'
            alias cbr='cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja && cd ..'
            alias ct='cd build && ctest --output-on-failure && cd ..'
            alias cr='./build/kartoza-screencaster'
            alias cf='find src tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i'

            # Documentation aliases
            alias docs='mkdocs serve'
            alias docs-build='mkdocs build'
            alias doxygen-build='cd build && ninja docs && cd ..'
            alias doxygen-open='cd build && ninja docs-open && cd ..'

            echo ""
            echo "🎬 Kartoza Screencaster Development Environment (C++/Qt6)"
            echo ""
            echo "Build commands:"
            echo "  cb   - Configure + build (Debug)"
            echo "  cbr  - Configure + build (Release, optimised+stripped)"
            echo "  ct   - Run all tests"
            echo "  cr   - Run the application"
            echo "  cf   - Format all C++ code (clang-format)"
            echo ""
            echo "Documentation:"
            echo "  docs          - Serve mkdocs (localhost:8000)"
            echo "  doxygen-build - Generate Doxygen API docs"
            echo "  doxygen-open  - Generate + open Doxygen in browser"
            echo ""
            echo "Neovim: <leader>p for all project commands"
            echo ""
          '';
        };

        apps = {
          default = {
            type = "app";
            program = "${self.packages.${system}.default}/bin/kartoza-screencaster";
          };

          setup = {
            type = "app";
            program = toString (pkgs.writeShellScript "setup" ''
              echo "Initializing kartoza-screencaster..."
              go mod download
              go mod tidy
              echo "Setup complete!"
            '');
          };

          release = {
            type = "app";
            program = toString (pkgs.writeShellScript "release" ''
              echo "Building all release binaries..."
              nix build .#all-releases
              mkdir -p release
              cp -r result/release/* release/
              echo "Release binaries created in ./release/"
            '');
          };

          release-upload = {
            type = "app";
            program = toString (pkgs.writeShellScript "release-upload" ''
              TAG="$1"
              if [ -z "$TAG" ]; then
                echo "Usage: nix run .#release-upload -- vX.Y.Z"
                exit 1
              fi

              echo "Building and uploading release $TAG..."
              nix build .#all-releases
              mkdir -p release
              cp -r result/release/* release/

              # Generate checksums
              cd release
              sha256sum *.tar.gz > checksums.txt
              cd ..

              # Upload to GitHub
              gh release upload "$TAG" release/*.tar.gz release/checksums.txt --clobber

              echo "Release $TAG uploaded successfully!"
            '');
          };

          # Jailed AI agents - restricted to this project folder
          claude = {
            type = "app";
            program = "${jailedClaude}/bin/claude-code";
          };

          # antigravity = {  # disabled due to electron version mismatch
          #   type = "app";
          #   program = "${jailedAntigravity}/bin/antigravity";
          # };
        };
      }
    );
}
