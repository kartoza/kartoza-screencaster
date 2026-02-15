{
  description = "Kartoza Screencaster - Screen recording tool for Wayland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        version = "0.7.4";

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
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            # Go toolchain
            go
            gopls
            golangci-lint
            gomodifytags
            gotests
            impl
            delve
            go-tools

            # Build tools
            gnumake
            gcc
            pkg-config

            # CGO dependencies for systray
            gtk3
            glib
            libayatana-appindicator
            xorg.libX11
            xorg.libXcursor
            xorg.libXrandr
            xorg.libXinerama
            xorg.libXi
            xorg.libXxf86vm
            libGL

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
          ];

          shellHook = ''
            export EDITOR=nvim
            export GOPATH="$PWD/.go"
            export GOCACHE="$PWD/.go/cache"
            export GOMODCACHE="$PWD/.go/pkg/mod"
            export PATH="$GOPATH/bin:$PATH"

            # Enable CGO for systray support in dev shell
            export CGO_ENABLED=1

            # Helpful aliases
            alias gor='go run .'
            alias got='go test -v ./...'
            alias gob='go build -o bin/kartoza-screencaster .'
            alias gom='go mod tidy'
            alias gol='golangci-lint run'

            # Documentation aliases
            alias docs='mkdocs serve'
            alias docs-build='mkdocs build'

            echo ""
            echo "🎬 Kartoza Screencaster Development Environment"
            echo ""
            echo "Available commands:"
            echo "  gor  - Run the application"
            echo "  got  - Run tests"
            echo "  gob  - Build binary"
            echo "  gom  - Tidy go modules"
            echo "  gol  - Run linter"
            echo ""
            echo "Documentation:"
            echo "  docs       - Serve docs locally (http://localhost:8000)"
            echo "  docs-build - Build static docs site"
            echo ""
            echo "Make targets:"
            echo "  make build    - Build binary"
            echo "  make test     - Run tests"
            echo "  make lint     - Run linter"
            echo "  make release  - Build all platforms"
            echo ""
            echo "CGO is enabled for systray support."
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
        };
      }
    );
}
