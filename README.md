# Ripple Effect for C and C++

The C and C++ bindings of the [Ripple Effect](https://ripplefx.app) engine:
play a campaign written in the Ripple Effect editor from a game, an app or an
engine of your own — Unreal, Godot, a console title, a phone game — with the
same story, the same dice and the same soundscapes the editor plays.

| | |
| --- | --- |
| **`Ripple::engine`** | The engine's C ABI: `ripple/ripple.h`, C99, over a shared or static library. Opaque handles, a `RippleResult` from every call that can fail, one `ripple_free` for everything the engine hands back. |
| **`Ripple::cpp`** | `ripple/ripple.hpp`, header-only C++17 over it, the way Vulkan-Hpp is over Vulkan: RAII handles, `std::variant` effects, exceptions or `ripple::Result<T>` under `RIPPLE_HPP_NO_EXCEPTIONS`, and the `Player` and `SoundscapeDriver` helpers. |

**No speaker ships.** A story's soundscapes are scheduled by the engine and
played by your audio — miniaudio, SDL, FMOD, Wwise or your engine's own —
through a small interface. [`docs/speaker.md`](docs/speaker.md) is the
contract, with mapping notes.

## A story in a few lines

```cpp
#include <ripple/ripple.hpp>

const auto campaign = ripple::Campaign::open_dir("campaigns/the-city-of-elsenmar");
ripple::SessionConfig config;
config.board = campaign.manifest().entries.front();
ripple::Session session = ripple::Session::create(campaign, config);

for (ripple::Effects batch = session.start();;) {
    const ripple::Effect& turn = batch.back();         // the Turn comes last
    if (ripple::is_over(turn)) break;
    if (auto* shown = std::get_if<ripple::Show>(&turn)) {
        draw(shown->passage);                          // Markdown, and its pictures
        batch = session.choose(pick(shown->passage));
    } else if (std::holds_alternative<ripple::Hold>(turn)) {
        wait(std::get<ripple::Hold>(turn).ms);         // your clock, never the engine's
        batch = session.elapsed();
    } else {
        batch = session.answer(fill(std::get<ripple::Ask>(turn).prompt));
    }
}
```

`ripple::Player` is the same loop written once: it answers holds from your
frame's `update()`, applies the sound, and dispatches the author's named
callbacks. `examples/` has a terminal player in C and one in C++.

## Getting it

Vendor it as a submodule, or use FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
    ripple_effect
    GIT_REPOSITORY https://github.com/ConnectingTheDots-DCWK/ripple_effect_c_cpp_integration
    GIT_TAG v1.5.0)
FetchContent_MakeAvailable(ripple_effect)
target_link_libraries(my_game PRIVATE Ripple::cpp)
```

The configure step downloads the engine for the target you are building and
refuses it unless its digest is the one this repository pins. No package
manager yet; vcpkg and Conan are candidates. Static or shared, with or
without Python, cross-compiling, Windows DLLs, offline builds and
`find_package` are all in [`docs/consuming.md`](docs/consuming.md).

**The engine is released on its own.** Each release of these bindings pins
one engine release, `engine-v<version>` of the public
[ripple_engine_releases](https://github.com/ConnectingTheDots-DCWK/ripple_engine_releases),
whose signed manifest names every archive. A version here is the engine's
contract, `MAJOR.MINOR`, and a `PATCH` of its own: 1.5.0 binds contract 1.5.

## Working on it

```sh
export RIPPLE_ENGINE_TREE=<a ripple_effect checkout>   # until a release is pinned
cmake --preset linux-clang && cmake --build --preset linux-clang && ctest --preset linux-clang
```

Every conformance fixture of the engine release plays through `Session` and
through `Player`, a C99 program plays one with no C++ at all, and the
soundscape driver is held to the scheduler on a speaker that writes down what
it is told. [`CLAUDE.md`](CLAUDE.md) has the rest.

## Licence

The bindings are MIT. The engine they download is © ConnectingTheDots under
the [Ripple Effect Runtime Licence](https://ripplefx.app/go/runtime-licence):
free, redistributable unmodified in function as part of your product, on the
condition that its `NOTICE` is reproduced. See [`LICENSE`](LICENSE).
