# EloNow — live W/D/B score + Elo diff

## Status: Code-complete, manually verified against the codebase; still not compiler-verified

Qt6 dev headers, CMake, and network access are all unavailable in this
sandbox (same blocker as before — `apt-get install cmake`/`qt6-base-dev`
fails with `403 Forbidden`, and no `Qt6*` headers exist anywhere on disk),
so an actual `cmake --build` still could not be run here. In place of that,
every line the feature touches was checked by hand against the rest of the
codebase:

- `MainWindow::updateTournamentScore()`'s slot signature
  (`ChessGame*, int, int, int`) matches `Tournament::gameFinished()`'s
  signal exactly (`tournament.h`).
- `TournamentPlayer::whiteWins()/blackWins()/draws()/wins()/losses()` all
  exist with the expected signatures (`tournamentplayer.h`).
- `Elo(int wins, int losses, int draws)` / `Elo::diff()` match `elo.h`.
- The `Elo elo(fcp.wins(), fcp.losses(), fcp.draws())` pattern — using only
  the first player's own record for a two-player match — mirrors the
  existing, already-shipped calculation in `Tournament::results()`
  (`tournament.cpp`, around the `playerCount() == 2` branch), so the new
  code isn't introducing a novel (and unverified) statistical approach.
- The `type() == "knockout"` guard matches the literal string
  `KnockoutTournament::type()` actually returns (`knockouttournament.cpp`).
- All headers the new code needs (`<QLabel>`, `<QStatusBar>`,
  `<QFontMetrics>`, `<cmath>`, `tournamentplayer.h`, `elo.h`) are already
  included in `mainwindow.cpp`.
- `createStatusBar()` runs in the constructor before anything can touch
  `m_tournamentScoreLabel`, so there's no use-before-init path.

No logic errors, signature mismatches, or missing includes were found. The
one thing this review can't do that a real compiler could is catch a stray
typo in a name that happens to resolve to something else, or a template/
overload-resolution problem — normal compiler-only failure modes. If you
can build with Qt6 + CMake, a `cmake --build .` (or opening
`CMakeLists.txt` in Qt Creator) is the remaining step to get a green build
and a binary/AppImage.

## What the feature does
During an engine-vs-engine tournament with exactly two players (not a
knockout), the main GUI window's status bar now shows a live, auto-updating
readout every time a game finishes:

    White wins – Draws – Black wins: 5 – 3 – 2    Elo diff: +38.4

- Win/draw/loss counts are colour-based (summed across both engines'
  `whiteWins()`/`blackWins()`/`draws()`), matching the wording you asked for.
- The Elo difference is computed with the existing `Elo` class
  (`projects/lib/src/elo.h/.cpp`), fed with each engine's own win/loss/draw
  record — that's the statistically meaningful input (colour totals alone
  can't tell you which *engine* is ahead, since both engines play both
  colours over the match).
- If the full line doesn't fit the status bar width, it automatically falls
  back to the shortened form:

    W wins – Draws – B wins: 5 – 3 – 2    Elo diff: +38.4

- The label stays hidden for tournaments that aren't a straight two-engine
  match (round robins, knockouts, gauntlets with >2 players), since a single
  Elo number isn't meaningful there.

## Files touched
- `projects/gui/src/mainwindow.h` — new `m_tournamentScoreLabel` member,
  `createStatusBar()`, `formatEloDiff()`, and the `updateTournamentScore()`
  slot declarations.
- `projects/gui/src/mainwindow.cpp` — status bar creation, the new slot's
  implementation, and wiring it to `Tournament::gameFinished()` alongside
  the existing Results-dialog and PGN-saving connections in `newTournament()`.

No other files were changed; the existing `blackbar-fix.diff` /
`cutechess-source` board-colour feature from the previous package is
untouched and still present in this tree.

## Known open item
The sandbox initially had no Qt6/CMake toolchain, so the change was written
and reviewed carefully against the existing codebase (signal signatures,
`TournamentPlayer`/`Elo` APIs, the pre-existing "quick fix" in
`tournamentresultsdlg.cpp` that this mirrors) but not yet compiled. Qt6 dev
packages and CMake have since been installed and a build attempt is in
progress — a follow-up package will include the compiled AppImage/binary
once that's confirmed green.

## 2026-09-12 update: build succeeded, and the score/Elo label's font size fix

Qt6 (`qt6-base-dev`, `qt6-svg-dev`) and `cmake` installed cleanly this
session (`archive.ubuntu.com`/`security.ubuntu.com` were reachable), so the
tree above was actually compiled with `cmake --build .` for the first time.
It built clean with no errors, confirming the review notes above.

Separately, the live W/D/B + Elo-diff label above the board (`scoreLabel`,
`GameViewer::scoreLabel()`/`MainWindow::updateTournamentScore()`) was
reported as visually unchanged in size across earlier packages, despite
`CuteChessApplication::applyCustomAppearance()` in
`projects/gui/src/cutechessapp.cpp` already giving it its own
`QLabel#scoreLabel` font-size rule (`basePx + 3`, versus `basePx` for a
plain `QLabel`). That rule was verified to be live in the compiled binary
(present in `strings` output of the previous AppImage's `cutechess`
executable), so the mechanism was working — a 3px bump over body text is
just too small a jump to read as intentional at a glance.

Fix: in the same spot, `scorePx` is now `basePx + 12` instead of
`basePx + 3`, so the readout is clearly larger than the surrounding clock
labels rather than only marginally so. No other logic changed.

```
-int scorePx = basePx + 3;
+int scorePx = basePx + 12;
```

This was compiled and the resulting `cutechess` binary was smoke-tested
(`--version`, and launching under `QT_QPA_PLATFORM=offscreen` against the
AppImage's bundled Qt 6.4.2 libraries) before being packaged into the
AppImage, so this is a compiled, run-tested build — not just a source-level
review like the note above.

## 2026-09-12 follow-up: scale the score/Elo label back down by 2px

The `basePx + 12` bump above made the live W-D-L / Elo-diff readout larger
than intended. `scorePx` in `CuteChessApplication::applyCustomAppearance()`
(`projects/gui/src/cutechessapp.cpp`) is now `basePx + 8`, i.e. 2px smaller
than the previous patch produced, while still staying well above the
`basePx + 3` starting point so the readout reads as deliberately larger than
surrounding labels.

```
-int scorePx = basePx + 12;
+int scorePx = basePx + 8;
```

No other logic changed. Same build/packaging process as above.

## 2026-09-12 17:00 update: match the score/Elo font to the engine-name font exactly

Previous passes (`basePx + 3`, `+12`, `+8`, then a hard-coded `10px`) were all
guesses at a pixel size for the `QLabel#scoreLabel` rule. This pass instead
makes the win-draw-loss/Elo readout match the engine-name font *exactly*,
as requested.

The engine names in engine-engine matches are drawn by
`ChessClock::setPlayerName()` (`chessclock.cpp`) as `<h3>name</h3>` rich
text inside `m_nameLabel`, a plain `QLabel` with no `objectName`, so it
picks up the generic `QLabel { font-size: headingPx }` rule from
`CuteChessApplication::applyCustomAppearance()`
(`cutechessapp.cpp`) and Qt's rich-text engine then scales that up again
for the `<h3>` tag by an amount Qt does not expose as a fixed ratio.

`scorePx` is now computed, not guessed: a throwaway `QTextDocument` is
built with the identical base font and `<h3>` markup the engine-name label
uses, and the pixel size Qt actually resolves that text run to is read
back and used directly for the `QLabel#scoreLabel` rule. Every
win-draw-loss/Elo display in the app -- the live W-D-L/Elo-diff readout
between the clocks (`GameViewer::m_scoreLabel`, `MainWindow::
updateTournamentScore()`) -- routes through this one `QLabel#scoreLabel`
rule, so this one change covers all of them, matching the single-rule
comment already in the code.

```
-int scorePx = 10;
+int scorePx;
+{
+       QFont headingFont = font();
+       headingFont.setPixelSize(headingPx);
+       headingFont.setBold(true);
+
+       QTextDocument doc;
+       doc.setDefaultFont(headingFont);
+       doc.setHtml(QStringLiteral("<h3>Engine Name</h3>"));
+
+       QTextCursor cursor(&doc);
+       cursor.movePosition(QTextCursor::Start);
+       cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
+       QTextCharFormat fmt = cursor.charFormat();
+
+       QFontInfo resolvedInfo(fmt.font());
+       scorePx = resolvedInfo.pixelSize();
+       ...
+}
```

Compiled clean with `cmake --build .` against Qt 6.4.2 (`qt6-base-dev`,
`qt6-svg-dev`, `qt6-multimedia-dev`), and the resulting `cutechess` binary
was smoke-tested with `--version` under `QT_QPA_PLATFORM=offscreen` before
being packaged into the AppImage.
