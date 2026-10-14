# ripple_effect_c_cpp_integration

The C and C++ bindings of the Ripple Effect engine: a CMake project that
fetches the engine's C ABI as a released binary and adds a header-only C++17
layer over it. **The engine is not built here.** It is released from the
private `ripple_effect` repository as `engine-v<version>` in the public
`ripple_engine_releases`, and this repository pins one (`engine.json`). Why
the C ABI is shaped as it is lives in that repository, as ADR
*the-engine-speaks-c* and `docs/internals/integrating-a-host.md` §11; neither
is repeated here.

| | |
| --- | --- |
| `include/ripple/ripple.hpp` | The C++17 layer, header-only: handles, `Value`, the `Effect` variant, errors or `Result<T>`, `Speaker`, `SoundscapeDriver`, `Player`. `ripple/ripple.h` is not here: it comes with the engine archive. |
| `cmake/RippleEngine.cmake` | Where the engine comes from: the pinned release (verified by digest), `RIPPLE_ENGINE_DIR`, or `RIPPLE_ENGINE_TREE` (cargo, UNVERIFIED). Picks the triple from the toolchain. |
| `cmake/RippleImport.cmake` | `Ripple::engine` from an unpacked archive, shared by the build tree and the installed config — the one file that knows the archive's layout. |
| `cmake/RippleInstall.cmake`, `RippleEffectConfig.cmake.in` | `cmake --install` and `find_package(RippleEffect 1.5)`. |
| `engine.json`, `engine-signing.pub` | The pin — the version, the key and every C archive's digest — and the key the manifest is checked against, a copy of `packages/ripple_native/native/engine-signing.pub` there. |
| `scripts/pin-engine.sh` | Writes the pin from a release's signed manifest, or (`--verify`) checks the committed one is one. |
| `tests/` | The conformance suite through `Session` and `Player`, a C99 program, the no-exceptions build, the soundscape driver, the values. |
| `examples/` | Terminal players in C and in C++ over any bundle folder. |
| `docs/speaker.md`, `docs/consuming.md` | The public speaker contract, and the three ways a project takes this one. |

## Environment

CMake 3.24 and a C11/C++17 compiler: GCC or Clang on Linux, Apple Clang,
MSVC (from a developer prompt, for the presets' Ninja). `cargo` too, only
with `RIPPLE_ENGINE_TREE`. Until `engine.json` pins a release, **every
configure needs `RIPPLE_ENGINE_TREE=<a ripple_effect checkout>`**, which
builds the engine's `ripple-capi` in that tree and marks the result
UNVERIFIED; the presets read it from the environment.

The hooks: `pre-commit install --hook-type pre-commit --hook-type pre-push`.
A commit pays for the secret scan (TruffleHog; a clone needs the binary),
codespell, and the clang-format and cmake-format checks — checked, never
applied. A push builds and tests on the Linux preset when there is an engine
to build against.

## Verifying a change

```sh
cmake --preset linux-clang && cmake --build --preset linux-clang && ctest --preset linux-clang
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan
# and linux-gcc, linux-static, linux-no-python; macos-clang; windows-msvc
```

The suite plays every conformance fixture of the pinned release through
`Session` and through `Player`, in the configured variant: 24 with Python,
21 without, skipping 3 — counts asserted, and the engine asked, not the
configuration. Fourteen tests in all. **A test has to be able to fail**: the
suite was seen red with a passage's title written into its text, which
fails every fixture through both hosts.

**The soundscape driver is tested against a speaker that writes down what it
is told**, on a clock that is a number (`tests/driver/`): the fade-in rising,
a late voice starting later in its file by the latency times the rate, a
handover fading the old out over its own two seconds before the new opens, a
reported end letting a voice go while a stopped voice's late end is ignored,
mute, pause. Nothing here makes a sound, so **a speaker of somebody's own is
heard, not only tested**, with `examples/cpp_player`.

`clang-tidy` (`.clang-tidy`, trimmed, every warning an error) runs in CI on
the Clang leg over the tests and examples, which is where `ripple.hpp` is
instantiated.

## The decisions

- **The C header is the engine's and ships with it.** `ripple.h` is
  generated in the engine repository and carried in every
  `ripple-engine-c-*` archive; this repository never copies it, so the
  header a host compiles against is always the library's own.
- **The pin is the trust anchor at configure time.** CMake cannot check
  Ed25519, so `scripts/pin-engine.sh` checks the manifest's signature when
  the pin moves and CI checks it on every run (`--verify`); a configure
  trusts the committed digests and refuses anything else.
- **`ripple.hpp` copies out of every batch.** An `Effect` owns its strings,
  and the C batch is freed before a verb returns. A host never holds an
  engine pointer through the C++ layer, and the cost is a copy per verb —
  turns, not frames.
- **One header for both error styles.** `Returns<T>` is `T` with exceptions
  and `Result<T>` without, so code reading results that way compiles both
  ways; the test `no_exceptions` builds with `-fno-exceptions` to hold it.
- **The driver and the player own no clock and no thread.** They read a
  `Clock` (steady by default, a number in tests) when the host calls
  `update()`; a game loop is the timer. A voice's reports are the one thing
  that may come from another thread, through `VoiceEvents`' locked inbox.
- **The driver is the host's half of the scheduler, and a speaker is only the
  sound** — ported from the JS binding's `SoundscapeDriver`, itself the
  editor's `SoundscapeMixer`. A new duty goes in the driver, or every
  speaker has to learn it.
- **A callback's handler table is the `Player`'s, host-side**
  (`campaign-host-api.md` §5b): the engine emits a name and an event kind,
  `ripple_callback_kind_name` spells the kind, and the lookup, the miss count
  and absorbing a throwing handler are here. A verb from inside a handler is
  refused (`Code::reentrant`), because the rest of the batch would arrive
  after the new one.
- **Ints are `int64_t`, Floats `double`**, and `Value`'s map is a vector of
  pairs, in order.

## Traps

- **A loop track is a fresh voice per pass.** Only a gapless `auto` bed is
  one looping voice; a test that expects `looping` on a `loop` track is wrong
  about the scheduler, not the driver.
- **A handover waits for the fade-out.** `tavern` fades over two seconds
  before `rain` opens; a check that looks too soon never sees rain at all.
- **`std::optional` checks under clang-tidy**: `bugprone-unchecked-optional-access`
  flags `.value()` too, which is why `Result<void>` keeps a flag rather than an
  optional.
- **clang-format 18 has no `Language: C`**: one style formats both, Allman.
- **CMake presets version 5** is the newest CMake 3.24 reads; 6 needs 3.25.

## What is deliberately not here

- **A speaker.** See `docs/speaker.md` for why, and how to write one.
- **A package manager.** vcpkg and Conan are candidates; the package config's
  SameMinorVersion and the archive layout are what a port would build on.
- **Threads.** A handle is used by one caller at a time; a host that wants
  the engine off its main thread moves the handle there.
- **A re-test of the engine's rules.** The fixtures test the spelling.

## Releasing

A version is the contract's `MAJOR.MINOR` and its own `PATCH`, in
`project(VERSION)` in `CMakeLists.txt`.
- Pin the engine: `scripts/pin-engine.sh <engine version>`, which verifies
  the signed manifest and writes every C archive's digest; commit
  `engine.json`.
- Run the presets on Linux, macOS and Windows locally — CI's matrix is the
  same — and the ASan one.
- Head `CHANGELOG.md`'s section `## <version>`, then dispatch **Release** on
  `main`. It refuses a version already tagged, one with no section and a pin
  that is not a signed release, builds and tests on the three platforms,
  then tags `v<version>` and creates the GitHub release with the source
  tarball and its `SHA256SUMS`. Nothing is published elsewhere.
