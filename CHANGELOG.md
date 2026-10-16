# Changelog

`MAJOR.MINOR` is the Ripple Effect engine contract these bindings bind, and
`PATCH` is their own. Each version pins one engine release, named here.

## 1.5.2

- **iOS**, pinning the same engine 1.5.3: this repository's CMake path for
  `aarch64-apple-ios` and `aarch64-apple-ios-sim` — the triple, the static
  import, and the system and framework libraries a real link needs — which no
  release had run before. The device leg links the example players; the
  simulator leg runs the whole suite on a booted simulator through
  `simctl spawn booted`. Static only, as the engine ships no shared iOS
  library. `docs/consuming.md` has the rest.
- **The `ios-arm64` and `ios-simulator` presets**, and a CI leg on macOS over
  both — the device cross-build and the suite on a simulator.

## 1.5.1

- **The web, through Emscripten**, pinning engine 1.5.3: a program compiled
  to the browser — a C++ engine, Unity WebGL — links the engine's static
  library for `wasm32-unknown-emscripten`, which the configure fetches under
  Emscripten's toolchain. Emscripten 4.0 or later, single-threaded, no
  exception flag asked of the host; Unity 6000.5 and later, not 6000.0 to
  6000.4. `docs/consuming.md` has the rest. *(Corrected after release: this
  entry also named .NET in the browser, which bundles an Emscripten older
  than 4.0 and cannot link the engine.)*
- **The `emscripten` and `emscripten-no-python` presets**, whose tests run
  under node, and a CI leg running them with Emscripten 4.0.10 and 6.0.12.

## 1.5.0

- **The C ABI and a C++17 layer over it**, binding contract 1.5 and pinning
  engine 1.5.2. `Ripple::engine` is `ripple/ripple.h` over the shared or static library; `Ripple::cpp` adds
  `ripple/ripple.hpp`: `Campaign`, `Session` and `Mixer` handles, `Value`,
  the `Effect` variant, and exceptions or `Result<T>` under
  `RIPPLE_HPP_NO_EXCEPTIONS`.
- **`Player`**, the loop over a session: holds answered from the host's
  `update()`, the sound applied, the author's named callbacks dispatched by
  name, a verb from inside a handler refused.
- **`SoundscapeDriver` and the `Speaker` interface**, with no speaker of its
  own: the driver does every duty the scheduler asks of a host — a late
  voice's entry point, a loop's wrap, voices reported ended or measured,
  tracks with no file told as silence — and a host's audio only plays.
  `docs/speaker.md` is the contract.
- **CMake that fetches the engine** for the target being built, refusing an
  archive whose digest is not the pinned one; `RIPPLE_ENGINE_DIR` for offline
  builds, `find_package(RippleEffect 1.5)` after an install.
