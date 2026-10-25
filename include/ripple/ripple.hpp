// The Ripple Effect engine, in C++17 — a header-only layer over the engine's
// C ABI (ripple/ripple.h), the way Vulkan-Hpp is over Vulkan.
//
// What it adds is ownership and types, and nothing the engine decides:
//
//   Campaign, Session, Mixer   move-only handles; destroyed with them
//   Value                      a story's value: null, bool, Int, Float, text,
//                              list, map — the map in its order
//   Effect                     a std::variant of what a verb answers; a
//                              std::visit with an arm missing fails to compile
//   Speaker, Voice             the interface a host's audio is plugged into
//   SoundscapeDriver           the host's half of the soundscape scheduler
//   Player                     the safe loop over a session: holds, callbacks
//
// **Nothing here owns a clock or a thread.** A host calls `update()` on the
// driver and the player from its own loop, and they read the clock they were
// given (`std::chrono::steady_clock` unless a test says otherwise).
//
// **Errors** are exceptions — `ripple::Error` and the classes below it — or,
// with `RIPPLE_HPP_NO_EXCEPTIONS` defined, a `ripple::Result<T>` from every
// call that can fail, for a game built with `-fno-exceptions`. The same code
// compiles both ways when it reads results through `ripple::Returns<T>`.
//
// Binds contract 1.6. MIT, like the rest of this repository; the engine it
// calls is under the Ripple Effect Runtime Licence.
#pragma once

#include <ripple/ripple.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifndef RIPPLE_HPP_NO_EXCEPTIONS
#include <exception>
#include <stdexcept>
#endif

#if RIPPLE_CONTRACT_MAJOR != 1 || RIPPLE_CONTRACT_MINOR != 6
#error "ripple.hpp binds contract 1.6, and this ripple/ripple.h is another contract's"
#endif

namespace ripple
{

inline constexpr unsigned contract_major = 1;
inline constexpr unsigned contract_minor = 6;

/// The engine library's version, `1.6.0` — the one loaded, not the one built against.
inline std::string_view engine_version() noexcept
{
    return ripple_engine_version();
}

/// The contract the loaded library binds, `1.6`.
inline std::string_view engine_contract() noexcept
{
    return ripple_engine_contract();
}

/// Whether the loaded library runs scripts: false for the `no-python` one.
inline bool engine_has_python() noexcept
{
    return ripple_engine_has_python();
}

////////////////////////////////////////////////////////////////////////////////
// Failures

/// What a call came to, by the engine's codes — and `reentrant`, this
/// header's own, for a verb a `Player` was asked for while it was delivering.
enum class Code : int
{
    success = RIPPLE_SUCCESS,
    bundle_format = RIPPLE_ERROR_BUNDLE_FORMAT,
    bundle_version = RIPPLE_ERROR_BUNDLE_VERSION,
    needs_python = RIPPLE_ERROR_NEEDS_PYTHON,
    wrong_turn = RIPPLE_ERROR_WRONG_TURN,
    no_such_choice = RIPPLE_ERROR_NO_SUCH_CHOICE,
    choice_spent = RIPPLE_ERROR_CHOICE_SPENT,
    stopped = RIPPLE_ERROR_STOPPED,
    nothing_asked = RIPPLE_ERROR_NOTHING_ASKED,
    unknown_locale = RIPPLE_ERROR_UNKNOWN_LOCALE,
    not_permitted = RIPPLE_ERROR_NOT_PERMITTED,
    stand_in = RIPPLE_ERROR_STAND_IN,
    no_countdown = RIPPLE_ERROR_NO_COUNTDOWN,
    countdown_not_begun = RIPPLE_ERROR_COUNTDOWN_NOT_BEGUN,
    board_gone = RIPPLE_ERROR_BOARD_GONE,
    node_gone = RIPPLE_ERROR_NODE_GONE,
    invalid_argument = RIPPLE_ERROR_INVALID_ARGUMENT,
    trap = RIPPLE_ERROR_TRAP,
    reentrant = -100,
};

/// Everything the engine said about a failure. Which fields mean something
/// depends on `code`, as in `RippleError`.
struct Failure
{
    Code code = Code::success;
    std::string message;
    std::optional<std::string> location;  // bundle_format
    std::int64_t found = 0;               // bundle_version
    std::int64_t supported = 0;           // bundle_version
    std::optional<std::int64_t> oldest;   // bundle_version
    std::uint32_t asked = 0;              // wrong_turn: a RippleTurnKind
    std::uint32_t answered = 0;           // wrong_turn: a RippleVerb
    std::uint32_t offered = 0;            // no_such_choice
    std::uint32_t chosen = 0;             // no_such_choice
    std::string subject;                  // choice_spent, unknown_locale, *_gone; a kind's name
    std::string node;                     // node_gone
};

#ifndef RIPPLE_HPP_NO_EXCEPTIONS

/// The base of every failure thrown, carrying what the engine said.
class Error : public std::runtime_error
{
  public:
    explicit Error(Failure failure)
        : std::runtime_error(failure.message), failure_(std::move(failure))
    {
    }
    const Failure& failure() const noexcept { return failure_; }
    Code code() const noexcept { return failure_.code; }

  private:
    Failure failure_;
};

/// A document of the bundle is malformed, missing, or from a newer engine.
class BundleError : public Error
{
    using Error::Error;
};
/// The bundle needs Python, and this is the `no-python` library.
class NeedsPython : public Error
{
    using Error::Error;
};
/// A verb the story was not asking for, or a reading the host may not hand
/// in — host API §11.
class HostError : public Error
{
    using Error::Error;
};
/// A save that cannot be reunited with this campaign.
class ResumeError : public Error
{
    using Error::Error;
};
/// A NULL or malformed argument; nothing was done.
class InvalidArgument : public Error
{
    using Error::Error;
};
/// The engine panicked, or did earlier on this handle, which is poisoned:
/// destroy it, open again, and resume from the last checkpoint.
class EngineTrap : public Error
{
    using Error::Error;
};
/// A `Player` verb called while it was delivering a batch, from inside a
/// handler. Schedule the verb for after the handler instead.
class ReentrantVerb : public Error
{
    using Error::Error;
};

#endif

/// A value or the failure that took its place — what every call answers
/// under `RIPPLE_HPP_NO_EXCEPTIONS`.
template <class T>
class Result
{
  public:
    Result(T value) : held_(std::in_place_index<0>, std::move(value)) {}
    Result(Failure failure) : held_(std::in_place_index<1>, std::move(failure)) {}

    bool ok() const noexcept { return held_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }
    T& value() & { return std::get<0>(held_); }
    const T& value() const& { return std::get<0>(held_); }
    T&& value() && { return std::get<0>(std::move(held_)); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const Failure& failure() const { return std::get<1>(held_); }

  private:
    std::variant<T, Failure> held_;
};

template <>
class Result<void>
{
  public:
    Result() = default;
    Result(Failure failure) : failure_(std::move(failure)), ok_(false) {}
    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok(); }
    /// What failed; a `Failure` whose code is `success` when nothing did.
    const Failure& failure() const noexcept { return failure_; }

  private:
    Failure failure_;
    bool ok_ = true;
};

#ifdef RIPPLE_HPP_NO_EXCEPTIONS
/// What a call that can fail answers: `Result<T>` here, `T` with exceptions.
template <class T>
using Returns = Result<T>;
#else
/// What a call that can fail answers: `T` here, `Result<T>` without exceptions.
template <class T>
using Returns = T;
#endif

namespace detail
{

inline std::string str(RippleStr text)
{
    return text.ptr ? std::string(text.ptr, text.len) : std::string();
}

inline std::optional<std::string> opt(RippleStr text)
{
    if (!text.ptr) return std::nullopt;
    return std::string(text.ptr, text.len);
}

inline RippleStr span(std::string_view text) noexcept
{
    // A present empty string has a non-NULL pointer; NULL means absent.
    return RippleStr{text.data() ? text.data() : "", text.size()};
}

inline RippleStr maybe_span(const std::optional<std::string>& text) noexcept
{
    return text ? span(std::string_view(*text)) : RippleStr{nullptr, 0};
}

template <class T>
std::optional<T> opt(bool present, T value)
{
    return present ? std::optional<T>(value) : std::nullopt;
}

inline std::optional<std::uint32_t> opt(RippleOptionalU32 value)
{
    return opt(value.present, value.value);
}
inline std::optional<std::uint64_t> opt(RippleOptionalU64 value)
{
    return opt(value.present, value.value);
}
inline std::optional<std::int64_t> opt(RippleOptionalI64 value)
{
    return opt(value.present, value.value);
}

/// Something the engine handed out, given back when it goes.
struct Free
{
    void operator()(const void* owned) const noexcept { ripple_free(owned); }
};
template <class T>
using Owned = std::unique_ptr<const T, Free>;

template <class T, class Item>
std::vector<T> list(const Item* items, std::size_t count, T (*read)(const Item&))
{
    std::vector<T> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.push_back(read(items[i]));
    return out;
}

inline std::vector<std::string> texts(const RippleStr* items, std::size_t count)
{
    return list<std::string, RippleStr>(items, count,
                                        [](const RippleStr& text) { return str(text); });
}

/// The failure an error describes, and the error given back.
inline Failure take(RippleResult code, const RippleError* error)
{
    Failure failure;
    failure.code = static_cast<Code>(code);
    if (error)
    {
        failure.message = str(error->message);
        failure.location = opt(error->location);
        failure.found = error->found;
        failure.supported = error->supported;
        failure.oldest = opt(error->oldest);
        failure.asked = error->asked;
        failure.answered = error->answered;
        failure.offered = error->offered;
        failure.chosen = error->chosen;
        failure.subject = str(error->subject);
        failure.node = str(error->node);
        ripple_free(error);
    }
    if (failure.message.empty()) failure.message = "the engine refused the call";
    return failure;
}

#ifndef RIPPLE_HPP_NO_EXCEPTIONS
[[noreturn]] inline void raise(Failure failure)
{
    switch (failure.code)
    {
        case Code::bundle_format:
        case Code::bundle_version:
            throw BundleError(std::move(failure));
        case Code::needs_python:
            throw NeedsPython(std::move(failure));
        case Code::wrong_turn:
        case Code::no_such_choice:
        case Code::choice_spent:
        case Code::stopped:
        case Code::nothing_asked:
        case Code::unknown_locale:
        case Code::not_permitted:
        case Code::stand_in:
        case Code::no_countdown:
        case Code::countdown_not_begun:
            throw HostError(std::move(failure));
        case Code::board_gone:
        case Code::node_gone:
            throw ResumeError(std::move(failure));
        case Code::trap:
            throw EngineTrap(std::move(failure));
        case Code::reentrant:
            throw ReentrantVerb(std::move(failure));
        case Code::invalid_argument:
        case Code::success:
        default:
            throw InvalidArgument(std::move(failure));
    }
}
#endif

/// A call's result, as this header answers it: `make()` on success, the
/// failure thrown or returned otherwise.
template <class T, class Make>
Returns<T> finish(RippleResult code, const RippleError* error, Make&& make)
{
    if (code != RIPPLE_SUCCESS)
    {
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        return Result<T>(take(code, error));
#else
        raise(take(code, error));
#endif
    }
    if constexpr (std::is_void_v<T>)
    {
        make();
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        return Result<void>();
#endif
    }
    else
    {
        return make();
    }
}

template <class T>
Returns<T> fail(Failure failure)
{
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
    return Result<T>(std::move(failure));
#else
    raise(std::move(failure));
#endif
}

}  // namespace detail

////////////////////////////////////////////////////////////////////////////////
// Values

/// A story's value. An Int is `std::int64_t` and a Float a `double`, and the
/// two never become each other; a map keeps the order it was written in.
class Value
{
  public:
    using List = std::vector<Value>;
    using Map = std::vector<std::pair<std::string, Value>>;
    using Storage =
        std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, List, Map>;

    Value() noexcept : held_(nullptr) {}
    Value(std::nullptr_t) noexcept : held_(nullptr) {}
    Value(bool truth) noexcept : held_(truth) {}
    template <class I, std::enable_if_t<std::is_integral_v<I> && !std::is_same_v<I, bool>, int> = 0>
    Value(I whole) noexcept : held_(static_cast<std::int64_t>(whole))
    {
    }
    Value(double real) noexcept : held_(real) {}
    Value(std::string text) : held_(std::move(text)) {}
    Value(const char* text) : held_(std::string(text)) {}
    Value(std::string_view text) : held_(std::string(text)) {}
    Value(List items) : held_(std::move(items)) {}
    Value(Map entries) : held_(std::move(entries)) {}

    bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(held_); }
    bool is_bool() const noexcept { return std::holds_alternative<bool>(held_); }
    bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(held_); }
    bool is_float() const noexcept { return std::holds_alternative<double>(held_); }
    bool is_text() const noexcept { return std::holds_alternative<std::string>(held_); }
    bool is_list() const noexcept { return std::holds_alternative<List>(held_); }
    bool is_map() const noexcept { return std::holds_alternative<Map>(held_); }

    template <class T>
    const T* get_if() const noexcept
    {
        return std::get_if<T>(&held_);
    }
    const Storage& storage() const noexcept { return held_; }

    /// A map's value under `key`, or nullptr — for a list or anything else too.
    const Value* find(std::string_view key) const noexcept
    {
        if (const Map* entries = get_if<Map>())
        {
            for (const auto& [name, value] : *entries)
                if (name == key) return &value;
        }
        return nullptr;
    }

  private:
    Storage held_;
};

namespace detail
{

// A child that is not after its parent, or past the end, reads as null: the
// rule that keeps an arena from looping, as the engine reads one.
inline Value read(const RippleValueNode* nodes, std::size_t count, std::size_t at)
{
    if (at >= count) return Value();
    const RippleValueNode& node = nodes[at];
    auto child = [&](std::uint32_t of)
    { return of > at && of < count ? read(nodes, count, of) : Value(); };
    switch (node.kind)
    {
        case RIPPLE_VALUE_KIND_BOOLEAN:
            return Value(node.data.boolean);
        case RIPPLE_VALUE_KIND_INTEGER:
            return Value(node.data.integer);
        case RIPPLE_VALUE_KIND_REAL:
            return Value(node.data.real);
        case RIPPLE_VALUE_KIND_TEXT:
            return Value(str(node.data.text));
        case RIPPLE_VALUE_KIND_ITEMS:
        {
            Value::List items;
            items.reserve(node.data.items.count);
            for (std::size_t i = 0; i < node.data.items.count; ++i)
                items.push_back(child(node.data.items.ptr[i]));
            return Value(std::move(items));
        }
        case RIPPLE_VALUE_KIND_ENTRIES:
        {
            Value::Map entries;
            entries.reserve(node.data.entries.count);
            for (std::size_t i = 0; i < node.data.entries.count; ++i)
            {
                const RippleValueEntry& entry = node.data.entries.ptr[i];
                entries.emplace_back(str(entry.key), child(entry.node));
            }
            return Value(std::move(entries));
        }
        default:
            return Value();
    }
}

inline Value value(const RippleValue& arena)
{
    return read(arena.nodes, arena.count, 0);
}

inline std::optional<Value> value(const RippleValue* arena)
{
    if (!arena) return std::nullopt;
    return value(*arena);
}

/// A value laid out as the engine reads one, and everything its view points
/// into. It owns what it points at, so it moves but never copies.
class Arena
{
  public:
    explicit Arena(const Value& root) { push(root); }
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    RippleValue view() const noexcept { return RippleValue{nodes_.data(), nodes_.size()}; }

  private:
    std::uint32_t push(const Value& value)
    {
        const auto at = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(RippleValueNode{});
        RippleValueNode node{};
        node.kind = RIPPLE_VALUE_KIND_NULL;
        std::visit(
            [&](const auto& held)
            {
                using T = std::decay_t<decltype(held)>;
                if constexpr (std::is_same_v<T, bool>)
                {
                    node.kind = RIPPLE_VALUE_KIND_BOOLEAN;
                    node.data.boolean = held;
                }
                else if constexpr (std::is_same_v<T, std::int64_t>)
                {
                    node.kind = RIPPLE_VALUE_KIND_INTEGER;
                    node.data.integer = held;
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    node.kind = RIPPLE_VALUE_KIND_REAL;
                    node.data.real = held;
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    node.kind = RIPPLE_VALUE_KIND_TEXT;
                    node.data.text = span(std::string_view(held));
                }
                else if constexpr (std::is_same_v<T, Value::List>)
                {
                    std::vector<std::uint32_t> children;
                    children.reserve(held.size());
                    for (const Value& item : held) children.push_back(push(item));
                    items_.push_back(std::move(children));
                    node.kind = RIPPLE_VALUE_KIND_ITEMS;
                    node.data.items = RippleIndices{items_.back().data(), items_.back().size()};
                }
                else if constexpr (std::is_same_v<T, Value::Map>)
                {
                    std::vector<RippleValueEntry> children;
                    children.reserve(held.size());
                    for (const auto& [key, item] : held)
                    {
                        // The child first, then the entry: a push can grow
                        // `entries_`, but never moves what is already in it.
                        const std::uint32_t child = push(item);
                        children.push_back(RippleValueEntry{span(std::string_view(key)), child});
                    }
                    entries_.push_back(std::move(children));
                    node.kind = RIPPLE_VALUE_KIND_ENTRIES;
                    node.data.entries =
                        RippleEntries{entries_.back().data(), entries_.back().size()};
                }
            },
            value.storage());
        nodes_[at] = node;
        return at;
    }

    // Each a std::deque, so what a node points into stays where it is as more is pushed.
    std::vector<RippleValueNode> nodes_;
    std::deque<std::vector<std::uint32_t>> items_;
    std::deque<std::vector<RippleValueEntry>> entries_;
};

/// Named values, as `answer` and a script take them.
class FieldArena
{
  public:
    explicit FieldArena(const Value::Map& fields)
    {
        for (const auto& [key, value] : fields) arenas_.emplace_back(value);
        std::size_t i = 0;
        for (const auto& [key, value] : fields)
            fields_.push_back(RippleField{span(std::string_view(key)), arenas_[i++].view()});
    }
    RippleFields view() const noexcept { return RippleFields{fields_.data(), fields_.size()}; }

  private:
    std::deque<Arena> arenas_;
    std::vector<RippleField> fields_;
};

}  // namespace detail

/// A value as text, the way a passage would print it.
inline Returns<std::string> as_text(const Value& value)
{
    detail::Arena arena(value);
    const RippleValue view = arena.view();
    const RippleStr* out = nullptr;
    const RippleError* error = nullptr;
    const RippleResult code = ripple_value_as_text(&view, &out, &error);
    return detail::finish<std::string>(code, error,
                                       [&]
                                       {
                                           detail::Owned<RippleStr> held(out);
                                           return detail::str(*held);
                                       });
}

////////////////////////////////////////////////////////////////////////////////
// The bundle's records

enum class TrackMode : std::uint32_t
{
    once = RIPPLE_TRACK_MODE_ONCE,
    looping = RIPPLE_TRACK_MODE_LOOPING,
    automatic = RIPPLE_TRACK_MODE_AUTO,
};

enum class PortKind : std::uint32_t
{
    control = RIPPLE_PORT_KIND_CONTROL,
    data = RIPPLE_PORT_KIND_DATA,
};

/// One clip on a soundscape's timeline. Times are milliseconds.
struct Track
{
    std::int64_t media = 0;
    std::int64_t lane = 0;
    std::int64_t start = 0;
    std::int64_t clip_start = 0;
    std::optional<std::int64_t> clip_end;
    double volume = 1.0;
    double speed = 1.0;
    std::int64_t fade_in = 0;
    std::int64_t fade_out = 0;
    TrackMode mode = TrackMode::once;
    std::int64_t interval = 0;
};

/// A timeline of clips a passage plays.
struct Soundscape
{
    std::string id;
    std::string name;
    std::int64_t length = 0;
    bool looping = false;
    std::int64_t fade_in = 0;
    std::int64_t fade_out = 0;
    double volume = 1.0;
    bool tied = false;
    std::vector<Track> tracks;
};

struct Snippet
{
    std::string code;
    bool is_inline = false;
};

struct Entry
{
    std::string id;
    std::string name;
    std::string path;
};

struct Project
{
    std::string title;
    std::string description;
    std::string default_locale;
    std::vector<std::string> locales;
    std::optional<std::string> cover;
};

struct Cvar
{
    std::string name;
    std::string type;
    Value default_value;
};

/// `campaign.json`: what the bundle holds, table by table.
struct Manifest
{
    std::int64_t format = 0;
    std::optional<std::string> generator;
    std::string text_format;
    Project project;
    std::vector<std::uint32_t> entries;
    std::vector<Entry> boards, passages, components, scripts, media, machines, soundscapes;
    std::vector<Cvar> cvars;
    std::vector<std::string> needs;
    /// What the story reads of the world outside it, by wire name —
    /// `dateTime`, `location`. A session asks about each before it begins.
    std::vector<std::string> permissions;
};

struct Edge
{
    std::int64_t node = 0;
    std::int64_t port = 0;
};

struct Port
{
    std::string id;
    PortKind kind = PortKind::control;
    std::vector<Edge> edges;
};

struct Node
{
    std::string id;
    std::string type;
    std::string title;
    std::vector<Port> inputs, outputs;
    Value data;
    Value metadata;
};

struct Board
{
    std::string id;
    std::string name;
    std::int64_t start = 0;
    std::vector<std::uint32_t> prime;
    std::vector<Node> nodes;
};

struct BundleProblem
{
    std::string message;
    std::string location;
};

namespace detail
{

inline Track track(const RippleTrack& t)
{
    return Track{t.media,   t.lane,  t.start,   t.clip_start, opt(t.clip_end),
                 t.volume,  t.speed, t.fade_in, t.fade_out,   static_cast<TrackMode>(t.mode),
                 t.interval};
}

inline Soundscape soundscape(const RippleSoundscape& s)
{
    return Soundscape{
        str(s.id), str(s.name), s.length,
        s.looping, s.fade_in,   s.fade_out,
        s.volume,  s.tied,      list<Track, RippleTrack>(s.tracks, s.track_count, track)};
}

inline std::optional<Soundscape> soundscape(const RippleSoundscape* s)
{
    if (!s) return std::nullopt;
    return soundscape(*s);
}

inline Entry entry(const RippleEntry& e)
{
    return Entry{str(e.id), str(e.name), str(e.path)};
}

inline std::vector<Entry> entries(const RippleEntry* items, std::size_t count)
{
    return list<Entry, RippleEntry>(items, count, entry);
}

inline Cvar cvar(const RippleCvar& c)
{
    return Cvar{str(c.name), str(c.type_name), value(c.default_value)};
}

inline Manifest manifest(const RippleManifest& m)
{
    Manifest out;
    out.format = m.format;
    out.generator = opt(m.generator);
    out.text_format = str(m.text_format);
    out.project =
        Project{str(m.project.title), str(m.project.description), str(m.project.default_locale),
                texts(m.project.locales, m.project.locale_count), opt(m.project.cover)};
    out.entries.assign(m.entries, m.entries + m.entry_count);
    out.boards = entries(m.boards, m.board_count);
    out.passages = entries(m.passages, m.passage_count);
    out.components = entries(m.components, m.component_count);
    out.scripts = entries(m.scripts, m.script_count);
    out.media = entries(m.media, m.media_count);
    out.machines = entries(m.machines, m.machine_count);
    out.soundscapes = entries(m.soundscapes, m.soundscape_count);
    out.cvars = list<Cvar, RippleCvar>(m.cvars, m.cvar_count, cvar);
    out.needs = texts(m.needs, m.need_count);
    out.permissions = texts(m.permissions, m.permission_count);
    return out;
}

inline Port port(const RipplePort& p)
{
    return Port{str(p.id), static_cast<PortKind>(p.kind),
                list<Edge, RippleEdge>(p.edges, p.edge_count,
                                       [](const RippleEdge& e) { return Edge{e.node, e.port}; })};
}

inline Node node(const RippleNode& n)
{
    return Node{str(n.id),
                str(n.type_name),
                str(n.title),
                list<Port, RipplePort>(n.inputs, n.input_count, port),
                list<Port, RipplePort>(n.outputs, n.output_count, port),
                value(n.data),
                value(n.metadata)};
}

inline Board board(const RippleBoard& b)
{
    Board out{
        str(b.id), str(b.name), b.start, {}, list<Node, RippleNode>(b.nodes, b.node_count, node)};
    out.prime.assign(b.prime, b.prime + b.prime_count);
    return out;
}

/// A soundscape laid out for the engine, pointing into `scape`'s strings.
class SoundscapeView
{
  public:
    explicit SoundscapeView(const Soundscape& scape)
    {
        for (const Track& t : scape.tracks)
        {
            tracks_.push_back(RippleTrack{
                t.media, t.lane, t.start, t.clip_start,
                RippleOptionalI64{t.clip_end.has_value(), t.clip_end.value_or(0)}, t.volume,
                t.speed, t.fade_in, t.fade_out, static_cast<std::uint32_t>(t.mode), t.interval});
        }
        view_ = RippleSoundscape{span(std::string_view(scape.id)),
                                 span(std::string_view(scape.name)),
                                 scape.length,
                                 scape.looping,
                                 scape.fade_in,
                                 scape.fade_out,
                                 scape.volume,
                                 scape.tied,
                                 tracks_.data(),
                                 tracks_.size()};
    }
    const RippleSoundscape* get() const noexcept { return &view_; }

  private:
    std::vector<RippleTrack> tracks_;
    RippleSoundscape view_{};
};

}  // namespace detail

////////////////////////////////////////////////////////////////////////////////
// The world outside the story — `campaign-host-api.md` §7a

/// What a story may read of the world outside it. Open for appending; the
/// microphone is not one.
///
/// The numbers are `RipplePermissionKind`'s, which `ripple.h` names in its
/// comments but does not declare in 1.6.0, so they are spelled here.
enum class PermissionKind : std::uint32_t
{
    date_time = 0,
    location = 1,
};

/// A kind's wire name — `dateTime`, `location` — as the manifest's
/// `permissions` and a notice spell it; empty for a number this header does
/// not know.
inline std::string_view permission_kind_name(PermissionKind kind) noexcept
{
    switch (kind)
    {
        case PermissionKind::date_time:
            return "dateTime";
        case PermissionKind::location:
            return "location";
        default:
            return "";
    }
}

/// An instant from the host: milliseconds since the epoch, and the offset in
/// minutes the reader's wall clock is at.
struct Moment
{
    std::int64_t utc_ms = 0;
    std::int32_t offset_minutes = 0;
};

/// A position fix, any field of which may be absent — shaped after the web's
/// `GeolocationPosition`. Degrees, and the accuracy in metres.
struct Position
{
    std::optional<std::string> label;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> accuracy;
    /// When the fix was taken.
    std::optional<Moment> when;
};

namespace detail
{

// Days since 1970-01-01 of a proleptic Gregorian date: Howard Hinnant's
// days_from_civil, which needs neither timegm nor a time zone database.
constexpr std::int64_t days_from_civil(std::int64_t year, unsigned month, unsigned day) noexcept
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto of_era = static_cast<unsigned>(year - era * 400);
    const unsigned of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned of_cycle = of_era * 365 + of_era / 4 - of_era / 100 + of_year;
    return era * 146097 + static_cast<std::int64_t>(of_cycle) - 719468;
}

inline std::int64_t seconds_of(const std::tm& at) noexcept
{
    const std::int64_t days = days_from_civil(
        at.tm_year + 1900, static_cast<unsigned>(at.tm_mon + 1), static_cast<unsigned>(at.tm_mday));
    return ((days * 24 + at.tm_hour) * 60 + at.tm_min) * 60 + at.tm_sec;
}

inline RippleMoment moment(const Moment& at) noexcept
{
    return RippleMoment{at.utc_ms, at.offset_minutes};
}

}  // namespace detail

/// What time it is on this machine, at its own offset — what a host that
/// granted `date_time` hands `Session::set_now`, or a `Player` asks for
/// itself through `PlayerHandlers::moment`.
inline Moment local_moment() noexcept
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    std::tm utc{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
    gmtime_s(&utc, &seconds);
#else
    localtime_r(&seconds, &local);
    gmtime_r(&seconds, &utc);
#endif
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return Moment{
        static_cast<std::int64_t>(ms),
        static_cast<std::int32_t>((detail::seconds_of(local) - detail::seconds_of(utc)) / 60)};
}

////////////////////////////////////////////////////////////////////////////////
// What a verb answers

struct Choice
{
    std::string port;
    std::string text;
    /// False for a choice the author limited and the reader used up.
    bool available = true;
};

struct LocaleText
{
    std::string locale;
    std::string text;
};

/// A link in a passage's prose and the medium it names.
struct MediaLink
{
    std::string link;
    std::uint32_t media = 0;
};

/// A passage's countdown — `campaign-host-api.md` §4b. The host's clock
/// waits `after_ms`, calls `begin_countdown`, waits `timeout_ms` and calls
/// `expire`; the engine measures none of it. A `Player` does all three.
struct ChoiceTimer
{
    std::uint64_t after_ms = 0;
    /// Never zero.
    std::uint64_t timeout_ms = 0;
    /// Whether the author meant the reader to see it count.
    bool visible = true;
    /// The index into the passage's choices taken when it runs out.
    std::uint32_t default_choice = 0;
};

/// A passage the reader is on.
struct Passage
{
    std::string node;
    std::string title;
    /// Markdown, or HTML when the manifest's text_format says so.
    std::string text;
    std::vector<Choice> choices;
    std::optional<std::vector<LocaleText>> sources;
    std::vector<Snippet> snippets;
    std::vector<MediaLink> media;
    std::optional<std::string> untranslated;
    std::optional<Soundscape> soundscape;
    bool silence = false;
    /// None when the passage waits for the reader as long as they like.
    std::optional<ChoiceTimer> timer;
};

/// One fixed answer a form field offers.
struct PromptOption
{
    /// What a host answers with; the same in every language.
    std::string id;
    /// What the reader is shown, in the reader's language.
    std::string label;
    /// What the story is handed when it is picked, of the field's type.
    Value value;
};

struct PromptField
{
    /// What an answer is keyed by — the field's ID, never shown.
    std::string name;
    std::string type;
    /// What the control is filled in with; a default nobody edits is the answer.
    Value default_value;
    /// What the reader is shown, in the reader's language: `name` where the
    /// author wrote nothing.
    std::string label;
    /// The hint drawn while the field is empty; empty for none.
    std::string placeholder;
    /// Whether the reader may leave it empty, which answers null. A required
    /// field is not submitted empty.
    bool optional = false;
    /// The fixed answers to pick from, in order — one for a scalar type, any
    /// number for a `…List` — answered with the picked options' ids. Empty
    /// for a field the reader types into.
    std::vector<PromptOption> options;
};

/// A form the story asks the reader to fill.
struct Prompt
{
    std::string node;
    std::string title;
    std::vector<PromptField> fields;
};

/// The soundscape in force, and the node that set it.
struct Ambience
{
    Soundscape soundscape;
    std::uint32_t board = 0;
    std::string node;
};

enum class RunEventKind : std::uint32_t
{
    run_started = RIPPLE_RUN_EVENT_KIND_RUN_STARTED,
    node_started = RIPPLE_RUN_EVENT_KIND_NODE_STARTED,
    node_finished = RIPPLE_RUN_EVENT_KIND_NODE_FINISHED,
    memo_hit = RIPPLE_RUN_EVENT_KIND_MEMO_HIT,
    diagnostic = RIPPLE_RUN_EVENT_KIND_DIAGNOSTIC,
    log = RIPPLE_RUN_EVENT_KIND_LOG,
    run_finished = RIPPLE_RUN_EVENT_KIND_RUN_FINISHED,
};

enum class PayloadKind : std::uint32_t
{
    present = RIPPLE_PAYLOAD_KIND_PRESENT,
    withheld = RIPPLE_PAYLOAD_KIND_WITHHELD,
    absent = RIPPLE_PAYLOAD_KIND_ABSENT,
};

struct TraceValue
{
    std::string from_node;
    std::string from_port;
    std::optional<std::string> to_node;
    std::optional<std::string> to_port;
    PayloadKind payload = PayloadKind::absent;
    /// Set when `payload` is `present`.
    Value value;
};

struct RunDiagnostic
{
    std::uint32_t issue = 0;  // a RippleRunIssue
    std::string message;
    std::optional<std::string> node;
    std::optional<std::string> port;
    std::optional<std::uint32_t> wires;
};

/// One event of the run's trace, read by `kind` as `RippleRunEvent` is.
struct RunEvent
{
    RunEventKind kind = RunEventKind::run_started;
    std::uint32_t run = 0, board = 0, sequence = 0;
    std::string node;
    std::uint32_t step = 0;
    std::string entry;
    std::vector<std::string> primed;
    std::optional<std::string> entered_via;
    bool pulled = false;
    std::vector<TraceValue> inputs;
    std::uint32_t outcome = 0;  // a RippleTurnOutcome
    std::vector<TraceValue> outputs;
    std::vector<std::string> flowed;
    std::optional<std::string> error;
    std::optional<RunDiagnostic> diagnostic;
    std::uint32_t level = 0;  // a RippleLogLevel
    std::string message;
    std::optional<Value> data;
    std::uint32_t steps = 0;
    bool cancelled = false;
    std::optional<std::string> failed_node;
};

/// A named callback the author declared — `campaign-host-api.md` §5b.
struct CallbackEvent
{
    /// The name the author gave it, which a handler is registered by.
    std::string callback;
    std::uint32_t event = 0;  // a RippleCallbackKind
    std::optional<std::uint32_t> board;
    std::optional<std::string> node;
    std::optional<std::string> name;
    std::optional<Value> from;
    std::optional<Value> to;
    std::optional<std::uint32_t> index;

    /// The event's name in the author's vocabulary: `cvar.changed`.
    std::string_view event_name() const noexcept
    {
        const char* named = ripple_callback_kind_name(event);
        return named ? named : "";
    }
};

struct Entity
{
    std::string key;
    std::optional<std::string> component;
    std::string label;
    /// A map, field by field.
    Value values;
};

struct Memory
{
    /// A map, node by node.
    Value nodes;
    std::vector<std::pair<std::string, std::string>> machines;
};

struct CvarSnapshot
{
    std::string name;
    std::string type;
    Value default_value;
    Value value;
};

enum class AudioKind : std::uint32_t
{
    play = RIPPLE_AUDIO_KIND_PLAY,
    stop = RIPPLE_AUDIO_KIND_STOP,
    cut = RIPPLE_AUDIO_KIND_CUT,
    cue = RIPPLE_AUDIO_KIND_CUE,
};

/// What the session asks the speaker — for `SoundscapeDriver::apply`.
struct AudioCommand
{
    AudioKind kind = AudioKind::cut;
    /// play: what to play.
    std::optional<Soundscape> soundscape;
    /// play: the fade-in; stop: the fade-out.
    std::uint64_t fade_ms = 0;
    /// cue: the medium, by index into the manifest's media.
    std::uint32_t media = 0;
    double volume = 1.0;
    double speed = 1.0;
    std::optional<std::string> node;
};

// The effects, one struct each, in the engine's order.
struct Trace
{
    RunEvent event;
};
struct Callback
{
    CallbackEvent event;
};
struct Jumped
{
    std::uint32_t board = 0;
    std::optional<std::string> node;
};
/// The soundscape now in force; none for silence.
struct AmbienceChanged
{
    std::optional<Ambience> ambience;
};
struct Audio
{
    AudioCommand command;
};
/// Which stores moved; a store that did not is absent.
struct Changed
{
    std::optional<std::vector<Entity>> entities;
    std::optional<Memory> memory;
    std::optional<std::vector<CvarSnapshot>> cvars;
};
/// A Turn: a passage, answered with `choose`.
struct Show
{
    Passage passage;
};
/// A Turn: a form, answered with `answer`.
struct Ask
{
    Prompt prompt;
};
/// A Turn: a wait, answered with `elapsed` once the host's clock says so.
struct Hold
{
    std::uint64_t ms = 0;
};
/// A Turn: the story is over.
struct Ended
{
    std::optional<std::string> title;
    /// True when it ran out of road rather than reaching an End.
    bool ran_out = false;
};
/// A Turn: the story could not go on.
struct Failed
{
    std::string message;
    std::optional<std::string> node;
};
/// A Turn: the session stopped.
struct Stopped
{
};
/// A Turn: the story has not begun, and reads these kinds that nobody has
/// answered. Answer each with `permit`; the last answer begins the story.
struct Permit
{
    std::vector<PermissionKind> kinds;
};
/// A read of the world outside the story that did not get the real value —
/// once per kind and outcome in a playthrough, before the Turn it was read for.
struct Notice
{
    /// `I.RT.05` a stand-in, `W.RT.06` a refusal, `I.RT.07` nothing; the
    /// letter is the severity.
    std::string code;
    PermissionKind kind = PermissionKind::date_time;
    /// The node whose Python read it.
    std::optional<std::string> node;
    /// The engine's English sentence.
    std::string message;
};

/// One effect. Apply a batch in order; the Turn comes last.
using Effect = std::variant<Trace, Callback, Jumped, AmbienceChanged, Audio, Changed, Show, Ask,
                            Hold, Ended, Failed, Stopped, Permit, Notice>;
using Effects = std::vector<Effect>;

/// Whether an effect is a Turn — where the story now is.
inline bool is_turn(const Effect& effect) noexcept
{
    return std::holds_alternative<Show>(effect) || std::holds_alternative<Ask>(effect) ||
           std::holds_alternative<Hold>(effect) || std::holds_alternative<Ended>(effect) ||
           std::holds_alternative<Failed>(effect) || std::holds_alternative<Stopped>(effect) ||
           std::holds_alternative<Permit>(effect);
}

/// Whether a Turn is the end of the story: ended, failed or stopped.
inline bool is_over(const Effect& effect) noexcept
{
    return std::holds_alternative<Ended>(effect) || std::holds_alternative<Failed>(effect) ||
           std::holds_alternative<Stopped>(effect);
}

/// What a script came to.
struct PythonFailure
{
    std::uint32_t kind = 0;  // a RipplePythonFailureKind
    std::optional<std::string> exception;
    std::string message;
    std::optional<std::uint32_t> line;
    std::optional<std::string> traceback;
};

struct PythonResult
{
    Value value;
    std::string output;
    std::optional<PythonFailure> error;
};

namespace detail
{

inline Passage passage(const RipplePassage& p)
{
    Passage out;
    out.node = str(p.node);
    out.title = str(p.title);
    out.text = str(p.text);
    out.choices =
        list<Choice, RippleChoice>(p.choices, p.choice_count, [](const RippleChoice& c)
                                   { return Choice{str(c.port), str(c.text), c.available}; });
    if (p.has_sources)
    {
        out.sources = list<LocaleText, RippleLocaleText>(
            p.sources, p.source_count,
            [](const RippleLocaleText& t) { return LocaleText{str(t.locale), str(t.text)}; });
    }
    out.snippets =
        list<Snippet, RippleSnippet>(p.snippets, p.snippet_count, [](const RippleSnippet& s)
                                     { return Snippet{str(s.code), s.is_inline}; });
    out.media =
        list<MediaLink, RippleMediaLink>(p.media, p.media_count, [](const RippleMediaLink& m)
                                         { return MediaLink{str(m.link), m.media}; });
    out.untranslated = opt(p.untranslated);
    out.soundscape = soundscape(p.soundscape);
    out.silence = p.silence;
    if (p.timer)
    {
        const RippleChoiceTimer& t = *p.timer;
        out.timer = ChoiceTimer{t.after_ms, t.timeout_ms, t.visible, t.default_choice};
    }
    return out;
}

inline PromptField prompt_field(const RipplePromptField& f)
{
    return PromptField{str(f.name),
                       str(f.type_name),
                       value(f.default_value),
                       str(f.label),
                       str(f.placeholder),
                       f.optional,
                       list<PromptOption, RipplePromptOption>(
                           f.options, f.option_count, [](const RipplePromptOption& o)
                           { return PromptOption{str(o.id), str(o.label), value(o.value)}; })};
}

inline Prompt prompt(const RipplePrompt& p)
{
    return Prompt{str(p.node), str(p.title),
                  list<PromptField, RipplePromptField>(p.fields, p.field_count, prompt_field)};
}

inline Ambience ambience(const RippleAmbience& a)
{
    return Ambience{soundscape(a.soundscape), a.board, str(a.node)};
}

inline std::vector<TraceValue> traced(const RippleTraceValue* items, std::size_t count)
{
    return list<TraceValue, RippleTraceValue>(
        items, count,
        [](const RippleTraceValue& t)
        {
            return TraceValue{str(t.from_node),
                              str(t.from_port),
                              opt(t.to_node),
                              opt(t.to_port),
                              static_cast<PayloadKind>(t.payload),
                              t.payload == RIPPLE_PAYLOAD_KIND_PRESENT ? value(t.value) : Value()};
        });
}

inline RunEvent run_event(const RippleRunEvent& e)
{
    RunEvent out;
    out.kind = static_cast<RunEventKind>(e.kind);
    out.run = e.at.run;
    out.board = e.at.board;
    out.sequence = e.at.sequence;
    out.node = str(e.node);
    out.step = e.step;
    out.entry = str(e.entry);
    out.primed = texts(e.primed, e.primed_count);
    out.entered_via = opt(e.entered_via);
    out.pulled = e.pulled;
    out.inputs = traced(e.inputs, e.input_count);
    out.outcome = e.outcome;
    out.outputs = traced(e.outputs, e.output_count);
    out.flowed = texts(e.flowed, e.flowed_count);
    out.error = opt(e.error);
    if (e.diagnostic)
    {
        const RippleRunDiagnostic& d = *e.diagnostic;
        out.diagnostic =
            RunDiagnostic{d.issue, str(d.message), opt(d.node), opt(d.port), opt(d.wires)};
    }
    out.level = e.level;
    out.message = str(e.message);
    out.data = value(e.data);
    out.steps = e.steps;
    out.cancelled = e.cancelled;
    out.failed_node = opt(e.failed_node);
    return out;
}

inline CallbackEvent callback(const RippleCallbackEvent& c)
{
    return CallbackEvent{str(c.callback), c.event,       opt(c.board), opt(c.node),
                         opt(c.name),     value(c.from), value(c.to),  opt(c.index)};
}

inline Entity entity(const RippleEntity& e)
{
    return Entity{str(e.key), opt(e.component), str(e.label), value(e.values)};
}

inline Memory memory(const RippleMemory& m)
{
    Memory out{value(m.nodes), {}};
    for (std::size_t i = 0; i < m.machine_count; ++i)
        out.machines.emplace_back(str(m.machines[i].machine), str(m.machines[i].state));
    return out;
}

inline CvarSnapshot cvar_snapshot(const RippleCvarSnapshot& c)
{
    return CvarSnapshot{str(c.name), str(c.type_name), value(c.default_value), value(c.value)};
}

inline AudioCommand audio(const RippleAudioCommand& a)
{
    return AudioCommand{static_cast<AudioKind>(a.kind),
                        soundscape(a.soundscape),
                        a.fade_ms,
                        a.media,
                        a.volume,
                        a.speed,
                        opt(a.node)};
}

inline Effect effect(const RippleEffect& e)
{
    switch (e.kind)
    {
        case RIPPLE_EFFECT_KIND_TRACE:
            return Trace{run_event(*e.data.trace)};
        case RIPPLE_EFFECT_KIND_CALLBACK:
            return Callback{callback(*e.data.callback)};
        case RIPPLE_EFFECT_KIND_JUMPED:
            return Jumped{e.data.jumped->board, opt(e.data.jumped->node)};
        case RIPPLE_EFFECT_KIND_AMBIENCE:
            return AmbienceChanged{e.data.ambience
                                       ? std::optional<Ambience>(ambience(*e.data.ambience))
                                       : std::nullopt};
        case RIPPLE_EFFECT_KIND_AUDIO:
            return Audio{audio(*e.data.audio)};
        case RIPPLE_EFFECT_KIND_CHANGED:
        {
            const RippleChanged& c = *e.data.changed;
            Changed out;
            if (c.has_entities)
                out.entities = list<Entity, RippleEntity>(c.entities, c.entity_count, entity);
            if (c.memory) out.memory = memory(*c.memory);
            if (c.has_cvars)
                out.cvars =
                    list<CvarSnapshot, RippleCvarSnapshot>(c.cvars, c.cvar_count, cvar_snapshot);
            return out;
        }
        case RIPPLE_EFFECT_KIND_SHOW:
            return Show{passage(*e.data.show)};
        case RIPPLE_EFFECT_KIND_ASK:
            return Ask{prompt(*e.data.ask)};
        case RIPPLE_EFFECT_KIND_HOLD:
            return Hold{e.data.hold_ms};
        case RIPPLE_EFFECT_KIND_ENDED:
            return Ended{opt(e.data.ended->title), e.data.ended->ran_out};
        case RIPPLE_EFFECT_KIND_FAILED:
            return Failed{str(e.data.failed->message), opt(e.data.failed->node)};
        case RIPPLE_EFFECT_KIND_PERMIT:
        {
            const RipplePermit& p = *e.data.permit;
            Permit out;
            out.kinds.reserve(p.kind_count);
            for (std::size_t i = 0; i < p.kind_count; ++i)
                out.kinds.push_back(static_cast<PermissionKind>(p.kinds[i]));
            return out;
        }
        case RIPPLE_EFFECT_KIND_NOTICE:
        {
            const RippleNotice& n = *e.data.notice;
            return Notice{str(n.code), static_cast<PermissionKind>(n.kind), opt(n.node),
                          str(n.message)};
        }
        case RIPPLE_EFFECT_KIND_STOPPED:
        default:
            return Stopped{};
    }
}

inline Effects effects(const RippleEffect* items, std::size_t count)
{
    return list<Effect, RippleEffect>(items, count, effect);
}

inline PythonResult python_result(const RipplePythonResult& r)
{
    PythonResult out{value(r.value), str(r.output), std::nullopt};
    if (r.error)
    {
        const RipplePythonFailure& f = *r.error;
        out.error =
            PythonFailure{f.kind, opt(f.exception), str(f.message), opt(f.line), opt(f.traceback)};
    }
    return out;
}

/// Calls `call(&out, &error)` and reads the owned answer with `read`.
template <class T, class View, class Call, class Read>
Returns<T> owned(Call&& call, Read&& read)
{
    const View* out = nullptr;
    const RippleError* error = nullptr;
    const RippleResult code = call(&out, &error);
    return finish<T>(code, error,
                     [&]() -> T
                     {
                         Owned<View> held(out);
                         return read(held.get());
                     });
}

/// Calls `call(&out, &error)` for a plain value.
template <class T, class Call>
Returns<T> plain(Call&& call)
{
    T out{};
    const RippleError* error = nullptr;
    const RippleResult code = call(&out, &error);
    return finish<T>(code, error, [&] { return out; });
}

}  // namespace detail

////////////////////////////////////////////////////////////////////////////////
// The campaign

/// One file of a bundle held in memory, by its path inside the bundle.
struct BundleFile
{
    std::string path;
    std::vector<std::uint8_t> bytes;
};

/// A bundle, opened. Shared by every session made from it; it may go before
/// they do.
class Campaign
{
  public:
    /// Opens the bundle in the folder that holds `campaign.json`.
    static Returns<Campaign> open_dir(const std::string& folder)
    {
        RippleCampaign* handle = nullptr;
        const RippleError* error = nullptr;
        const RippleResult code = ripple_campaign_open_dir(detail::span(folder), &handle, &error);
        return detail::finish<Campaign>(code, error, [&] { return Campaign(handle, folder); });
    }

    /// Opens a bundle held in memory. The bytes are copied.
    static Returns<Campaign> open_files(const std::vector<BundleFile>& files)
    {
        std::vector<RippleFile> table;
        table.reserve(files.size());
        for (const BundleFile& file : files)
            table.push_back(
                RippleFile{detail::span(file.path), file.bytes.data(), file.bytes.size()});
        RippleCampaign* handle = nullptr;
        const RippleError* error = nullptr;
        const RippleResult code =
            ripple_campaign_open_memory(table.data(), table.size(), &handle, &error);
        return detail::finish<Campaign>(code, error,
                                        [&] { return Campaign(handle, std::nullopt); });
    }

    RippleCampaign* handle() const noexcept { return handle_.get(); }

    /// The folder it was opened from; none for a bundle in memory.
    const std::optional<std::string>& folder() const noexcept { return folder_; }

    Returns<Manifest> manifest() const
    {
        return detail::owned<Manifest, RippleManifest>(
            [&](auto out, auto error) { return ripple_campaign_manifest(handle(), out, error); },
            [](const RippleManifest* m) { return detail::manifest(*m); });
    }

    Returns<Board> board(std::uint32_t index) const
    {
        return detail::owned<Board, RippleBoard>(
            [&](auto out, auto error)
            { return ripple_campaign_board(handle(), index, out, error); },
            [](const RippleBoard* b) { return detail::board(*b); });
    }

    Returns<Soundscape> soundscape(std::uint32_t index) const
    {
        return detail::owned<Soundscape, RippleSoundscape>(
            [&](auto out, auto error)
            { return ripple_campaign_soundscape(handle(), index, out, error); },
            [](const RippleSoundscape* s) { return detail::soundscape(*s); });
    }

    Returns<std::optional<std::uint32_t>> board_with_id(std::string_view id) const
    {
        RippleOptionalU32 out{};
        const RippleError* error = nullptr;
        const RippleResult code =
            ripple_campaign_board_with_id(handle(), detail::span(id), &out, &error);
        return detail::finish<std::optional<std::uint32_t>>(code, error,
                                                            [&] { return detail::opt(out); });
    }

    Returns<std::optional<Node>> node(std::uint32_t board, std::string_view id) const
    {
        return detail::owned<std::optional<Node>, RippleNode>(
            [&](auto out, auto error)
            { return ripple_campaign_node(handle(), board, detail::span(id), out, error); },
            [](const RippleNode* n)
            { return n ? std::optional<Node>(detail::node(*n)) : std::nullopt; });
    }

    Returns<std::vector<BundleProblem>> validate() const
    {
        return detail::owned<std::vector<BundleProblem>, RippleBundleProblems>(
            [&](auto out, auto error) { return ripple_campaign_validate(handle(), out, error); },
            [](const RippleBundleProblems* p)
            {
                return detail::list<BundleProblem, RippleBundleProblem>(
                    p->items, p->count, [](const RippleBundleProblem& one)
                    { return BundleProblem{detail::str(one.message), detail::str(one.location)}; });
            });
    }

    /// A medium's path inside the bundle; none when there is no such medium.
    Returns<std::optional<std::string>> media_path(std::uint32_t index) const
    {
        return detail::owned<std::optional<std::string>, RippleStr>(
            [&](auto out, auto error)
            { return ripple_campaign_media_path(handle(), index, out, error); },
            [](const RippleStr* s)
            { return s ? std::optional<std::string>(detail::str(*s)) : std::nullopt; });
    }

    /// A medium's file on disk — the folder joined with its path — for a
    /// campaign opened from a folder; none otherwise.
    std::optional<std::string> media_file(std::uint32_t index) const
    {
        if (!folder_) return std::nullopt;
        const RippleStr* out = nullptr;
        if (ripple_campaign_media_path(handle(), index, &out, nullptr) != RIPPLE_SUCCESS || !out)
            return std::nullopt;
        detail::Owned<RippleStr> held(out);
        std::string file = *folder_;
        if (!file.empty() && file.back() != '/' && file.back() != '\\') file += '/';
        file += detail::str(*held);
        return file;
    }

    Returns<std::optional<std::vector<std::uint8_t>>> media_bytes(std::uint32_t index) const
    {
        return detail::owned<std::optional<std::vector<std::uint8_t>>, RippleBytes>(
            [&](auto out, auto error)
            { return ripple_campaign_media_bytes(handle(), index, out, error); },
            [](const RippleBytes* b)
            {
                return b ? std::optional<std::vector<std::uint8_t>>(
                               std::vector<std::uint8_t>(b->ptr, b->ptr + b->len))
                         : std::nullopt;
            });
    }

  private:
    struct Destroy
    {
        void operator()(RippleCampaign* handle) const noexcept { ripple_campaign_destroy(handle); }
    };
    Campaign(RippleCampaign* handle, std::optional<std::string> folder)
        : handle_(handle), folder_(std::move(folder))
    {
    }
    std::unique_ptr<RippleCampaign, Destroy> handle_;
    std::optional<std::string> folder_;
};

////////////////////////////////////////////////////////////////////////////////
// The session

enum class Untranslated : std::uint32_t
{
    report = RIPPLE_UNTRANSLATED_REPORT,
    fall_back = RIPPLE_UNTRANSLATED_FALL_BACK,
};

/// The engine's budgets and policies; `SessionOptions{}` is the engine's own.
struct SessionOptions
{
    std::uint32_t step_budget;
    std::uint32_t silent_jump_limit;
    std::uint32_t loop_limit;
    std::uint64_t script_deadline_ms;
    std::uint64_t script_memory_bytes;
    std::uint32_t script_recursion_depth;
    Untranslated untranslated = Untranslated::report;
    bool trace = false;
    bool trace_payloads = false;

    SessionOptions()
    {
        RippleSessionOptions given{};
        ripple_session_options_default(&given);
        step_budget = given.step_budget;
        silent_jump_limit = given.silent_jump_limit;
        loop_limit = given.loop_limit;
        script_deadline_ms = given.script_limits.deadline_ms;
        script_memory_bytes = given.script_limits.memory_bytes;
        script_recursion_depth = given.script_limits.recursion_depth;
    }

    RippleSessionOptions raw() const noexcept
    {
        return RippleSessionOptions{
            step_budget,
            silent_jump_limit,
            loop_limit,
            RippleScriptLimits{script_deadline_ms, script_memory_bytes, script_recursion_depth},
            static_cast<std::uint32_t>(untranslated),
            trace,
            trace_payloads};
    }
};

/// Where a new session begins.
struct SessionConfig
{
    /// The board, by index into the manifest's boards: usually `entries[0]`.
    std::uint32_t board = 0;
    std::optional<std::string> entry_node;
    std::optional<std::string> locale;
    std::uint64_t seed = 0;
    std::optional<std::string> digest;
    SessionOptions options;
};

struct Paused
{
    /// The save, as JSON text; none when there was nothing to save.
    std::optional<std::string> save;
    Effects effects;
};

enum class CvarWrite : std::uint32_t
{
    done = RIPPLE_CVAR_WRITE_DONE,
    unknown = RIPPLE_CVAR_WRITE_UNKNOWN,
    refused = RIPPLE_CVAR_WRITE_REFUSED,
};

struct CvarChange
{
    CvarWrite outcome = CvarWrite::done;
    std::optional<std::string> refusal;
    Effects effects;
};

struct EntityRemoval
{
    bool removed = false;
    Effects effects;
};

struct ConsoleResult
{
    PythonResult result;
    Effects effects;
};

struct GuardReport
{
    std::vector<std::string> abandoned;
    std::uint32_t refused = 0;
    bool exhausted = false;
};

/// The dice's state, carried across a resume by a host that wants the dice
/// to run on.
struct Generator
{
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;
};

/// The choice that means the reader is leaving the passage.
inline constexpr std::optional<std::uint32_t> leave = std::nullopt;

/// One story being played. Every verb answers the effects it led to, in the
/// order they are applied, the Turn last. One caller at a time.
class Session
{
  public:
    static Returns<Session> create(const Campaign& campaign, const SessionConfig& config)
    {
        const RippleSessionConfig raw{
            config.board, detail::maybe_span(config.entry_node), detail::maybe_span(config.locale),
            config.seed,  detail::maybe_span(config.digest),     config.options.raw()};
        RippleSession* handle = nullptr;
        const RippleError* error = nullptr;
        const RippleResult code = ripple_session_create(campaign.handle(), &raw, &handle, &error);
        return detail::finish<Session>(code, error, [&] { return Session(handle); });
    }

    /// A session picking up a save; `start` it next.
    static Returns<Session> resume(const Campaign& campaign, std::string_view save,
                                   std::uint64_t seed,
                                   const SessionOptions& options = SessionOptions())
    {
        const RippleSessionOptions raw = options.raw();
        RippleSession* handle = nullptr;
        const RippleError* error = nullptr;
        const RippleResult code = ripple_session_resume(campaign.handle(), detail::span(save), seed,
                                                        &raw, &handle, &error);
        return detail::finish<Session>(code, error, [&] { return Session(handle); });
    }

    RippleSession* handle() const noexcept { return handle_.get(); }

    Returns<Effects> start()
    {
        return batch([&](auto out, auto error)
                     { return ripple_session_start(handle(), out, error); });
    }

    /// Takes the choice at `index` among those offered — or, with `leave`,
    /// the reader leaves the passage.
    Returns<Effects> choose(std::optional<std::uint32_t> index)
    {
        const std::uint32_t choice = index.value_or(RIPPLE_CHOICE_NONE);
        return batch([&](auto out, auto error)
                     { return ripple_session_choose(handle(), choice, out, error); });
    }

    /// Answers the form showing — or, with none, the reader leaves it.
    Returns<Effects> answer(const std::optional<Value::Map>& values)
    {
        if (!values)
            return batch([&](auto out, auto error)
                         { return ripple_session_answer(handle(), nullptr, out, error); });
        const detail::FieldArena fields(*values);
        const RippleFields view = fields.view();
        return batch([&](auto out, auto error)
                     { return ripple_session_answer(handle(), &view, out, error); });
    }

    /// Answers a wait, once the host's clock says it is over.
    Returns<Effects> elapsed()
    {
        return batch([&](auto out, auto error)
                     { return ripple_session_elapsed(handle(), out, error); });
    }

    /// The host's clock says the shown passage's countdown begins — after its
    /// timer's `after_ms`. Moves nothing: the effects are only the callbacks an
    /// author aimed at it, and empty when there are none or it already began.
    Returns<Effects> begin_countdown()
    {
        return batch([&](auto out, auto error)
                     { return ripple_session_begin_countdown(handle(), out, error); });
    }

    /// The host's clock says the countdown ran out: the passage's default is
    /// taken as `choose` would take it. Refused with `countdown_not_begun`
    /// before `begin_countdown`, and `no_countdown` on a passage with none.
    Returns<Effects> expire()
    {
        return batch([&](auto out, auto error)
                     { return ripple_session_expire(handle(), out, error); });
    }

    /// The host's answer for one kind, at any time — before `start` included,
    /// which is how a host that already knows its reader's answers never has
    /// them asked. On a `Permit` turn, the last kind answered begins the story.
    Returns<Effects> permit(PermissionKind kind, bool granted)
    {
        return batch(
            [&](auto out, auto error) {
                return ripple_session_permit(handle(), static_cast<std::uint32_t>(kind), granted,
                                             out, error);
            });
    }

    /// What time it is, from the host — none for no clock. Before every verb
    /// that can step, by a host that granted `date_time`; within one verb
    /// time does not move.
    Returns<void> set_now(std::optional<Moment> now)
    {
        const RippleMoment raw = detail::moment(now.value_or(Moment{}));
        const RippleError* error = nullptr;
        const RippleResult code = ripple_session_set_now(handle(), now ? &raw : nullptr, &error);
        return detail::finish<void>(code, error, [] {});
    }

    /// The host's last position fix — none for none. Refused with
    /// `not_permitted` once `location` is denied; a fix given before the
    /// answer is kept, and read only if it is yes.
    Returns<void> set_position(const std::optional<Position>& fix)
    {
        const RippleError* error = nullptr;
        if (!fix)
        {
            const RippleResult code = ripple_session_set_position(handle(), nullptr, &error);
            return detail::finish<void>(code, error, [] {});
        }
        const auto real = [](const std::optional<double>& one)
        { return RippleOptionalF64{one.has_value(), one.value_or(0.0)}; };
        const RippleMoment when = detail::moment(fix->when.value_or(Moment{}));
        const RipplePosition raw{detail::maybe_span(fix->label), real(fix->latitude),
                                 real(fix->longitude), real(fix->accuracy),
                                 fix->when ? &when : nullptr};
        const RippleResult code = ripple_session_set_position(handle(), &raw, &error);
        return detail::finish<void>(code, error, [] {});
    }

    /// The reader's stand-in for one kind, as they typed it — none takes it
    /// away: ISO 8601 for the clock, read at `local_offset_minutes` when it
    /// carries no offset; `"lat, lon"` or a place's name for a position.
    /// Answered whenever the real value is missing. Refused with `stand_in`,
    /// saying why, for one that does not read.
    Returns<void> set_stand_in(PermissionKind kind, std::optional<std::string_view> typed,
                               std::int32_t local_offset_minutes = 0)
    {
        const RippleStr raw = typed ? detail::span(*typed) : RippleStr{nullptr, 0};
        const RippleError* error = nullptr;
        const RippleResult code = ripple_session_set_stand_in(
            handle(), static_cast<std::uint32_t>(kind), raw, local_offset_minutes, &error);
        return detail::finish<void>(code, error, [] {});
    }

    /// Changes the language the story reads in. The Turn it is on is answered
    /// again in it: a passage re-rendered with its countdown kept, a form
    /// re-worded with its field names unchanged.
    Returns<Effects> set_locale(std::string_view locale)
    {
        return batch(
            [&](auto out, auto error)
            { return ripple_session_set_locale(handle(), detail::span(locale), out, error); });
    }

    Returns<Effects> restart()
    {
        return batch([&](auto out, auto error)
                     { return ripple_session_restart(handle(), out, error); });
    }
    Returns<Effects> stop()
    {
        return batch([&](auto out, auto error)
                     { return ripple_session_stop(handle(), out, error); });
    }

    /// Puts the story down: its save, and what pausing did.
    Returns<Paused> pause()
    {
        return detail::owned<Paused, RipplePaused>(
            [&](auto out, auto error) { return ripple_session_pause(handle(), out, error); },
            [](const RipplePaused* p)
            { return Paused{detail::opt(p->save), detail::effects(p->effects, p->effect_count)}; });
    }

    /// A save of the turn the story is on, without stopping it.
    Returns<std::optional<std::string>> checkpoint()
    {
        return text([&](auto out, auto error)
                    { return ripple_session_checkpoint(handle(), out, error); });
    }

    Returns<ConsoleResult> console_feed(std::string_view code)
    {
        return detail::owned<ConsoleResult, RippleConsoleResult>(
            [&](auto out, auto error)
            { return ripple_session_console_feed(handle(), detail::span(code), out, error); },
            [](const RippleConsoleResult* r)
            {
                return ConsoleResult{detail::python_result(r->result),
                                     detail::effects(r->effects, r->effect_count)};
            });
    }

    Returns<CvarChange> write_cvar(std::string_view name, const Value& value)
    {
        const detail::Arena arena(value);
        const RippleValue view = arena.view();
        return cvar_change(
            [&](auto out, auto error)
            { return ripple_session_write_cvar(handle(), detail::span(name), &view, out, error); });
    }

    Returns<CvarChange> reset_cvar(std::string_view name)
    {
        return cvar_change(
            [&](auto out, auto error)
            { return ripple_session_reset_cvar(handle(), detail::span(name), out, error); });
    }

    Returns<Effects> reset_all_cvars()
    {
        return batch([&](auto out, auto error)
                     { return ripple_session_reset_all_cvars(handle(), out, error); });
    }

    Returns<EntityRemoval> remove_entity(std::string_view key)
    {
        return detail::owned<EntityRemoval, RippleEntityRemoval>(
            [&](auto out, auto error)
            { return ripple_session_remove_entity(handle(), detail::span(key), out, error); },
            [](const RippleEntityRemoval* r)
            { return EntityRemoval{r->removed, detail::effects(r->effects, r->effect_count)}; });
    }

    // The readers.
    Returns<std::uint32_t> board() const
    {
        return detail::plain<std::uint32_t>([&](auto out, auto error)
                                            { return ripple_session_board(handle(), out, error); });
    }
    Returns<std::string> locale() const
    {
        return detail::owned<std::string, RippleStr>(
            [&](auto out, auto error) { return ripple_session_locale(handle(), out, error); },
            [](const RippleStr* s) { return detail::str(*s); });
    }
    Returns<std::string> default_locale() const
    {
        return detail::owned<std::string, RippleStr>(
            [&](auto out, auto error)
            { return ripple_session_default_locale(handle(), out, error); },
            [](const RippleStr* s) { return detail::str(*s); });
    }
    Returns<std::optional<Ambience>> ambience() const
    {
        return detail::owned<std::optional<Ambience>, RippleAmbience>(
            [&](auto out, auto error) { return ripple_session_ambience(handle(), out, error); },
            [](const RippleAmbience* a)
            { return a ? std::optional<Ambience>(detail::ambience(*a)) : std::nullopt; });
    }
    Returns<std::vector<Entity>> entities() const
    {
        return detail::owned<std::vector<Entity>, RippleEntityList>(
            [&](auto out, auto error) { return ripple_session_entities(handle(), out, error); },
            [](const RippleEntityList* l)
            { return detail::list<Entity, RippleEntity>(l->items, l->count, detail::entity); });
    }
    Returns<Memory> memory() const
    {
        return detail::owned<Memory, RippleMemory>(
            [&](auto out, auto error) { return ripple_session_memory(handle(), out, error); },
            [](const RippleMemory* m) { return detail::memory(*m); });
    }
    Returns<std::vector<CvarSnapshot>> cvars() const
    {
        return detail::owned<std::vector<CvarSnapshot>, RippleCvarList>(
            [&](auto out, auto error) { return ripple_session_cvars(handle(), out, error); },
            [](const RippleCvarList* l)
            {
                return detail::list<CvarSnapshot, RippleCvarSnapshot>(l->items, l->count,
                                                                      detail::cvar_snapshot);
            });
    }
    Returns<GuardReport> guard() const
    {
        return detail::owned<GuardReport, RippleGuardReport>(
            [&](auto out, auto error) { return ripple_session_guard(handle(), out, error); },
            [](const RippleGuardReport* g) {
                return GuardReport{detail::texts(g->abandoned, g->abandoned_count), g->refused,
                                   g->exhausted};
            });
    }
    Returns<bool> python_available() const
    {
        return detail::plain<bool>(
            [&](auto out, auto error)
            { return ripple_session_python_available(handle(), out, error); });
    }
    Returns<std::optional<std::string>> describe_cvar_write(std::string_view name,
                                                            const Value& value) const
    {
        const detail::Arena arena(value);
        const RippleValue view = arena.view();
        return text(
            [&](auto out, auto error) {
                return ripple_session_describe_cvar_write(handle(), detail::span(name), &view, out,
                                                          error);
            });
    }
    Returns<void> set_trace(bool enabled)
    {
        const RippleError* error = nullptr;
        const RippleResult code = ripple_session_set_trace(handle(), enabled, &error);
        return detail::finish<void>(code, error, [] {});
    }
    Returns<void> set_trace_payloads(bool enabled)
    {
        const RippleError* error = nullptr;
        const RippleResult code = ripple_session_set_trace_payloads(handle(), enabled, &error);
        return detail::finish<void>(code, error, [] {});
    }
    Returns<Generator> generator() const
    {
        RippleGenerator out{};
        const RippleError* error = nullptr;
        const RippleResult code = ripple_session_generator(handle(), &out, &error);
        return detail::finish<Generator>(code, error, [&] { return Generator{out.lo, out.hi}; });
    }
    Returns<void> set_generator(Generator state)
    {
        const RippleError* error = nullptr;
        const RippleResult code =
            ripple_session_set_generator(handle(), RippleGenerator{state.lo, state.hi}, &error);
        return detail::finish<void>(code, error, [] {});
    }

  private:
    struct Destroy
    {
        void operator()(RippleSession* handle) const noexcept { ripple_session_destroy(handle); }
    };
    explicit Session(RippleSession* handle) : handle_(handle) {}

    template <class Call>
    static Returns<Effects> batch(Call&& call)
    {
        return detail::owned<Effects, RippleEffects>(
            std::forward<Call>(call),
            [](const RippleEffects* b) { return detail::effects(b->items, b->count); });
    }

    template <class Call>
    static Returns<std::optional<std::string>> text(Call&& call)
    {
        return detail::owned<std::optional<std::string>, RippleStr>(
            std::forward<Call>(call), [](const RippleStr* s)
            { return s ? std::optional<std::string>(detail::str(*s)) : std::nullopt; });
    }

    template <class Call>
    static Returns<CvarChange> cvar_change(Call&& call)
    {
        return detail::owned<CvarChange, RippleCvarChange>(
            std::forward<Call>(call),
            [](const RippleCvarChange* c)
            {
                return CvarChange{static_cast<CvarWrite>(c->outcome), detail::opt(c->refusal),
                                  detail::effects(c->effects, c->effect_count)};
            });
    }

    std::unique_ptr<RippleSession, Destroy> handle_;
};

////////////////////////////////////////////////////////////////////////////////
// The mixer

enum class VoiceKind : std::uint32_t
{
    open = RIPPLE_VOICE_KIND_OPEN,
    gain = RIPPLE_VOICE_KIND_GAIN,
    stop = RIPPLE_VOICE_KIND_STOP,
    pause = RIPPLE_VOICE_KIND_PAUSE,
    resume = RIPPLE_VOICE_KIND_RESUME,
};

/// Why a voice is open: a soundscape's track, usually long and better
/// streamed, or a cue, short and better decoded whole.
enum class VoiceRole : std::uint32_t
{
    track = RIPPLE_VOICE_ROLE_TRACK,
    cue = RIPPLE_VOICE_ROLE_CUE,
};

enum class Phase : std::uint32_t
{
    queued = RIPPLE_PHASE_QUEUED,
    starting = RIPPLE_PHASE_STARTING,
    playing = RIPPLE_PHASE_PLAYING,
    finishing = RIPPLE_PHASE_FINISHING,
    finished = RIPPLE_PHASE_FINISHED,
};

/// One command for the speaker, read by `kind`.
struct VoiceCommand
{
    VoiceKind kind = VoiceKind::stop;
    std::uint64_t voice = 0;
    std::uint32_t media = 0;
    std::uint64_t from_ms = 0;
    std::optional<std::uint64_t> to_ms;
    double gain = 0.0;
    double rate = 1.0;
    bool looping = false;
    std::uint64_t loop_from_ms = 0;
    VoiceRole role = VoiceRole::track;
    std::uint32_t track = 0;
};
using VoiceCommands = std::vector<VoiceCommand>;

/// The engine's soundscape scheduler, raw: commands in, commands out, the
/// host's clock on every verb. `SoundscapeDriver` drives one for a host that
/// has a `Speaker`.
class Mixer
{
  public:
    static Returns<Mixer> create(std::uint32_t max_cues = RIPPLE_MIXER_DEFAULT_MAX_CUES,
                                 double gain_step = RIPPLE_MIXER_DEFAULT_GAIN_STEP)
    {
        RippleMixer* handle = nullptr;
        const RippleError* error = nullptr;
        const RippleResult code = ripple_mixer_create(max_cues, gain_step, &handle, &error);
        return detail::finish<Mixer>(code, error, [&] { return Mixer(handle); });
    }

    RippleMixer* handle() const noexcept { return handle_.get(); }

    Returns<VoiceCommands> play(std::uint64_t now_ms, const Soundscape& scape,
                                const std::vector<std::uint32_t>& unresolved = {},
                                std::uint64_t at_ms = 0, bool muted = false)
    {
        const detail::SoundscapeView view(scape);
        return commands(
            [&](auto out, auto error)
            {
                return ripple_mixer_play(handle(), now_ms, view.get(), unresolved.data(),
                                         unresolved.size(), at_ms, muted, out, error);
            });
    }
    Returns<VoiceCommands> stop(std::uint64_t now_ms)
    {
        return commands([&](auto out, auto error)
                        { return ripple_mixer_stop(handle(), now_ms, out, error); });
    }
    Returns<VoiceCommands> cut(std::uint64_t now_ms)
    {
        return commands([&](auto out, auto error)
                        { return ripple_mixer_cut(handle(), now_ms, out, error); });
    }
    Returns<VoiceCommands> cue(std::uint64_t now_ms, std::uint32_t media, double volume,
                               double speed)
    {
        return commands(
            [&](auto out, auto error)
            { return ripple_mixer_cue(handle(), now_ms, media, volume, speed, out, error); });
    }
    Returns<VoiceCommands> pause(std::uint64_t now_ms)
    {
        return commands([&](auto out, auto error)
                        { return ripple_mixer_pause(handle(), now_ms, out, error); });
    }
    Returns<VoiceCommands> resume(std::uint64_t now_ms)
    {
        return commands([&](auto out, auto error)
                        { return ripple_mixer_resume(handle(), now_ms, out, error); });
    }
    Returns<VoiceCommands> tick(std::uint64_t now_ms)
    {
        return commands([&](auto out, auto error)
                        { return ripple_mixer_tick(handle(), now_ms, out, error); });
    }
    Returns<VoiceCommands> voice_ended(std::uint64_t now_ms, std::uint64_t voice)
    {
        return commands([&](auto out, auto error)
                        { return ripple_mixer_voice_ended(handle(), now_ms, voice, out, error); });
    }
    Returns<void> duration_known(std::uint64_t voice, std::uint64_t ms)
    {
        const RippleError* error = nullptr;
        const RippleResult code = ripple_mixer_duration_known(handle(), voice, ms, &error);
        return detail::finish<void>(code, error, [] {});
    }
    Returns<void> set_muted(bool muted)
    {
        const RippleError* error = nullptr;
        const RippleResult code = ripple_mixer_set_muted(handle(), muted, &error);
        return detail::finish<void>(code, error, [] {});
    }

    Returns<std::optional<std::string>> playing_id() const
    {
        return id([&](auto out, auto error)
                  { return ripple_mixer_playing_id(handle(), out, error); });
    }
    Returns<std::optional<std::string>> finishing_id() const
    {
        return id([&](auto out, auto error)
                  { return ripple_mixer_finishing_id(handle(), out, error); });
    }
    Returns<std::optional<Phase>> state() const
    {
        RippleOptionalU32 out{};
        const RippleError* error = nullptr;
        const RippleResult code = ripple_mixer_state(handle(), &out, &error);
        return detail::finish<std::optional<Phase>>(
            code, error,
            [&] {
                return out.present ? std::optional<Phase>(static_cast<Phase>(out.value))
                                   : std::nullopt;
            });
    }
    Returns<Phase> track_state(std::uint32_t track) const
    {
        std::uint32_t out = 0;
        const RippleError* error = nullptr;
        const RippleResult code = ripple_mixer_track_state(handle(), track, &out, &error);
        return detail::finish<Phase>(code, error, [&] { return static_cast<Phase>(out); });
    }
    Returns<std::uint64_t> position_ms() const
    {
        return detail::plain<std::uint64_t>(
            [&](auto out, auto error) { return ripple_mixer_position_ms(handle(), out, error); });
    }
    Returns<std::uint32_t> pass() const
    {
        return detail::plain<std::uint32_t>([&](auto out, auto error)
                                            { return ripple_mixer_pass(handle(), out, error); });
    }
    Returns<bool> ran_out() const
    {
        return detail::plain<bool>([&](auto out, auto error)
                                   { return ripple_mixer_ran_out(handle(), out, error); });
    }
    Returns<bool> is_paused() const
    {
        return detail::plain<bool>([&](auto out, auto error)
                                   { return ripple_mixer_is_paused(handle(), out, error); });
    }
    Returns<bool> muted() const
    {
        return detail::plain<bool>([&](auto out, auto error)
                                   { return ripple_mixer_muted(handle(), out, error); });
    }
    Returns<bool> is_idle() const
    {
        return detail::plain<bool>([&](auto out, auto error)
                                   { return ripple_mixer_is_idle(handle(), out, error); });
    }

  private:
    struct Destroy
    {
        void operator()(RippleMixer* handle) const noexcept { ripple_mixer_destroy(handle); }
    };
    explicit Mixer(RippleMixer* handle) : handle_(handle) {}

    static VoiceCommand command(const RippleVoiceCommand& c)
    {
        return VoiceCommand{static_cast<VoiceKind>(c.kind),
                            c.voice,
                            c.media,
                            c.from_ms,
                            detail::opt(c.to_ms),
                            c.gain,
                            c.rate,
                            c.looping,
                            c.loop_from_ms,
                            static_cast<VoiceRole>(c.role),
                            c.track};
    }

    template <class Call>
    static Returns<VoiceCommands> commands(Call&& call)
    {
        return detail::owned<VoiceCommands, RippleVoiceCommands>(
            std::forward<Call>(call),
            [](const RippleVoiceCommands* c) {
                return detail::list<VoiceCommand, RippleVoiceCommand>(c->items, c->count, command);
            });
    }

    template <class Call>
    static Returns<std::optional<std::string>> id(Call&& call)
    {
        return detail::owned<std::optional<std::string>, RippleStr>(
            std::forward<Call>(call), [](const RippleStr* s)
            { return s ? std::optional<std::string>(detail::str(*s)) : std::nullopt; });
    }

    std::unique_ptr<RippleMixer, Destroy> handle_;
};

////////////////////////////////////////////////////////////////////////////////
// The speaker: what a host's audio implements

/// What a voice is to play, in milliseconds of the file and the gain scale.
struct VoicePlay
{
    /// Where in the file to begin.
    double from_ms = 0.0;
    /// Where to stop; the file's end when absent.
    std::optional<double> to_ms;
    /// How loud, 0 to 1, on the engine's **cubic** scale: a speaker writing
    /// amplitude writes `gain * gain * gain`.
    double gain = 1.0;
    /// Playback speed, 1 being as recorded.
    double rate = 1.0;
    /// Whether reaching `to_ms` comes round again — to `loop_from_ms`.
    bool looping = false;
    double loop_from_ms = 0.0;
};

/// What a voice reports back, which only it can know. Thread-safe: an audio
/// thread may call either at any time, and the driver hears it on its next
/// `update()`. A call after the voice was stopped is ignored.
class VoiceEvents
{
  public:
    VoiceEvents() = default;
    /// The voice reached the end of what it was given — once per play that
    /// does not loop, and never after `stop()`.
    void ended() const
    {
        if (auto inbox = inbox_.lock()) inbox->post(Report{voice_, generation_, false, 0.0});
    }
    /// The file's length, once its decoder knows it.
    void duration(double ms) const
    {
        if (auto inbox = inbox_.lock()) inbox->post(Report{voice_, generation_, true, ms});
    }

  private:
    friend class SoundscapeDriver;
    struct Report
    {
        std::uint64_t voice;
        std::uint64_t generation;
        bool is_duration;
        double ms;
    };
    struct Inbox
    {
        std::mutex lock;
        std::vector<Report> reports;
        void post(Report report)
        {
            const std::lock_guard<std::mutex> held(lock);
            reports.push_back(report);
        }
    };
    VoiceEvents(std::weak_ptr<Inbox> inbox, std::uint64_t voice, std::uint64_t generation)
        : inbox_(std::move(inbox)), voice_(voice), generation_(generation)
    {
    }
    std::weak_ptr<Inbox> inbox_;
    std::uint64_t voice_ = 0;
    std::uint64_t generation_ = 0;
};

/// One play of one file.
class Voice
{
  public:
    virtual ~Voice() = default;
    /// Begins at once, replacing what the voice was playing. Called again on
    /// the same voice for a gapless bed whose file ended and comes round.
    virtual void play(const VoicePlay& request) = 0;
    /// A new gain on the same cubic scale; smooth it over a few milliseconds.
    virtual void set_gain(double gain) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    /// Ends the voice for good. No `ended` follows.
    virtual void stop() = 0;
};

/// The file a voice is for.
struct VoiceSource
{
    /// The medium, by index into the manifest's media.
    std::uint32_t media = 0;
    /// Its path inside the bundle.
    std::string path;
    /// The file on disk, for a campaign opened from a folder.
    std::optional<std::string> file;
};

/// What a host's audio implements: voices, opened on request.
class Speaker
{
  public:
    virtual ~Speaker() = default;
    /// A voice for `source`, returned once it can begin at once — or nullptr
    /// when the file cannot be played, which is heard as silence and never
    /// retried. `events` is how the voice reports its end and its length;
    /// keep it with the voice.
    virtual std::unique_ptr<Voice> open(const VoiceSource& source, VoiceRole role,
                                        VoiceEvents events) = 0;
    /// Whether the speaker can play the medium at all; a track it cannot is
    /// told to the scheduler as silence up front. Yes unless overridden.
    virtual bool resolves(const VoiceSource& /*source*/) { return true; }
};

/// The clock the driver and the player read: milliseconds, monotonic.
using Clock = std::function<std::uint64_t()>;

inline Clock steady_clock()
{
    const auto origin = std::chrono::steady_clock::now();
    return [origin]
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - origin)
                                              .count());
    };
}

struct SoundscapeOptions
{
    std::uint32_t max_cues = RIPPLE_MIXER_DEFAULT_MAX_CUES;
    double gain_step = RIPPLE_MIXER_DEFAULT_GAIN_STEP;
    bool muted = false;
    /// The clock; `steady_clock()` unless a test says otherwise.
    Clock now;
};

/// The host's half of the soundscape scheduler, written once: it forwards
/// the session's audio commands to the engine's mixer, ticks it from
/// `update()` while anything sounds, and applies what it answers to a
/// `Speaker` — a late voice's entry point, a loop's wrap, voices reported
/// ended or measured, tracks with no file told as silence. A speaker only
/// makes the sound. Calls come from one thread; `VoiceEvents` from any.
class SoundscapeDriver
{
  public:
    static Returns<std::unique_ptr<SoundscapeDriver>> create(const Campaign& campaign,
                                                             Speaker& speaker,
                                                             SoundscapeOptions options = {})
    {
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        auto mixer = Mixer::create(options.max_cues, options.gain_step);
        if (!mixer) return Result<std::unique_ptr<SoundscapeDriver>>(mixer.failure());
        std::unique_ptr<SoundscapeDriver> driver(
            new SoundscapeDriver(campaign, speaker, std::move(mixer).value(), std::move(options)));
        return Result<std::unique_ptr<SoundscapeDriver>>(std::move(driver));
#else
        Mixer mixer = Mixer::create(options.max_cues, options.gain_step);
        return std::unique_ptr<SoundscapeDriver>(
            new SoundscapeDriver(campaign, speaker, std::move(mixer), std::move(options)));
#endif
    }

    SoundscapeDriver(const SoundscapeDriver&) = delete;
    SoundscapeDriver& operator=(const SoundscapeDriver&) = delete;

    ~SoundscapeDriver()
    {
        disposed_ = true;
        for (auto& [id, slot] : slots_)
            if (slot.voice) slot.voice->stop();
        slots_.clear();
    }

    /// Obeys one of the session's audio commands.
    void apply(const AudioCommand& command)
    {
        switch (command.kind)
        {
            case AudioKind::play:
            {
                if (!command.soundscape) return;
                std::vector<std::uint32_t> unresolved;
                for (std::uint32_t i = 0; i < command.soundscape->tracks.size(); ++i)
                {
                    const auto media =
                        static_cast<std::uint32_t>(command.soundscape->tracks[i].media);
                    const std::optional<VoiceSource> source = source_of(media);
                    if (!source || !speaker_.resolves(*source)) unresolved.push_back(i);
                }
                run([&](Mixer& m)
                    { return m.play(clock(), *command.soundscape, unresolved, 0, muted_); });
                break;
            }
            case AudioKind::stop:
                run([&](Mixer& m) { return m.stop(clock()); });
                break;
            case AudioKind::cut:
                run([&](Mixer& m) { return m.cut(clock()); });
                break;
            case AudioKind::cue:
                // A cue with no file is ignored, as the editor does.
                if (!source_of(command.media)) return;
                run([&](Mixer& m)
                    { return m.cue(clock(), command.media, command.volume, command.speed); });
                break;
        }
    }

    /// What the host calls from its loop: hears what the voices reported and
    /// moves the timeline on. Every 25 ms or so is the editor's pace.
    void update()
    {
        drain();
        if (idle() || paused_) return;
        run([&](Mixer& m) { return m.tick(clock()); });
    }

    /// Mutes or unmutes at once; the scheduler writes every gain as zero and
    /// remembers the rest.
    void set_muted(bool muted)
    {
        muted_ = muted;
        call([&](Mixer& m) { return m.set_muted(muted); });
        update();
    }
    bool muted() const noexcept { return muted_; }

    /// Holds every voice, and the timeline with them — not the story.
    void pause()
    {
        run([&](Mixer& m) { return m.pause(clock()); });
        paused_ = true;
    }
    void resume()
    {
        paused_ = false;
        run([&](Mixer& m) { return m.resume(clock()); });
    }

    /// The soundscape in force: sounding, or waiting its turn.
    std::optional<std::string> playing() const
    {
        return read<std::optional<std::string>>([](const Mixer& m) { return m.playing_id(); });
    }

    /// Whether nothing sounds, waits or cues — when `update()` may rest.
    bool idle() const
    {
        return read<bool>([](const Mixer& m) { return m.is_idle(); });
    }

    /// How many voices the speaker holds open now.
    std::size_t voices() const noexcept { return slots_.size(); }

  private:
    struct Slot
    {
        std::unique_ptr<Voice> voice;
        std::uint64_t generation = 0;
        bool wants_duration = false;
        bool duration_reported = false;
    };

    SoundscapeDriver(const Campaign& campaign, Speaker& speaker, Mixer mixer,
                     SoundscapeOptions options)
        : campaign_(campaign),
          speaker_(speaker),
          mixer_(std::move(mixer)),
          clock_(options.now ? std::move(options.now) : steady_clock()),
          inbox_(std::make_shared<VoiceEvents::Inbox>())
    {
        if (options.muted) set_muted(true);
    }

    std::uint64_t clock() const { return clock_(); }

    std::optional<VoiceSource> source_of(std::uint32_t media) const
    {
        const RippleStr* out = nullptr;
        if (ripple_campaign_media_path(campaign_.handle(), media, &out, nullptr) !=
                RIPPLE_SUCCESS ||
            !out)
            return std::nullopt;
        detail::Owned<RippleStr> held(out);
        return VoiceSource{media, detail::str(*held), campaign_.media_file(media)};
    }

    // A failure from the mixer is the engine's (a trap, in practice): the
    // sound stops rather than the story.
    template <class Verb>
    void call(Verb&& verb)
    {
        (void)verb(mixer_);
    }

    template <class T, class Verb>
    T read(Verb&& verb) const
    {
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        auto done = verb(mixer_);
        return done ? std::move(done).value() : T{};
#else
        return verb(mixer_);
#endif
    }

    template <class Verb>
    void run(Verb&& verb)
    {
        if (disposed_) return;
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        auto done = verb(mixer_);
        if (!done) return;
        obey(done.value());
#else
        obey(verb(mixer_));
#endif
        drain();
    }

    void obey(const VoiceCommands& commands)
    {
        for (const VoiceCommand& command : commands)
        {
            if (disposed_) return;
            switch (command.kind)
            {
                case VoiceKind::open:
                    open(command);
                    break;
                case VoiceKind::gain:
                {
                    const auto slot = slots_.find(command.voice);
                    if (slot != slots_.end()) slot->second.voice->set_gain(command.gain);
                    break;
                }
                case VoiceKind::stop:
                    stop(command.voice);
                    break;
                case VoiceKind::pause:
                    for (auto& [id, slot] : slots_) slot.voice->pause();
                    break;
                case VoiceKind::resume:
                    for (auto& [id, slot] : slots_) slot.voice->resume();
                    break;
            }
        }
    }

    void open(const VoiceCommand& open)
    {
        const std::uint64_t id = open.voice;
        auto play = [&](double from)
        {
            VoicePlay request;
            request.from_ms = from;
            if (open.to_ms) request.to_ms = static_cast<double>(*open.to_ms);
            request.gain = open.gain;
            request.rate = open.rate;
            request.looping = open.looping;
            request.loop_from_ms = static_cast<double>(open.loop_from_ms);
            return request;
        };
        const auto existing = slots_.find(id);
        if (existing != slots_.end())
        {
            // A gapless bed whose file ended, given again on the same voice.
            existing->second.voice->play(play(static_cast<double>(open.from_ms)));
            return;
        }
        const std::optional<VoiceSource> source = source_of(open.media);
        if (!source)
        {
            // Not a file the scheduler was told is missing: the same silence
            // an unresolved track is, reported so the id is let go of.
            ended_unknown_.push_back(id);
            return;
        }
        const std::uint64_t generation = ++generations_;
        const std::uint64_t issued = clock();
        std::unique_ptr<Voice> voice =
            speaker_.open(*source, open.role, VoiceEvents(inbox_, id, generation));
        if (!voice)
        {
            // A file that would not play is heard the way one that ended is.
            ended_unknown_.push_back(id);
            return;
        }
        // How late the voice came is the host's to add (§9): the ramps do not
        // move; they are measured from when the track was due.
        double from =
            static_cast<double>(open.from_ms) + static_cast<double>(clock() - issued) * open.rate;
        if (open.to_ms)
        {
            const double to = static_cast<double>(*open.to_ms);
            const double loop_from = static_cast<double>(open.loop_from_ms);
            if (open.looping)
            {
                // A bed has been round some number of times meanwhile.
                const double clip = to - loop_from;
                if (clip > 0 && from > to) from = loop_from + std::fmod(from - loop_from, clip);
            }
            else if (from >= to)
            {
                // Over by the time the voice came: heard, as far as the clock goes.
                voice->stop();
                ended_unknown_.push_back(id);
                return;
            }
        }
        Slot slot;
        slot.generation = generation;
        slot.wants_duration = open.role == VoiceRole::track && !open.to_ms;
        voice->play(play(from));
        slot.voice = std::move(voice);
        slots_.emplace(id, std::move(slot));
    }

    // The slot goes before the voice is stopped, so a second stop on the same
    // tick finds nothing, and a late `ended` from it is ignored.
    void stop(std::uint64_t id)
    {
        const auto slot = slots_.find(id);
        if (slot == slots_.end()) return;
        std::unique_ptr<Voice> voice = std::move(slot->second.voice);
        slots_.erase(slot);
        voice->stop();
    }

    // What the voices reported, and the voices the driver itself let go of,
    // told to the scheduler — after the batch that caused them, never inside it.
    void drain()
    {
        for (int rounds = 0; rounds < 64 && !disposed_; ++rounds)
        {
            std::vector<VoiceEvents::Report> reports;
            {
                const std::lock_guard<std::mutex> held(inbox_->lock);
                reports.swap(inbox_->reports);
            }
            std::vector<std::uint64_t> unknown;
            unknown.swap(ended_unknown_);
            if (reports.empty() && unknown.empty()) return;
            for (std::uint64_t id : unknown)
                run_quietly([&](Mixer& m) { return m.voice_ended(clock(), id); });
            for (const VoiceEvents::Report& report : reports)
            {
                const auto slot = slots_.find(report.voice);
                if (slot == slots_.end() || slot->second.generation != report.generation) continue;
                if (report.is_duration)
                {
                    if (!slot->second.wants_duration || slot->second.duration_reported) continue;
                    slot->second.duration_reported = true;
                    call(
                        [&](Mixer& m) {
                            return m.duration_known(
                                report.voice, static_cast<std::uint64_t>(std::llround(report.ms)));
                        });
                }
                else
                {
                    run_quietly([&](Mixer& m) { return m.voice_ended(clock(), report.voice); });
                }
            }
        }
    }

    template <class Verb>
    void run_quietly(Verb&& verb)
    {
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        auto done = verb(mixer_);
        if (done) obey(done.value());
#else
        obey(verb(mixer_));
#endif
    }

    const Campaign& campaign_;
    Speaker& speaker_;
    mutable Mixer mixer_;
    Clock clock_;
    std::shared_ptr<VoiceEvents::Inbox> inbox_;
    std::unordered_map<std::uint64_t, Slot> slots_;
    std::vector<std::uint64_t> ended_unknown_;
    std::uint64_t generations_ = 0;
    bool muted_ = false;
    bool paused_ = false;
    bool disposed_ = false;
};

////////////////////////////////////////////////////////////////////////////////
// The player: the safe loop over a session

/// What a `Player` hands the host.
struct PlayerHandlers
{
    /// Every effect, in the order the engine produced it.
    std::function<void(const Effect&)> on_effect;
    /// A failure of a verb the player made by itself — the `elapsed` after a
    /// hold — or a handler's exception. Delivery goes on either way.
    std::function<void(const Failure&)> on_error;
    /// Where the story's sound goes; without one, nothing is heard.
    SoundscapeDriver* audio = nullptr;
    /// What time it is for the story — `local_moment` for this machine's —
    /// asked before every verb that can step and handed to `set_now`. Empty
    /// for a host that hands in no clock, or has not been granted it.
    std::function<std::optional<Moment>()> moment;
    /// Whether the player runs a passage's countdown on its own clock, from
    /// `update()`. Off for a host that keeps time some other way — a test, a
    /// walk of every branch, a reader who asked for no time limits (WCAG
    /// 2.2.1) — which calls `begin_countdown` and `expire` itself, or never.
    bool countdowns = true;
};

/// Where a passage's countdown stands, by the player's clock.
struct Countdown
{
    ChoiceTimer timer;
    /// Whether the countdown has begun: `begin_countdown` was said.
    bool begun = false;
    /// When the next of the two is due — the beginning, or once begun the end.
    std::uint64_t due = 0;
};

using CallbackHandler = std::function<void(const CallbackEvent&)>;

/// The loop over a session, written once. It delivers every effect in order,
/// keeps the Turn the last one named, answers a `hold` from its clock when
/// `update()` finds it due, runs a passage's countdown the same way (§4b),
/// tells the session the time before every verb, applies the sound to the
/// driver, and is the table the author's named callbacks are dispatched
/// through (`campaign-host-api.md` §5b). A verb from inside a handler is
/// refused: the rest of the batch would arrive after the new one.
///
/// What the player does not wrap is on `session()`: `set_position` and
/// `set_stand_in` move no story and answer no effects.
class Player
{
  public:
    Player(Session session, PlayerHandlers handlers, Clock now = {})
        : session_(std::move(session)),
          handlers_(std::move(handlers)),
          clock_(now ? std::move(now) : steady_clock())
    {
    }

    /// The session underneath, for what the player does not wrap. A verb
    /// called on it goes round the player.
    Session& session() noexcept { return session_; }

    /// Where the story is: the last Turn delivered.
    const std::optional<Effect>& turn() const noexcept { return turn_; }

    /// When the pending hold is due, by the player's clock; none when none is.
    std::optional<std::uint64_t> hold_due() const noexcept { return due_; }

    /// The shown passage's countdown, for a host that draws one; none when
    /// the passage has none. Kept across a `set_locale`, which is the same
    /// visit; dropped once the reader chooses or anything else is shown.
    const std::optional<Countdown>& countdown() const noexcept { return countdown_; }

    /// Calls `handler` whenever the callback named `name` fires, replacing
    /// any handler already under that name.
    void on_callback(std::string name, CallbackHandler handler)
    {
        callbacks_[std::move(name)] = std::move(handler);
    }
    void remove_callback(const std::string& name) { callbacks_.erase(name); }
    /// Calls `handler` for every callback that fires, registered or not.
    void on_any_callback(CallbackHandler handler) { every_.push_back(std::move(handler)); }
    void clear_callbacks()
    {
        callbacks_.clear();
        every_.clear();
    }
    /// How many callbacks fired with nothing registered under their name —
    /// not a failure: the author and the host declare their halves apart.
    std::uint64_t callback_misses() const noexcept { return misses_; }

    Returns<void> start()
    {
        return verb("start", Visit::ends, [&] { return session_.start(); });
    }
    Returns<void> choose(std::optional<std::uint32_t> index)
    {
        return verb("choose", Visit::ends, [&] { return session_.choose(index); });
    }
    Returns<void> answer(const std::optional<Value::Map>& values)
    {
        return verb("answer", Visit::ends, [&] { return session_.answer(values); });
    }
    /// The countdown begins now, whatever its `after_ms` said — for a host
    /// that runs countdowns itself. With `countdowns` on, `update()` says it.
    Returns<void> begin_countdown()
    {
        if (countdown_ && !countdown_->begun && !delivering_ && !disposed_)
        {
            countdown_->begun = true;
            countdown_->due = clock_() + countdown_->timer.timeout_ms;
        }
        return verb("begin_countdown", Visit::stays, [&] { return session_.begin_countdown(); });
    }
    /// The countdown ran out now: the passage's default is taken.
    Returns<void> expire()
    {
        return verb("expire", Visit::ends, [&] { return session_.expire(); });
    }
    /// The reader's answer for one kind; on a `Permit` turn, the last one
    /// begins the story.
    Returns<void> permit(PermissionKind kind, bool granted)
    {
        return verb("permit", Visit::stays, [&] { return session_.permit(kind, granted); });
    }
    Returns<void> set_locale(std::string_view locale)
    {
        return verb("set_locale", Visit::stays, [&] { return session_.set_locale(locale); });
    }
    Returns<void> restart()
    {
        return verb("restart", Visit::ends, [&] { return session_.restart(); });
    }
    Returns<void> stop()
    {
        return verb("stop", Visit::ends, [&] { return session_.stop(); });
    }

    /// Puts the story down and answers its save — none, and nothing stopped,
    /// unless the story is on a passage or a prompt.
    Returns<std::optional<std::string>> pause()
    {
        if (delivering_) return detail::fail<std::optional<std::string>>(reentrant("pause"));
        due_.reset();
        // A save carries no time: a story resumed on a timed passage waits afresh.
        countdown_.reset();
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        auto paused = session_.pause();
        if (!paused) return Result<std::optional<std::string>>(paused.failure());
        dispatch(paused->effects);
        return Result<std::optional<std::string>>(paused->save);
#else
        Paused paused = session_.pause();
        dispatch(paused.effects);
        return paused.save;
#endif
    }

    /// What the host calls from its loop: answers a hold whose time has come,
    /// and begins or runs out a countdown whose time has.
    void update()
    {
        if (delivering_ || disposed_) return;
        if (due_)
        {
            if (clock_() < *due_) return;
            due_.reset();
            by_itself([&] { return session_.elapsed(); });
            return;
        }
        if (!countdown_ || !handlers_.countdowns || clock_() < countdown_->due) return;
        if (!countdown_->begun)
        {
            // From when it was due rather than when this frame noticed, so a
            // slow loop does not lengthen the reader's time.
            countdown_->begun = true;
            countdown_->due += countdown_->timer.timeout_ms;
            by_itself([&] { return session_.begin_countdown(); });
            return;
        }
        countdown_.reset();
        by_itself([&] { return session_.expire(); });
    }

    /// Delivers nothing more and moves the story no further.
    void dispose() noexcept
    {
        due_.reset();
        countdown_.reset();
        disposed_ = true;
    }

  private:
    /// Whether a verb's answer is still the visit to the passage on show —
    /// the countdown kept — or the end of it.
    enum class Visit
    {
        stays,
        ends,
    };

    static Failure reentrant(const char* verb)
    {
        Failure failure;
        failure.code = Code::reentrant;
        failure.message =
            std::string(verb) +
            "() was called while the player was delivering a batch; call it after the "
            "handler returns";
        return failure;
    }

    template <class Verb>
    Returns<void> verb(const char* name, Visit visit, Verb&& run)
    {
        if (delivering_) return detail::fail<void>(reentrant(name));
        if (disposed_)
        {
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
            return Result<void>();
#else
            return;
#endif
        }
        due_.reset();
        tell_time();
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        auto effects = run();
        if (!effects) return Result<void>(effects.failure());
        if (visit == Visit::ends) countdown_.reset();
        dispatch(effects.value());
        return Result<void>();
#else
        Effects effects = run();
        if (visit == Visit::ends) countdown_.reset();
        dispatch(effects);
#endif
    }

    // A verb the player makes by itself, from `update()`: its failure is
    // reported rather than thrown at a host that did not call it.
    template <class Verb>
    void by_itself(Verb&& run)
    {
        tell_time();
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        auto next = run();
        if (!next)
        {
            report(next.failure());
            return;
        }
        dispatch(next.value());
#else
        try
        {
            dispatch(run());
        }
        catch (const Error& error)
        {
            if (handlers_.on_error)
                handlers_.on_error(error.failure());
            else
                throw;
        }
#endif
    }

    // The host's instant, handed over before a verb can read it. Nothing to
    // fail on but a dead handle, which the verb itself then reports.
    void tell_time()
    {
        if (!handlers_.moment) return;
        const std::optional<Moment> now = handlers_.moment();
        const RippleMoment raw = detail::moment(now.value_or(Moment{}));
        ripple_session_set_now(session_.handle(), now ? &raw : nullptr, nullptr);
    }

    void report(const Failure& failure)
    {
        if (handlers_.on_error) handlers_.on_error(failure);
    }

    // A handler's error is the host's bug and not the story's: reported, and
    // the rest of the batch still delivered.
    template <class Handler>
    void absorb(Handler&& handler)
    {
#ifdef RIPPLE_HPP_NO_EXCEPTIONS
        handler();
#else
        try
        {
            handler();
        }
        catch (const Error& error)
        {
            report(error.failure());
        }
        catch (const std::exception& error)
        {
            Failure failure;
            failure.code = Code::invalid_argument;
            failure.message = error.what();
            report(failure);
        }
#endif
    }

    void dispatch(const Effects& effects)
    {
        if (disposed_) return;
        delivering_ = true;
        for (const Effect& effect : effects)
        {
            if (disposed_) break;
            if (is_turn(effect))
            {
                turn_ = effect;
                watch(effect);
            }
            if (const Audio* audio = std::get_if<Audio>(&effect))
            {
                if (handlers_.audio) absorb([&] { handlers_.audio->apply(audio->command); });
            }
            if (const Callback* fired = std::get_if<Callback>(&effect)) fire(fired->event);
            if (handlers_.on_effect) absorb([&] { handlers_.on_effect(effect); });
        }
        delivering_ = false;
        if (!effects.empty() && !disposed_)
        {
            if (const Hold* hold = std::get_if<Hold>(&effects.back())) due_ = clock_() + hold->ms;
        }
    }

    // A passage shown with a timer starts a countdown, unless this is the
    // same visit re-rendered; anything else shown ends one.
    void watch(const Effect& turn)
    {
        const Show* shown = std::get_if<Show>(&turn);
        if (!shown || !shown->passage.timer)
        {
            countdown_.reset();
            return;
        }
        if (!countdown_)
            countdown_ =
                Countdown{*shown->passage.timer, false, clock_() + shown->passage.timer->after_ms};
    }

    void fire(const CallbackEvent& fired)
    {
        const auto handler = callbacks_.find(fired.callback);
        if (handler != callbacks_.end())
            absorb([&] { handler->second(fired); });
        else
            ++misses_;
        for (const CallbackHandler& every : every_) absorb([&] { every(fired); });
    }

    Session session_;
    PlayerHandlers handlers_;
    Clock clock_;
    std::optional<Effect> turn_;
    std::optional<std::uint64_t> due_;
    std::optional<Countdown> countdown_;
    std::unordered_map<std::string, CallbackHandler> callbacks_;
    std::vector<CallbackHandler> every_;
    std::uint64_t misses_ = 0;
    bool delivering_ = false;
    bool disposed_ = false;
};

}  // namespace ripple
