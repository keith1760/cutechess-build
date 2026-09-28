# Fork notes

This is a fork of [Cute Chess](https://github.com/cutechess/cutechess) with a
few additions on top of upstream. This file describes what's different and
where to find it in the source; it replaces the set of dated patch files and
progress logs that used to accumulate here as each change was made.

## Live score / Elo readout

During an engine-vs-engine tournament with exactly two players (not a
knockout), the main window's status bar shows a live, auto-updating readout
each time a game finishes:

    White wins – Draws – Black wins: 5 – 3 – 2    Elo diff: +38.4

(shortened to `W wins – Draws – B wins: ...` if the full text doesn't fit.)
Win/draw/loss counts are colour-based; the Elo difference uses the existing
`Elo` class (`projects/lib/src/elo.h/.cpp`) fed with each engine's own
win/loss/draw record. The label is hidden for tournaments that aren't a
straight two-engine match, since a single Elo number isn't meaningful there.

The readout's font size is computed, not hard-coded, so that it always
matches the engine-name label's font exactly: see the `scorePx` block in
`CuteChessApplication::applyCustomAppearance()`
(`projects/gui/src/cutechessapp.cpp`).

Relevant code: `MainWindow::createStatusBar()`, `MainWindow::formatEloDiff()`
and `MainWindow::updateTournamentScore()` (`mainwindow.h`/`.cpp`), wired to
`Tournament::gameFinished()` in `MainWindow::newTournament()`.

## Custom appearance (mid-cream theme, larger headings)

`CuteChessApplication::applyCustomAppearance()`
(`projects/gui/src/cutechessapp.cpp`) applies a mid-cream palette (colour is
user-configurable from Settings > General) and renders headings/labels bold
and slightly larger than the platform default. See the comments in that
function for why this is done via `QPalette` rather than a stylesheet alone,
and how the heading size is derived.

## Window geometry and dock/tick-box persistence

The main window's position and each dock's ticked/visible state (View menu)
are saved on a clean shutdown and restored on the next launch, independent
of `QMainWindow`'s own combined `saveState()`/`restoreState()` blob. The
logic lives in `MainWindow` (`applySavedGeometry()`, `closeEvent()`,
`moveEvent()`/`resizeEvent()`, `eventFilter()`, `createDockWindows()`) and
`CuteChessApplication` (`installSignalHandlers()`/`handleUnixSignal()`,
`onAboutToQuit()`, `backupViewMenuState()`). The comments next to each
piece explain the specific Qt behaviour it's working around (e.g. why a
UNIX signal needs a self-pipe, why dock visibility is tracked separately
from `QDockWidget::isVisible()`, why a dock's own close button and
`QApplication::closeAllWindows()` need to be told apart).

## Book move display delay

The "Book move display delay" slider (New Tournament > tournament settings,
0-5000 ms, next to "Wait between games") paces out opening/book moves in
engine-engine games so they appear on the board one at a time instead of the
whole line flashing past in a single repaint. 0 (the default) means no delay,
i.e. the unchanged upstream behaviour. It never applies to games involving a
human player. The delay is display pacing only: it happens before the move is
handed to the players, so it is never charged to either engine's clock.

There are two places where "book" moves get played, and both are paced:

* `ChessGame::startGame()` -- the *forced opening* moves: the moves taken from
  the opening suite (PGN/EPD) and, when both sides use an opening book, the
  moves that `ChessGame::generateOpening()` pre-generated from the book. This
  is where nearly all book moves in a tournament come from. Each move is
  played by `ChessGame::playOpeningMove()`, scheduled one at a time via
  `scheduleOpeningMove()` / `continueOpening()`, and once the last one has been
  played `beginPlay()` connects the players' `moveMade` signals and starts the
  first real turn. `m_openingIndex` tracks progress (-1 = not in a paced
  opening); stale timers, pause/resume and stop()/finish are all handled (see
  the comments in `continueOpening()` and `ChessGame::resume()`).
* `ChessGame::startTurn()` -- book moves looked up during play (only reached
  when just one side has a book, or a book line continues past the forced
  moves), paced with a `QTimer::singleShot`.

`Tournament::setBookMoveDelay()` hands the value to every game it creates in
`Tournament::startNextGame()`. Note that this call must *not* be inside the
`if (m_finishedGameCount > 0)` that guards `setStartDelay()` -- the wait
between games only makes sense after a game has finished, but the book move
delay applies to the very first game too.

## Pause/Resume button (CutechessPauseButton25-09-2026)

A "Pause"/"Resume" button sits in the top right-hand corner of the main
window (a `QToolButton` set as the menu bar's corner widget -- see the end of
`MainWindow::createMenus()`, `projects/gui/src/mainwindow.cpp`). The same
action is also in the Game menu, with the `Ctrl+P` shortcut. It's plain
text, not an icon, so it doesn't depend on an icon theme being present.

It works for any game currently on screen, whether either side is a human
or an engine, and for both a standalone game (New Game / pasted FEN) and a
tournament/match:

* For a standalone game, it pauses/resumes just that one `ChessGame`.
* For a tournament tab, it pauses/resumes every currently-running game that
  belongs to that same tournament -- not just the tab that happens to be on
  screen -- since a tournament can have more than one game going at once
  when its concurrency setting is above 1. The tournament is also remembered
  as "paused" (`MainWindow::m_pausedTournaments`) so that a game which starts
  later in the same match (the next round, or a concurrency slot that frees
  up) starts paused too, instead of the pause only having applied to
  whichever games were already running at the moment the button was
  clicked. See `MainWindow::togglePauseResume()`, `pausableGames()`,
  `updatePauseResumeAction()` and the pause check added to `addGame()`.

The actual pause/resume mechanics were already implemented at the engine
level, in `ChessGame::pause()`/`resume()` (`projects/lib/src/chessgame.h/
.cpp`) as part of the book-move-delay work above -- they were just never
wired up to anything in the GUI. `pause()` sets a flag that
`ChessGame::startTurn()` and the opening-move pacing
(`ChessGame::continueOpening()`) check before scheduling the next move, so
it's naturally agnostic to whether either side is human or an engine: a
paused game simply doesn't get asked for (or schedule) its next move until
`resume()` is called, whatever kind of player is on move. `resume()` picks
back up wherever the game left off -- back in the middle of a paced opening
line if that's where it was paused, or the current turn otherwise.

### Pausing takes effect immediately, not after the move already in
### flight (CutechessPauseButton27-09-2026)

The flag alone isn't enough to pause *immediately*: if the side to move is
an engine, it was very likely already asked to move (`go()`) before the
button was clicked, and that request can't be un-asked. Originally, the
in-flight reply was simply applied to the board like any other move as
soon as it arrived -- `ChessGame::onMoveMade()` didn't look at `m_paused`
at all -- and only the *following* turn was skipped, in `startTurn()`. In
practice that meant clicking Pause always let one more move play out
before anything visibly stopped, which is one move too many when the
point of the button is to freeze the game the instant it's pressed.

`onMoveMade()` now checks `m_paused` before touching any state. If the
game is paused when a move comes in, it's held as-is (`m_moveHeld`,
`m_heldMoveSender`, `m_heldMove`) instead of being applied: the board,
clocks, PGN and the waiting player are left exactly where they were, so
nothing about the position or the display changes after Pause is
clicked, no matter how far along the outstanding engine search was.
The rest of the old `onMoveMade()` body -- updating the board, adjudicating,
informing the opponent, starting the next turn -- is now `applyMove()`, a
separate function so it can be called either from `onMoveMade()` directly
(not paused) or from `resume()` (a move was held). `resume()` checks for a
held move before falling back to its previous behaviour (continuing a
paced opening line, or starting the next turn), and applies it exactly as
if it had just arrived -- so nothing is lost, the held move simply lands
the moment you resume rather than the moment the engine happened to finish
thinking.

Two small additions were made to `ChessGame` to support the button:
`isPaused()` (a plain getter for the existing `m_paused` flag) and a new
`pausedChanged(bool)` signal, emitted by `pause()`/`resume()` only when the
state actually changes. The signal is what lets the button keep itself in
sync if a game's paused state changes on its own while its tab is being
viewed -- e.g. a brand new human-to-move game un-pausing itself the instant
the board is ready (see `beginPlay()`'s existing `wokeUp()`->`resume()`
connection, which predates this feature and is unrelated to it).

### Pause crash fix (CutechessPauseCrashFix28-09-2026)

Symptoms: pressing Pause during an engine-vs-engine tournament often made
engines crash or misbehave -- "Terminating process of engine ...",
"Illegal pv move from engine", "Premature bestmove while pondering", or an
engine seeming to resign early.

Cause: the previous `pause()` sent `stop` to *both* engines
(`ChessEngine::stopThinking()`) and `onMoveMade()` then *discarded* the
resulting `bestmove`. That desynchronises engine and GUI:

* `UciEngine` appends its own `bestmove` to its internal move list
  (`m_moveStrings`) *before* the game sees it. Discarding the move left the
  engine believing it had already played it, so on Resume the GUI sent a
  fresh `go` with no new `position`: the engine searched the wrong side to
  move (illegal PV moves, absurd scores, adjudicated "resignations",
  illegal bestmoves).
* Stopping the engine that was only *pondering* made it emit a
  "premature bestmove", which wiped the ponder state, so the following
  `ponderhit` went to an engine that was no longer pondering.
* A `stop` whose reply was swallowed left `m_idleTimer` running until it
  killed the engine ("Terminating process ...").

Fix (`projects/lib/src/chessgame.h/.cpp`): the engines are never touched
by Pause. `pause()` only sets the atomic `m_paused` flag. `onMoveMade()`
*holds* a move that arrives while paused (`m_moveHeld`, `m_heldMoveSender`,
`m_heldMove`) instead of applying or discarding it, and `resume()` applies
it exactly as if it had just arrived (then `startTurn()` carries on).
`startTurn()`/`continueOpening()` still refuse to start anything new while
paused. Clocks are charged when the engine's reply *arrives*, so paused
time is never billed to a player. A held move is dropped if the game
finishes while paused (`stop()`).

### Pause stops the thinking engine's clock at once (follow-up to the crash fix)

Symptom: with the crash fix alone, Pause froze the board but the engine on
move kept searching and its clock kept running until its own search ended
(seconds, at 5-minute games), so Pause did not feel immediate and that
engine was billed for the time.

Fix: `ChessGame::pause()` also queues `stopThinkingForPause()` on the game's
thread, which calls the new virtual `ChessPlayer::pauseThinking()`
(`ChessEngine` implementation in `chessengine.cpp`). It only acts on an
engine that is genuinely searching -- state Thinking, not being pinged, not
pondering -- and sends the protocol's stop (`stop` / `?`); if the engine is
not ready yet it retries every 50 ms for up to 2 s. The engine's reply is
NOT discarded: it arrives as a normal move, `onMoveMade()` holds it, and
`resume()` applies it, so engine and game stay in sync. The engine that is
only pondering is never touched. Note the held move comes from a search cut
short by Pause.

Tested (Qt 6.4.2, Stockfish, pondering on): real MainWindow Pause action at
300+0 -- clocks freeze at the click and stay frozen, no warnings; 344
random 20-370 ms pause/resume cycles over 3 concurrent games -- all games
completed, no warnings, no move applied while paused.

### Pause also stops a pondering engine (CutechessPauseCPU28-09-2026)

Symptom: with "Thinking on opponent's time" (pondering) enabled, Pause froze
the board and stopped the engine that was on move, but the *other* engine
kept pondering at full CPU for the whole pause (measured: one Stockfish
process pegged at 100% while paused), so the cores stayed hot.

Fix: `ChessGame::pause()` now also queues `stopPonderingForPause()`, which
calls the new virtual `ChessPlayer::pausePondering()` on the player that is
NOT on move. `UciEngine::pausePondering()` treats it exactly like a ponder
miss (the same steps `UciEngine::makeMove()` takes): it forgets the
speculative ponder move, sends `stop`, and ignores the resulting
`bestmove` via `m_ignoreThinking`. On Resume the held move is applied as
before and the engine is sent the real position like after any ponder miss,
so engine and game stay in sync. Only UCI engines are handled; XBoard
engines (which ponder on their own in "hard" mode) are not touched.

Not changed: the engine that was on move when Pause was pressed is still
stopped with `stop` and its (cut short) move is applied when Resume is
pressed; UCI has no way to suspend and later continue a search.

Tested (Qt 6.4.2, Stockfish, cutechess-cli with a pause/resume hook):
per-second CPU of both engines is 0% for the whole pause with pondering on
(previously one engine stayed at 100%); repeated random pause/resume cycles
(20-520 ms apart) over 2 concurrent games, ponder on and off, all games
completed with no warnings.

## Removed

An earlier build of the AppImage also bundled RyzenAdj, an unrelated
root-privileged CPU power-management utility, wired up via an undocumented
`AppRun` argument. It had nothing to do with this application and has been
removed; nothing in this source tree references it.
