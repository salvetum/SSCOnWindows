# Contributing to SSC On Windows

Thank you for your interest in improving **SSC On Windows** — a fork of
[A2DP Windows Bridge](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge) that
adds Samsung SSC streaming.

## Bug Reports & Feature Requests

This is a fork; the recommended place for fork-specific issues is this
repository's issue tracker. For anything that is clearly upstream
(A2DP Windows Bridge core), the
[upstream issues](https://github.com/SeiyaFunaokaJP/A2DP-Windows-Bridge/issues)
may also be relevant.

- **Bug reports**: include steps to reproduce, expected vs. actual behavior, and
  the relevant stderr/diagnostic output (`CAP:`, `SSC:`, `WasapiCapture: CLK`,
  `BTstack: CTX`, `CB:`).
- **Feature requests**: describe the feature and why it is useful.

## Building from Source

See [docs/building.md](docs/building.md) and the [README](README.md) for full
instructions. Quick start:

**Requirements:**

- Windows 10/11 (x64)
- Visual Studio 2022 or later with the "Desktop development with C++" workload
- CMake 3.16+ (with CMake 4.x, pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`)
- Git
- For SSC streaming: WSL2 (default) or Python 3.14 + Qiling (native)

**Build the core / CLI:**

```powershell
git clone --recursive <this-repo-url> SSCOnWindows
cmake -S SSCOnWindows -B SSCOnWindows\build_msvc -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build SSCOnWindows\build_msvc --config Release --target A2DPWB -j 8
```

**Apply the submodule patch after cloning:**

```powershell
git -C extern/btstack apply ..\..\patches\btstack-win-usb-logs.patch
```

**Build the WinUI GUI:** see [docs/building.md](docs/building.md).

## Pull Requests

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Keep the codec fallback priority (SSC > AAC > SBC) and the SSC int32 scale
   (2^29) consistent between the CLI and the service backend
4. Commit your changes (`git commit -m "Add my feature"`)
5. Push and open a pull request

## Versioning (SemVer)

This project follows [Semantic Versioning](https://semver.org/); while on the
`0.x` line, the assignment rules are:

- **Patch (0.x.N)** — bug fixes, internal refactors, docs-only changes.
- **Minor (0.N.0)** — new/backward-compatible features (codec modes, GUI
  features, tests, new daemon capabilities).
- **Major (1.0.0 and later)** — breaking changes, per standard SemVer.

Rules for contributors:
- Every user-visible change belongs in `CHANGELOG.md` (Keep a Changelog
  format) — new work goes under `[Unreleased]` and is folded into the version
  header at release time.
- Do **not** bump the version in a feature PR; the maintainer bumps
  (`project(VERSION)` in `CMakeLists.txt`, WinUI `APP_VERSION` +
  `app.manifest`, the two wx `APP_VERSION` defines, and the names in
  `README.md` / `docs/` / `release.yml`) when cutting a release.
- Update `docs/compatibility.md` when you add tested hardware, and
  `docs/architecture.md` when threads, time budgets or failure handling change.

## Proprietary binaries

Do **not** add proprietary or downloaded binaries to this repository:

- Samsung SSC blobs (`libScalable_Encoder.so`, `libScalable_Decoder.so`,
  `lib_bt_bundle.so`)
- any `*.so`, `*.fw`, `*.bin`, `*.psr` binary artifact
- Realtek / CSR dongle firmware (`rtl8761*`, `rtl8763*`, `csr*`)

The repo currently tracks a proprietary Samsung encoder blob for the interim
release; it is scheduled for removal. Do **not** update, re-add, or commit new
copies of it. `tools/check_proprietary.py` rejects such additions:
- via the pre-commit hook (install it once locally):
  ```powershell
  git config core.hooksPath tools/githooks
  ```
- and in CI on PRs.

Deletions are always allowed, so removing the bundled blob later is fine. See
[`docs/dev/audit-2026.md`](docs/dev/audit-2026.md) for the full license / blob
inventory. If a proprietary file slips in, open an issue rather than silently
removing history.

## License

Contributions are provided under the [MIT License](LICENSE).
