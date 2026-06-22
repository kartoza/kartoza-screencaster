<!-- SPDX-FileCopyrightText: Tim Sutton -->
<!-- SPDX-License-Identifier: MIT -->

# Contributing

We're glad to have you. The contribution loop is straightforward.

## Workflow

1. **Open an issue first** with a user story, success criteria, and a
   job-size guess. The maintainers triage and tag.
2. Branch from `main` as `feature/<topic>` or `bugfix/<topic>`.
3. Implement, run tests, run `clang-format`.
4. Open a PR referencing the issue with GitHub's `fixes #N` syntax.
5. CI runs Linux / macOS / Windows builds + the test suite.
6. Maintainers review; address feedback; merge.

## Commit conventions

[Conventional Commits](https://www.conventionalcommits.org/):

```text
feat: add foo
fix: bar no longer crashes when baz is empty
docs: clarify portal session lifetime
chore: bump dependency X
refactor: extract Y helper from Z
test: add coverage for the W code path
```

The version bump on `main` is driven by commit prefixes: `feat` → minor,
`fix` → patch, breaking change → major.

## Code style

- C++ formatted with `clang-format`. The project file is in the repo
  root.
- No tabs.
- Headers use `#pragma once`.
- SPDX header on every source file: `// SPDX-FileCopyrightText:` +
  `// SPDX-License-Identifier:`.

## Where the spec lives

- `SPECIFICATION.md` — high-level requirements and user stories.
- `CHANGELOG.md` — the canonical record of what changed in each
  release.
- `docs/` — the user-facing documentation site (this site).
