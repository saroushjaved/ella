# Contributing to ELLA

Thanks for your interest in improving ELLA. Contributions of all sizes are welcome.

## Before You Start

- Read the setup guide: [docs/developer-setup-windows.md](docs/developer-setup-windows.md)
- Search existing issues/PRs to avoid duplicate work
- For larger changes, open an issue first so we can align early

## Development Workflow

1. Fork the repository and create a feature branch from `main`
2. Set up your local environment
3. Implement and test your changes
4. Open a pull request with clear context

Example branch names:

- `feat/search-evidence-label`
- `fix/indexing-timeout`
- `docs/windows-setup-update`

## Build and Test Requirements

Before opening a PR, run locally:

```powershell
cmake -S . -B build-dev -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dev --target appSecondBrain ellaMemoryBrowserTests --config Debug -j 4
ctest --test-dir build-dev --output-on-failure -C Debug
```

PRs should keep existing tests green and add/update tests for behavior changes when possible.

## Pull Request Expectations

- Use the PR template
- Keep PR scope focused
- Describe:
  - what changed
  - why it changed
  - how it was tested
- Link related issues when applicable

Required for merge:

- Passing `Windows CI` checks
- At least one maintainer approval

## Coding Guidelines

- C++17 and Qt/QML conventions used in this repository
- Prefer clear, small, and reviewable changes
- Keep comments concise and focused on non-obvious behavior
- Avoid unrelated refactors in the same PR

## Reporting Bugs and Requesting Features

- Use issue templates in `.github/ISSUE_TEMPLATE/`
- Include clear repro steps, expected behavior, and actual behavior

## Community

By participating, you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).
