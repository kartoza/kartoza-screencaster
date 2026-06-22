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
        # Version extracted from CMakeLists.txt (single source of truth)
        version = builtins.head (builtins.match ".*project\\(kartoza-screencaster VERSION ([0-9.]+).*" (builtins.readFile ./CMakeLists.txt));

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

        # MkDocs with Material theme for documentation. The plugin set
        # mirrors what the Docs.yml CI workflow installs.
        mkdocsEnv = pkgs.python3.withPackages (ps: with ps; [
          mkdocs
          mkdocs-material
          mkdocs-minify-plugin
          mkdocs-glightbox
          mkdocs-git-revision-date-localized-plugin
          pygments
          pymdown-extensions
        ]);

        # Dev shell commands (work in any shell: bash, fish, zsh)
        devScripts = [
          (pkgs.writeShellScriptBin "cb" ''
            cd build && cmake .. -G Ninja && ninja && cd ..
          '')
          (pkgs.writeShellScriptBin "cbr" ''
            cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja && cd ..
          '')
          (pkgs.writeShellScriptBin "ct" ''
            cd build && ctest --output-on-failure && cd ..
          '')
          (pkgs.writeShellScriptBin "cr" ''
            ./build/kartoza-screencaster
          '')
          (pkgs.writeShellScriptBin "cf" ''
            find src tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i
          '')
          (pkgs.writeShellScriptBin "cclean" ''
            rm -rf build/* && cd build && cmake .. -G Ninja && ninja && cd ..
          '')
          (pkgs.writeShellScriptBin "ctr" ''
            cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -R test_merger_exhaustive && cd ..
            echo "Opening test renders..."
            for f in $(ls tests/test_outputs/land_*.mp4 tests/test_outputs/vert_*.mp4 2>/dev/null | sort); do
              xdg-open "$f"
              sleep 3
            done
          '')
          (pkgs.writeShellScriptBin "docs" ''
            mkdocs serve
          '')
          (pkgs.writeShellScriptBin "docs-build" ''
            mkdocs build
          '')
          (pkgs.writeShellScriptBin "doxygen-build" ''
            cd build && ninja docs && cd ..
          '')
          (pkgs.writeShellScriptBin "doxygen-open" ''
            cd build && ninja docs-open && cd ..
          '')
        ];

        # Runtime dependencies wrapped into PATH
        runtimeDeps = with pkgs; [
          wl-screenrec                # Wayland (wlroots) screen recording
          ffmpeg                      # Video/audio processing (includes ffprobe)
          grim                        # Wayland (wlroots) screenshot
          xorg.xrandr                 # X11 monitor enumeration
          # GStreamer + plugins for the xdg-desktop-portal recording path
          # (GNOME/KDE Wayland — reads from PipeWire screencast node).
          gst_all_1.gstreamer
          gst_all_1.gst-plugins-base  # videoconvert
          gst_all_1.gst-plugins-good  # mp4mux
          gst_all_1.gst-plugins-ugly  # x264enc
          gst_all_1.gst-libav
          pipewire                    # provides gst-plugin-pipewire (pipewiresrc)
        ];

        # CMake/Qt6-based package
        mkPackage = { buildType ? "Release" }:
          pkgs.stdenv.mkDerivation {
            pname = "kartoza-screencaster";
            inherit version;
            src = pkgs.lib.cleanSource ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              pkg-config
              qt6.wrapQtAppsHook
            ];

            buildInputs = with pkgs; [
              qt6.qtbase
              qt6.qtmultimedia
              qt6.qtsvg
              qt6.qtwayland
              # GStreamer backend for QtMultimedia (video playback in history)
              gst_all_1.gstreamer
              gst_all_1.gst-plugins-base
              gst_all_1.gst-plugins-good
              gst_all_1.gst-plugins-bad
              gst_all_1.gst-libav
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=${buildType}"
            ];

            postInstall = ''
              # GStreamer plugins live in <pkg>/lib/gstreamer-1.0 — they are
              # only discoverable when GST_PLUGIN_SYSTEM_PATH_1_0 points at
              # each plugin package. Needed for the portal recording path
              # (pipewiresrc, x264enc, mp4mux) and QtMultimedia playback.
              gstPluginPath="${pkgs.lib.makeSearchPath "lib/gstreamer-1.0" [
                pkgs.gst_all_1.gst-plugins-base
                pkgs.gst_all_1.gst-plugins-good
                pkgs.gst_all_1.gst-plugins-bad
                pkgs.gst_all_1.gst-plugins-ugly
                pkgs.gst_all_1.gst-libav
                pkgs.pipewire
              ]}"

              wrapProgram $out/bin/kartoza-screencaster \
                --prefix PATH : ${pkgs.lib.makeBinPath runtimeDeps} \
                --prefix GST_PLUGIN_SYSTEM_PATH_1_0 : "$gstPluginPath"
            '';

            meta = with pkgs.lib; {
              description = "Screen recording tool for Wayland with WYSIWYG canvas editor";
              homepage = "https://github.com/kartoza/kartoza-screencaster";
              license = licenses.mit;
              mainProgram = "kartoza-screencaster";
              platforms = platforms.linux;
            };
          };

      in
      {
        packages = {
          default = mkPackage {};
          kartoza-screencaster = self.packages.${system}.default;

          # Debug build (with symbols, no optimisation)
          debug = mkPackage { buildType = "Debug"; };

          # Jailed AI agents (sandboxed to project folder)
          claude-jailed = jailedClaude;
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
            gst_all_1.gstreamer
            gst_all_1.gst-plugins-base
            gst_all_1.gst-plugins-good
            gst_all_1.gst-plugins-bad
            gst_all_1.gst-libav
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

            # Audio synthesis (for creating intro/outro sound effects)
            yoshimi
            helm

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
          ] ++ devScripts;

          shellHook = ''
            export EDITOR=nvim

            # Suppress VDPAU nvidia probe (not available in nix env).
            # If you DO have an NVIDIA GPU with drivers installed, use instead:
            #   export VDPAU_DRIVER=nvidia
            #   export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/vdpau''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
            export VDPAU_DRIVER=va_gl

            # Qt multimedia needs its GStreamer backend plugin + GStreamer plugins.
            # Adds plugins-ugly (x264enc) and pipewire's GStreamer plugin
            # (pipewiresrc) so `cr` can run the portal recording pipeline
            # without the wrapProgram env that only the installed binary gets.
            export QT_PLUGIN_PATH="${pkgs.qt6.qtmultimedia}/${pkgs.qt6.qtbase.qtPluginPrefix}''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
            export GST_PLUGIN_SYSTEM_PATH_1_0="${pkgs.gst_all_1.gst-plugins-base}/lib/gstreamer-1.0:${pkgs.gst_all_1.gst-plugins-good}/lib/gstreamer-1.0:${pkgs.gst_all_1.gst-plugins-bad}/lib/gstreamer-1.0:${pkgs.gst_all_1.gst-plugins-ugly}/lib/gstreamer-1.0:${pkgs.gst_all_1.gst-libav}/lib/gstreamer-1.0:${pkgs.pipewire}/lib/gstreamer-1.0''${GST_PLUGIN_SYSTEM_PATH_1_0:+:$GST_PLUGIN_SYSTEM_PATH_1_0}"

            # Ensure build directory exists
            mkdir -p build

            # Symlink compile_commands.json for clangd
            [ -f build/compile_commands.json ] && ln -sf build/compile_commands.json compile_commands.json

            echo ""
            echo "🎬 Kartoza Screencaster Development Environment (C++/Qt6)"
            echo ""
            echo "Build commands:"
            echo "  cb   - Configure + build (Debug)"
            echo "  cbr  - Configure + build (Release, optimised+stripped)"
            echo "  ct   - Run all tests"
            echo "  cr   - Run the application"
            echo "  cclean - Clean rebuild from scratch"
            echo "  cf   - Format all C++ code (clang-format)"
            echo "  ctr  - Run merger tests + play renders for visual review"
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

          release-upload = {
            type = "app";
            program = toString (pkgs.writeShellScript "release-upload" ''
              #!/usr/bin/env bash
              TAG="$1"
              if [ -z "$TAG" ]; then
                echo "Usage: nix run .#release-upload -- vX.Y.Z"
                exit 1
              fi

              echo "Building release package..."
              nix build .#default
              BINARY="result/bin/kartoza-screencaster"

              # Create release tarball
              mkdir -p release
              tar -czf "release/kartoza-screencaster-linux-$(uname -m).tar.gz" -C result/bin kartoza-screencaster
              cd release && sha256sum *.tar.gz > checksums.txt && cd ..

              # Upload to GitHub
              gh release upload "$TAG" release/*.tar.gz release/checksums.txt --clobber
              echo "Release $TAG uploaded!"
            '');
          };

          # Jailed AI agent
          claude = {
            type = "app";
            program = "${jailedClaude}/bin/claude-code";
          };
        };
      }
    );
}
