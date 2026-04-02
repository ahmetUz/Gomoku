# Gomoku — Ninuki-renju AI

A high-performance Gomoku engine written in C++17, featuring a multi-threaded AI with advanced search algorithms and both a terminal (CLI) and graphical (SFML) interface.

## Rules

The game follows **Ninuki-renju** (Pente-style) rules on a 19x19 board:

- **Win by alignment**: 5 stones in a row (must be unbreakable by capture)
- **Win by capture**: capture 5 pairs (10 opponent stones)
- **Pair capture**: surround exactly 2 opponent stones between your stones (`XOO.` + place at `.` = capture)
- **Double-three forbidden**: Black cannot play a move that simultaneously creates two open threes

## Features

- **AI search pipeline** with priority-based move selection:
  1. Opening book (first 1-3 moves)
  2. Break opponent's five / immediate win detection
  3. VCF (Victory by Continuous Fours) — forced win search
  4. VCF defense (block opponent's forced win)
  5. Alpha-beta search with iterative deepening
- **Lazy SMP** parallel search (auto-scales to available cores, up to 8)
- **Transposition table** (lock-free atomic TT, 16 MB default)
- **Search optimizations**: aspiration windows, PVS, LMR, null move pruning, reverse futility, razoring, futility pruning, late move pruning, threat extensions, IID, quiescence search
- **Bitboard representation** (6 x `uint64_t` for the 19x19 board)
- **GUI** with SFML: interactive board, undo, hints, AI stats display
- **CLI** with ASCII board, standard notation (A1-T19), undo, hints

## Build

Requires **CMake 3.14+** and a **C++17** compiler.

```bash
# Build everything (CLI + GUI) in release mode
make

# Build only the GUI
make gui

# Build in debug mode
make debug

# Run tests
make test

# Clean build
make clean
```

The GUI requires SFML 2.5+. If not found on the system, it is automatically fetched via CMake's `FetchContent`.

## Usage

### GUI

```bash
./build/gomoku_gui
```

- **Click** on the board to place a stone
- **U** — undo last move
- **H** — show AI hint
- **N** — new game
- **Esc** — return to menu

### CLI

```bash
./build/gomoku
```

- Enter moves in standard notation: `J10`, `A1`, `T19` (columns A-H, J-T; letter I is skipped)
- Commands: `undo`, `hint`, `new`, `mode`, `help`, `quit`

## Project structure

```
src/
  board/       Bitboard and board representation
  rules/       Capture, win detection, forbidden move (double-three)
  eval/        Pattern scoring, heuristic evaluation
  search/      Zobrist hashing, transposition table, VCF/VCT threat search, alpha-beta
  engine/      AI engine (search pipeline orchestration)
  gui/         SFML graphical interface (board renderer, UI panel, game controller)
  main.cpp     Terminal interface

include/gomoku/
  board/       types.hpp, board.hpp, bitboard.hpp
  rules/       capture.hpp, win.hpp, forbidden.hpp
  eval/        patterns.hpp, heuristic.hpp
  search/      zobrist.hpp, tt.hpp, threat.hpp, alphabeta.hpp
  engine/      engine.hpp
  gui/         gui_constants.hpp, board_renderer.hpp, ui_panel.hpp, game_controller.hpp

tests/         Unit tests (Catch2)
bench/         Benchmark
documentation/ Technical documentation per module
assets/fonts/  DejaVuSans.ttf (GUI font)
```

## Tests

Tests use [Catch2](https://github.com/catchorg/Catch2) (auto-fetched by CMake).

```bash
make test
```

193 test cases covering board operations, captures, win detection, forbidden moves, pattern evaluation, Zobrist hashing, transposition table, threat search, and the full engine pipeline.
