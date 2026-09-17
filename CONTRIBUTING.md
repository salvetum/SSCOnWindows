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

## License

Contributions are provided under the [MIT License](LICENSE).
