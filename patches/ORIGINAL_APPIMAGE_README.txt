CutechessEngineInstallFixed
============================

This AppImage bundles:
  - cutechess          (GUI, built with the resilient-detection patch)
  - cutechess-cli       (command-line tournament manager)
  - berserk-avx2-linux  (a sample UCI engine binary, AVX2 build)
  - The full patched Cutechess source code + the resilient-detection
    patch files, under usr/share/cutechess/src/

USAGE
-----
  ./CutechessEngineInstallFixed                  Launch the GUI
  ./CutechessEngineInstallFixed --cli [args...]  Run cutechess-cli
  ./CutechessEngineInstallFixed --extract-source [dir]
                                                  Copy the source tarball
                                                  and patches to [dir]
                                                  (default: ./cutechess-source)
  ./CutechessEngineInstallFixed --help-launcher   Show this usage text

You can also get at everything inside without running it at all:
  ./CutechessEngineInstallFixed --appimage-extract
This unpacks the whole AppImage (including usr/share/cutechess/src and
the berserk engine) into a squashfs-root/ folder next to the AppImage.

PORTABILITY NOTES
------------------
This AppImage bundles Qt 6 and all of its non-base runtime dependencies,
so it does not require Qt to be installed on the host system. It relies
on the host for only the lowest-level pieces every Linux distribution
ships (the dynamic linker, glibc, and the kernel's X11/graphics stack),
which is the standard/most-compatible approach for AppImages.

One real limitation comes from the pre-built cutechess GUI binary itself,
not from the packaging: it was compiled against a very recent glibc and
requires glibc >= 2.38 (roughly Ubuntu 23.10/24.04+, Fedora 38+, or other
distros released in 2023-2024 or later). cutechess-cli and the bundled
berserk engine only require glibc >= 2.29 (Ubuntu 19.04+/20.04+, Debian
10+, RHEL/CentOS 8+), so they run on a much wider range of systems.

If you need the GUI to run on an older distribution, the fix has to
happen at compile time (a different glibc means a different build), not
at packaging time. That's why the full source and the resilient-detection
patch are included in this AppImage - so you (or anyone else) can
recompile cutechess on an older base system if needed. See
usr/share/cutechess/src/ for the tarball and patch files.

FILES
-----
  usr/bin/cutechess
  usr/bin/cutechess-cli
  usr/share/cutechess/engines/berserk-avx2-linux
  usr/share/cutechess/src/Cutechess-with-pgn-source-with-resilient-detection.tar.gz
  usr/share/cutechess/src/cutechess-resilient-detection.patch
  usr/share/cutechess/src/resilient-detection-applied.patch
