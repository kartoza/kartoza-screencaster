<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Packaging

Every release tag publishes the same artefacts. The contents and
naming are stable, so you can pin to a version in your provisioning
tools.

| Artefact                                            | Format    | Target                          |
| --------------------------------------------------- | --------- | ------------------------------- |
| `kartoza-screencaster_<ver>_amd64.deb`              | `.deb`    | Ubuntu 24.04+, Debian 12+       |
| `kartoza-screencaster-<ver>-1.x86_64.rpm`           | `.rpm`    | Fedora 39+, RHEL 9+             |
| `kartoza-screencaster-linux-x86_64.tar.gz`          | `.tar.gz` | Distribution-neutral Linux      |
| `kartoza-screencaster-macos-arm64.tar.gz`           | `.tar.gz` | macOS 12+ on Apple Silicon      |
| `kartoza-screencaster-windows-x86_64.zip`           | `.zip`    | Windows 10/11 on x86_64         |
| `checksums.txt`                                     | text      | SHA-256 of every other artefact |

## Verifying integrity

```bash
sha256sum -c checksums.txt
```

Always cross-check the `checksums.txt` against the release page before
deploying to a fleet.

## SBOM

A CycloneDX SBOM lives alongside each release artefact. The intent is
to enumerate every linked library / runtime dependency for licence
audit and supply-chain review.
