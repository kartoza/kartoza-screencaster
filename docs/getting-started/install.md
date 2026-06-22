<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Install

!!! info "Walkthrough pending"
    This page will be expanded with platform-specific install steps and
    screenshots from the walkthrough recording. The list below covers
    each supported install method.

## Debian / Ubuntu

```bash
# Download the latest .deb from the release page, then:
sudo apt install ./kartoza-screencaster_<version>_amd64.deb
```

## Fedora / RHEL

```bash
sudo dnf install ./kartoza-screencaster-<version>-1.x86_64.rpm
```

## Linux tarball

A distribution-neutral build for systems where the `.deb` or `.rpm`
isn't a fit. Unpack and install:

```bash
tar -xzf kartoza-screencaster-linux-x86_64.tar.gz
sudo install -m 755 kartoza-screencaster /usr/local/bin/
```

If anything's missing on your system, the app will tell you on first
launch. (System administrators: the full dependency list lives in the
[Administrator Guide → System dependencies](../admin-guide/dependencies.md).)

## macOS

```bash
tar -xzf kartoza-screencaster-macos-arm64.tar.gz
mv kartoza-screencaster /Applications/
```

## Windows

Unzip `kartoza-screencaster-windows-x86_64.zip` and run
`kartoza-screencaster.exe`. Everything the app needs is bundled in
the zip.

## Nix flake

```bash
nix run github:kartoza/kartoza-screencaster
```

See the [Developer guide → Dev shell](../developer-guide/dev-shell.md)
for the full reproducible toolchain.
