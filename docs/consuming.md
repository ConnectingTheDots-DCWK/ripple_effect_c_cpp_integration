# Consuming the bindings

Three ways in, one engine underneath. Whichever you choose, the configure
step picks the engine archive for the target you are building, downloads it
from the release engine.json pins, refuses it unless its SHA-256 is the
pinned one, and unpacks it into your build tree. You link one of two
targets:

| Target | What it is |
| --- | --- |
| `Ripple::engine` | The C library and `ripple/ripple.h`. |
| `Ripple::cpp` | The same, and `ripple/ripple.hpp` over it (C++17). |

## As a submodule

```sh
git submodule add https://github.com/ConnectingTheDots-DCWK/ripple_effect_c_cpp_integration third_party/ripple_effect
git -C third_party/ripple_effect checkout v1.6.0
```

```cmake
add_subdirectory(third_party/ripple_effect)
target_link_libraries(my_game PRIVATE Ripple::cpp)
ripple_effect_copy_runtime(my_game)   # the DLL beside the .exe, on Windows
```

## With FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    ripple_effect
    GIT_REPOSITORY https://github.com/ConnectingTheDots-DCWK/ripple_effect_c_cpp_integration
    GIT_TAG v1.6.0)
FetchContent_MakeAvailable(ripple_effect)
target_link_libraries(my_game PRIVATE Ripple::cpp)
```

Or the release's source tarball, pinned by the digest in its `SHA256SUMS`:

```cmake
FetchContent_Declare(
    ripple_effect
    URL https://github.com/ConnectingTheDots-DCWK/ripple_effect_c_cpp_integration/releases/download/v1.6.0/ripple_effect_c_cpp_integration-1.6.0.tar.gz
    URL_HASH SHA256=<from SHA256SUMS>)
```

As a subproject it builds no tests and no examples, and downloads nothing
but the engine.

## After `cmake --install`

```sh
cmake --preset linux-clang && cmake --build --preset linux-clang
cmake --install out/build/linux-clang --prefix /opt/ripple_effect
```

```cmake
find_package(RippleEffect 1.6 REQUIRED)   # CMAKE_PREFIX_PATH=/opt/ripple_effect
target_link_libraries(my_game PRIVATE Ripple::cpp)
```

The installed tree is `include/ripple/{ripple.h,ripple.hpp}`, the library
under `lib/` (the DLL under `bin/`), the package config under
`lib/cmake/RippleEffect/`, and the engine's notice as
`share/ripple_effect/ENGINE-NOTICE`. The package version is
**SameMinorVersion**: `MAJOR.MINOR` is the engine's contract, so a 1.6 host
takes any 1.6.x and nothing else.

## The options

| Option | Default | What it does |
| --- | --- | --- |
| `RIPPLE_ENGINE_PYTHON` | `ON` | The engine with the Python interpreter. `OFF` links the `no-python` one — about half the size — which refuses a campaign that needs Python at open (`RIPPLE_ERROR_NEEDS_PYTHON`). The editor's *Needs no Python* export is checked for exactly that host. |
| `RIPPLE_ENGINE_SHARED` | `ON` | The shared library. `OFF` links the static one, and with it the system libraries its archive lists (`lib/native-static-libs.txt`). Forced `OFF` on iOS and under Emscripten. |
| `RIPPLE_ENGINE_TARGET` | worked out | The engine's triple, for a toolchain the configure cannot name. |
| `RIPPLE_ENGINE_DIR` | — | An unpacked `ripple-engine-c` archive: offline, vendored, or audited by your own process. Nothing is downloaded. |
| `RIPPLE_ENGINE_CACHE` | the build tree | Where downloaded archives are kept, to share them between build trees. |
| `RIPPLE_ENGINE_TREE` | — | A `ripple_effect` checkout to build an unreleased engine from with cargo. For the engine's own developers; the result is marked UNVERIFIED and a release refuses it. |

## Per platform

- **Linux** (x86_64, aarch64): glibc 2.35 and up. The shared library's soname
  is `libripple_effect_engine.so`; ship it beside your binary and set an
  `$ORIGIN` RPATH, or link statically.
- **macOS** (x86_64, arm64), 12.0 and up: one architecture per configure; the
  release has no universal library, so build twice and `lipo`. The dylib's
  install name is `@rpath/libripple_effect_engine.dylib`.
- **Windows** (x64, arm64): MSVC's ABI only. The shared engine is a DLL with
  an import library; `ripple_effect_copy_runtime(<target>)` copies the DLL
  beside an executable after it links. The static library is built against
  the dynamic CRT (`/MD`).
- **Android** (arm64-v8a, x86_64), API 24 and up, with the NDK's toolchain
  file. The `.so` goes in your APK's `jniLibs`; the static library links
  into your own native library instead.
- **iOS** (device and arm64 simulator), 13.0 and up: static only — the engine
  ships no shared library for iOS, so `RIPPLE_ENGINE_SHARED` is forced `OFF`.
  A configure with `CMAKE_SYSTEM_NAME=iOS` fetches the `aarch64-apple-ios`
  archive; add `CMAKE_OSX_SYSROOT=iphonesimulator` and it fetches
  `aarch64-apple-ios-sim` instead. The `ios-arm64` and `ios-simulator` presets
  are those two. A simulator runtime's own minimum may raise the built
  deployment target above 13.0.
- **The web, through Emscripten**: see below.

## On the web

A program compiled to the browser with Emscripten links the engine as a
static library: a C++ engine built for the web, or Unity WebGL. Configure
with Emscripten's toolchain file, `emcmake cmake` or the `emscripten` preset,
and the archive for `wasm32-unknown-emscripten` is the one fetched. From
engine 1.5.3.

- **Emscripten 4.0 or later.** Each release is linked and played with
  4.0.10, 4.0.19 and the newest it was built with. Older ones fail, two ways:
  before 4.0 the optimiser rejects a feature the engine's objects carry, and
  **3.1.38 links and then cannot open a bundle's folder**, because its libc's
  file structures are not the ones the engine was compiled for. Nothing
  reports that one. **Unity 6000.0 to 6000.4 bundle 3.1.38 and are not
  supported; Unity 6000.5 and later bundle 4.0.19 and are.** Unity wants a
  plugin built with its own Emscripten: the archive's `.a` is the plugin.
- **.NET in the browser is not supported.** Its `wasm-tools` workload links
  with an Emscripten of its own — 3.1.34 for .NET 8, 3.1.56 for .NET 10 —
  below the floor; the C# bindings leave `browser-wasm` out for that reason.
- **Single-threaded.** A program linked with `-pthread` refuses the
  library: shared memory needs every object compiled with atomics, and the
  engine's standard library is not.
- **No exception flag is asked of you.** On this target a panic inside the
  engine aborts the module rather than unwinding, so it links into a program
  built with no flag, `-fexceptions` or `-fwasm-exceptions` alike. It also
  means a trap is not recoverable here as it is natively: the module ends,
  and the page reopens and resumes from its last checkpoint.
- **Open a campaign from memory.** A page has no folder to hand
  `ripple_campaign_open_dir`; fetch the bundle's files and pass them to
  `ripple_campaign_open_memory`. A folder works where one exists, under
  `-sNODERAWFS` or in Emscripten's virtual filesystem.
- **Size.** A linked program with the interpreter is about 5.4 MB of
  WebAssembly, 2.0 MB gzipped; without it, about 0.8 MB and 0.3 MB.

This repository's own tests run the same way, under node
(`ctest --preset emscripten`), against both variants and the oldest and
newest Emscripten supported.

The engine archive's `NOTICE` must be reproduced wherever your product lists
the third-party software it contains: the Ripple Effect Runtime Licence asks
that and nothing else.
