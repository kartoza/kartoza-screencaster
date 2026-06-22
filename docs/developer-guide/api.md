<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# C++ API reference

The full Doxygen-generated API reference lives at
[**`/api/`**](../api/){ target=_self } — every class, struct, free
function and macro the C++ codebase declares, with cross-linked
include / inheritance / call graphs.

<div class="kz-cta" markdown>
[:material-book-open-variant: Open API Reference](../api/){ .kz-cta__primary target=_self }
[:material-source-branch: src/ on GitHub](https://github.com/kartoza/kartoza-screencaster/tree/main/src){ .kz-cta__secondary }
</div>

!!! tip "Previewing locally?"
    `mkdocs serve` / `nix run .#docs-serve` only renders the user
    docs — clicking the link above shows a placeholder. To browse the
    real API output locally, run
    **`nix run .#docs-full-serve`** instead and reload. (Same flake
    app, but it generates Doxygen, merges it in, and static-serves
    the combined site on the right path.)

## When to use this

Reach for the API reference when you are:

- **Adding a feature** that talks to an existing module (Recorder,
  Portal, Canvas, Merger, …). The Doxygen pages show every public
  method's signature, parameters, and the brief comment from the
  header.
- **Tracing a bug** through cross-module calls. Use the call graphs
  (each function page links into and out of it) to follow data flow.
- **Writing tests**. The class summaries highlight which methods are
  `public slots` vs internal helpers — useful when wiring a test
  harness.

## When to use the source instead

The Architecture page in this guide ([Architecture](architecture.md))
is the right starting point for *why* a module exists; the API
reference is for *what* it exposes. If you want to see how the
modules talk to each other at runtime, read
[Capture pipelines](capture-pipelines.md) first.

## How it stays current

The Doxygen output is regenerated on every push to `main` by the
[`📚 Docs`](https://github.com/kartoza/kartoza-screencaster/actions/workflows/Docs.yml)
workflow, copied into `site/api/`, and published as part of the same
GitHub Pages deploy as the rest of this site. Locally:

```bash
nix run .#docs-full-serve   # builds + serves the combined site (the link above works)
nix run .#docs-full-build   # builds doxygen + mkdocs and merges into ./site
nix run .#docs-doxygen      # only builds doxygen into ./build/doxygen
```
