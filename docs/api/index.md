---
hide:
  - navigation
  - toc
---
<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->
<!--
This file is a build-time placeholder so internal links to ../api/
resolve under `mkdocs build --strict`. When `nix run .#docs-full-build`
or the Docs.yml CI workflow runs, `site/api/` is removed and replaced
with the Doxygen-generated HTML. Don't link to it from the nav.
-->

# C++ API reference (placeholder)

You are seeing this page because the site was built with
`mkdocs serve` or `nix run .#docs-serve` — neither command includes
the Doxygen-generated C++ API reference. The deployed site at
[kartoza.github.io/kartoza-screencaster/api/](https://kartoza.github.io/kartoza-screencaster/api/)
always carries the live one.

## See it locally

Run the combined build-and-serve and the real Doxygen browser will
replace this page:

```bash
nix run .#docs-full-serve
```

That command runs MkDocs and Doxygen, merges the output, and serves
the result at <http://127.0.0.1:8000/kartoza-screencaster/>. Reload
this URL and the placeholder is gone — every class, struct, free
function and macro the C++ codebase declares is browsable, with
include / inheritance / call graphs.

If you only want the files (no server), run `nix run
.#docs-full-build` and open `site/api/index.html` directly.

[:octicons-arrow-left-24: Back to the Developer Guide](../developer-guide/api.md)
