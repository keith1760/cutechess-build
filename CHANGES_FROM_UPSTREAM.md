# What's different in the patched Linux source vs. stock upstream

Base compared against: upstream `cutechess/cutechess` git history, commit
`5e84232b` — 5 commits past the official `v1.5.1` tag (the tag itself is
NOT what your source is based on; three commits landed after the tag,
including one that removes the old Windows-only `engineprocess_win.*`
files in favor of Qt's `QProcess` everywhere — that's why those files are
absent from your upload, not a change you made). `.version` reads "1.5.1"
at both the tag and at this later commit, since it hadn't been bumped for
the next dev cycle yet.

22 files changed, ~1,500 lines added, 3 new files. Grouped by feature:

## 1. User-configurable board colours
`boardview/graphicsboard.{cpp,h}`, `boardview/boardview.{cpp,h}`,
`boardview/boardscene.cpp`, `settingsdlg.{cpp,h,ui}`, `cutechessapp.{cpp,h}`
Light/dark square colour and the board's background ("wall") colour, 
previously hard-coded, are now stored in QSettings and editable from a new
"Board colours" section on the Settings dialog's General tab, applied live.

## 2. Live evaluation bar
`evalbar.cpp` / `evalbar.h` (new files)
A slim vertical bar next to the board showing which side is ahead, filled
proportionally to White's evaluation. Deliberately only updates once a move
is finalized (via `ChessGame::scoreChanged`), not on every engine "info"
line, so it doesn't flicker mid-search.

## 3. Match history recording to PGN
`gamehistoryrecorder.cpp` / `gamehistoryrecorder.h` (new files),
`newtournamentdialog.{cpp,h,ui}`, `gameviewer.{cpp,h}`, `gamedatabasedlg.cpp`
Every finished game is appended to a rolling PGN file (500-game cap,
oldest dropped first) — separate default files for engine-vs-engine and
human games, or a single file the user picks in the New Tournament dialog's
new "Match history" field. Adds non-standard `Tournament` and `EloDiff`
PGN tags.

## 4. Live tournament score / Elo diff in the status bar
`mainwindow.{cpp,h}`, `tournamentresultsdlg.cpp`
For a straight two-engine match (not knockout/round-robin/gauntlet), the
main window's status bar shows a running "White wins – Draws – Black wins"
line plus the Elo difference, updating after every finished game, using
the existing `Elo` class.

## 5. Dialog geometry persistence
`dialoggeometry.h` (new file)
Small helper so dialogs remember their last size/position across restarts,
the same way the main window already does.

## 6a. Bugfix: black-on-black menus
`cutechessapp.cpp` (`applyCustomAppearance()`)
Fixes menus (the main menu bar and right-click context menus) sometimes
rendering as unreadable dark/black text on a dark/black background.

Root cause: `applyCustomAppearance()` (added by the "User-configurable
board colours" patch above, item 1) applies a blanket
`QWidget { color: #202020; }` rule via `QApplication::setStyleSheet()`
so that every widget's text stays dark regardless of the platform theme.
That rule reaches `QMenu`/`QMenuBar` items too, but nothing forced their
*background*. Popup menus are frequently painted by the native platform
style rather than sourced from `QPalette::Window` (true of native
Windows dark mode and of several Linux/GTK platform themes), so on a
dark-themed desktop a menu could end up with a native black background
underneath text that this stylesheet had forced to near-black -- i.e.
invisible menu items.

Fix: give `QMenu`/`QMenuBar` (and their item/selected-item/separator
states) explicit `background-color` and `color` rules in the same
stylesheet, so their appearance no longer depends on native popup
painting or the platform palette at all.

## 6. Misc
`chessclock.cpp` — minor clock-display tweaks accompanying the above.
`CMakeLists.txt` / `.gitignore` — registers the 2 new .cpp files for the
build, ignores the local `build/` directory.

See `patched-vs-unpatched-linux.diff` for the full unified diff.
