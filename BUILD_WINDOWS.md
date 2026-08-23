# Building this source on Windows

This is the same cross-platform CMake project cutechess-org uses for every
platform — there's no separate "Windows source tree"; Windows, Linux and
macOS all build from these same files, with CMake switching in the
Windows-specific bits (see engineprocess.cpp / WIN32 guards) automatically.

This copy = upstream cutechess @ commit 5e84232b (5 commits past the
v1.5.1 tag, `.version` still reads "1.5.1") + the custom patch described
in CHANGES_FROM_UPSTREAM.md, applied cleanly with zero fuzz.

## Steps (matches the project's own official Windows CI exactly)

1. Install **Qt 6.8.3** (online installer from qt.io, MSVC 2022 64-bit component).
2. Install **Visual Studio 2022** (Desktop development with C++ workload) and **CMake**.
3. From a "x64 Native Tools Command Prompt for VS 2022":
   ```
   cmake -S . -B build -DWITH_TESTS=OFF
   cmake --build build --config Release
   ```
4. The binaries land at `build\Release\cutechess.exe` and `build\Release\cutechess-cli.exe`.
5. To reproduce the exact zip/installer the project ships, copy the Qt6
   DLLs (Core/Gui/Widgets/Svg/PrintSupport/Concurrent), the `platforms\qwindows.dll`
   plugin, and the MSVC redistributable next to the exe — see
   `.github/workflows/release.yml` in this tree for the exact file list,
   or just run `windeployqt cutechess.exe`.

## Building it for free without a Windows machine

This repo already contains a working GitHub Actions workflow
(`.github/workflows/release.yml`) that does exactly the above on a
GitHub-hosted Windows runner. Push this tree to a GitHub repo/fork and
push a tag matching `v*` (e.g. `git tag v1.5.1-patched && git push --tags`)
and GitHub will build `cutechess.exe` for you automatically, no local
Windows install needed.
