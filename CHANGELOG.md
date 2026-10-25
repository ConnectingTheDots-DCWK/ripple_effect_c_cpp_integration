# Changelog

`MAJOR.MINOR` is the Ripple Effect engine contract these bindings bind, and
`PATCH` is their own. Each version pins one engine release, named here.

## 1.6.0

- **Contract 1.6, Grammar Lich**, pinning engine 1.6.0: what is new for a
  host in `ripple.h`, bound in `ripple.hpp`. The C++ layer refuses a 1.5
  header at compile time, and the package is `find_package(RippleEffect 1.6)`.
- **A story that reads the world asks first.** `Manifest::permissions`; the
  `Permit` turn, answered with `Session::permit(PermissionKind, bool)` —
  before `start` too, so a host that remembers its reader's answers never has
  them asked; `set_now` with a `Moment`, `set_position` with a `Position`,
  and `set_stand_in`; the `Notice` effect, once per kind and outcome; and
  `Code::not_permitted` and `Code::stand_in`, thrown as `HostError`.
  `PermissionKind` and `permission_kind_name` are spelled here, because
  engine 1.6.0's `ripple.h` names `RipplePermissionKind` without declaring
  it. `local_moment()` is this machine's time at its own offset.
- **A passage may count down.** `Passage::timer`, a `ChoiceTimer`;
  `Session::begin_countdown` and `expire`; `Code::no_countdown` and
  `Code::countdown_not_begun`. The `Player` runs one on its own clock from
  `update()`, as it does a hold, keeps it across a `set_locale` and drops it
  once the reader chooses, and says where it stands with `countdown()`.
  `PlayerHandlers::countdowns = false` leaves the timing to the host — a test,
  or a reader who asked for no time limits.
- **The `Player` tells the story the time**: `PlayerHandlers::moment`, asked
  before every verb that can step. And `Player::permit`, `begin_countdown`
  and `expire`.
- **A form speaks the reader's language, and may offer fixed answers.**
  `PromptField` gains `label`, `placeholder`, `optional` and `options`, each a
  `PromptOption`: the `id` a host answers with, the `label` it shows, the
  `value` the story is handed.
- **`Effect` has two more alternatives**, `Permit` and `Notice`, appended:
  a `std::visit` with no arm for them no longer compiles, which is what the
  variant is for. The two `countdown.*` callback events and the `W.RT.08` run
  remark arrive as numbers the engine names, as the others do.
- **Thirty-three conformance fixtures**, twenty-seven without Python, through
  `Session` and `Player`, with the fixtures' `now`, `permit` and `standIns`
  and their `countdown`, `expire` and `permit` moves. The Player's own
  countdown and the permit turn are tested on a clock that is a number; the
  C99 program runs out a countdown and answers a permit.
- **The example players** ask before a story reads the clock or the
  location, and the C++ one runs countdowns on the Player's clock.

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
