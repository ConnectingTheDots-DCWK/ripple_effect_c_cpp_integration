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
git -C third_party/ripple_effect checkout v1.5.0
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
    GIT_TAG v1.5.0)
FetchContent_MakeAvailable(ripple_effect)
target_link_libraries(my_game PRIVATE Ripple::cpp)
```

Or the release's source tarball, pinned by the digest in its `SHA256SUMS`:

```cmake
FetchContent_Declare(
    ripple_effect
    URL https://github.com/ConnectingTheDots-DCWK/ripple_effect_c_cpp_integration/releases/download/v1.5.0/ripple_effect_c_cpp_integration-1.5.0.tar.gz
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
find_package(RippleEffect 1.5 REQUIRED)   # CMAKE_PREFIX_PATH=/opt/ripple_effect
target_link_libraries(my_game PRIVATE Ripple::cpp)
```

The installed tree is `include/ripple/{ripple.h,ripple.hpp}`, the library
under `lib/` (the DLL under `bin/`), the package config under
`lib/cmake/RippleEffect/`, and the engine's notice as
`share/ripple_effect/ENGINE-NOTICE`. The package version is
**SameMinorVersion**: `MAJOR.MINOR` is the engine's contract, so a 1.5 host
takes any 1.5.x and nothing else.

## The options

| Option | Default | What it does |
| --- | --- | --- |
| `RIPPLE_ENGINE_PYTHON` | `ON` | The engine with the Python interpreter. `OFF` links the `no-python` one — about half the size — which refuses a campaign that needs Python at open (`RIPPLE_ERROR_NEEDS_PYTHON`). The editor's *Needs no Python* export is checked for exactly that host. |
| `RIPPLE_ENGINE_SHARED` | `ON` | The shared library. `OFF` links the static one, and with it the system libraries its archive lists (`lib/native-static-libs.txt`). Forced `OFF` on iOS. |
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
  install name is `@rpath/libripple_effect_engine.dylib` **from engine
  1.5.2**; engine 1.5.1's names the path it was built at, so with 1.5.1 link
  statically on macOS (`-DRIPPLE_ENGINE_SHARED=OFF`).
- **Windows** (x64, arm64): MSVC's ABI only. The shared engine is a DLL with
  an import library; `ripple_effect_copy_runtime(<target>)` copies the DLL
  beside an executable after it links. The static library is built against
  the dynamic CRT (`/MD`).
- **Android** (arm64-v8a, x86_64), API 24 and up, with the NDK's toolchain
  file. The `.so` goes in your APK's `jniLibs`; the static library links
  into your own native library instead.
- **iOS** (device, arm64 simulator), 13.0 and up: static only.

The engine archive's `NOTICE` must be reproduced wherever your product lists
the third-party software it contains: the Ripple Effect Runtime Licence asks
that and nothing else.
