// Every conformance fixture of the pinned engine release, played through this
// binding's own API — `ripple::Session` and `ripple::Player` — and compared
// with what the fixture says a reader saw, the way the release's FORMAT.md
// describes. The engine's own suites judge its rules; this judges the
// spelling: that every effect crosses into C++ whole, an Int stays an Int and
// a Float a Float, and a host built on these types sees what any host sees.
#include <ripple/ripple.hpp>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

constexpr std::uint32_t kStepBudget = 65536;

Json read_json(const fs::path& path)
{
    std::ifstream in(path);
    return Json::parse(in);
}

// A value as text that keeps what the fixtures assert: an Int and a Float of
// the same size differ, and an object's key order does not count. The C++
// twin of the JS binding's canonical.ts.
std::string canonical(const Json& value)
{
    if (value.is_number_integer() || value.is_number_unsigned()) return value.dump();
    if (value.is_number_float())
    {
        const double real = value.get<double>();
        if (std::isfinite(real) && std::floor(real) == real && std::fabs(real) < 1e15)
        {
            char text[64];
            std::snprintf(text, sizeof text, "%.1f", real);
            return text;
        }
        return value.dump();
    }
    if (value.is_array())
    {
        std::string out = "[";
        for (std::size_t i = 0; i < value.size(); ++i) out += (i ? "," : "") + canonical(value[i]);
        return out + "]";
    }
    if (value.is_object())
    {
        std::vector<std::pair<std::string, std::string>> entries;
        for (auto it = value.begin(); it != value.end(); ++it)
            entries.emplace_back(Json(it.key()).dump(), canonical(it.value()));
        std::sort(entries.begin(), entries.end());
        std::string out = "{";
        for (std::size_t i = 0; i < entries.size(); ++i)
            out += (i ? "," : "") + entries[i].first + ":" + entries[i].second;
        return out + "}";
    }
    return value.dump();
}

Json to_json(const ripple::Value& value)
{
    return std::visit(
        [](const auto& held) -> Json
        {
            using T = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return nullptr;
            else if constexpr (std::is_same_v<T, ripple::Value::List>)
            {
                Json out = Json::array();
                for (const auto& item : held) out.push_back(to_json(item));
                return out;
            }
            else if constexpr (std::is_same_v<T, ripple::Value::Map>)
            {
                Json out = Json::object();
                for (const auto& [key, item] : held) out[key] = to_json(item);
                return out;
            }
            else
                return Json(held);
        },
        value.storage());
}

ripple::Value from_json(const Json& value)
{
    if (value.is_null()) return ripple::Value();
    if (value.is_boolean()) return ripple::Value(value.get<bool>());
    if (value.is_number_integer() || value.is_number_unsigned())
        return ripple::Value(value.get<std::int64_t>());
    if (value.is_number_float()) return ripple::Value(value.get<double>());
    if (value.is_string()) return ripple::Value(value.get<std::string>());
    if (value.is_array())
    {
        ripple::Value::List items;
        for (const auto& item : value) items.push_back(from_json(item));
        return ripple::Value(std::move(items));
    }
    ripple::Value::Map entries;
    for (auto it = value.begin(); it != value.end(); ++it)
        entries.emplace_back(it.key(), from_json(it.value()));
    return ripple::Value(std::move(entries));
}

struct Played
{
    std::vector<Json> trace;
    std::vector<Json> callbacks;
    Json entities = Json::object();
};

// What a reader saw of one effect, as FORMAT.md's trace records it.
class Recorder
{
  public:
    Recorder(std::vector<std::string> boards, const ripple::Campaign& campaign, Played& into)
        : boards_(std::move(boards)), campaign_(campaign), into_(into)
    {
    }

    void operator()(const ripple::Effect& effect)
    {
        std::visit([&](const auto& e) { record(e); }, effect);
    }

  private:
    std::string board(std::uint32_t at) const
    {
        return at < boards_.size() ? boards_[at] : std::string();
    }

    void record(const ripple::Show& e)
    {
        Json event{{"kind", "passage"}, {"node", e.passage.node}, {"markdown", e.passage.text}};
        Json choices = Json::array();
        for (const auto& choice : e.passage.choices) choices.push_back(choice.text);
        event["choices"] = choices;
        if (e.passage.untranslated) event["untranslated"] = *e.passage.untranslated;
        into_.trace.push_back(event);
    }
    void record(const ripple::Ask& e)
    {
        Json fields = Json::array();
        for (const auto& field : e.prompt.fields)
            fields.push_back({{"name", field.name}, {"type", field.type}});
        into_.trace.push_back({{"kind", "prompt"}, {"node", e.prompt.node}, {"fields", fields}});
    }
    void record(const ripple::Jumped& e)
    {
        Json event{{"kind", "jump"}, {"board", board(e.board)}};
        if (e.node) event["node"] = *e.node;
        into_.trace.push_back(event);
    }
    void record(const ripple::Ended& e)
    {
        if (e.ran_out)
        {
            into_.trace.push_back({{"kind", "ranOut"}});
            return;
        }
        Json event{{"kind", "end"}};
        if (e.title) event["title"] = *e.title;
        into_.trace.push_back(event);
    }
    void record(const ripple::Failed& e)
    {
        Json event{{"kind", "error"}, {"message", e.message}};
        if (e.node) event["node"] = *e.node;
        into_.trace.push_back(event);
    }
    void record(const ripple::Hold& e)
    {
        into_.trace.push_back({{"kind", "wait"}, {"ms", static_cast<std::int64_t>(e.ms)}});
    }
    void record(const ripple::AmbienceChanged& e)
    {
        Json event{{"kind", "ambience"}};
        event["soundscape"] = e.ambience ? Json(e.ambience->soundscape.id) : Json(nullptr);
        into_.trace.push_back(event);
    }
    void record(const ripple::Audio& e)
    {
        if (e.command.kind != ripple::AudioKind::cue) return;
        Json event{{"kind", "sound"}};
        if (e.command.node) event["node"] = *e.command.node;
        event["media"] = campaign_.media_path(e.command.media).value_or("");
        into_.trace.push_back(event);
    }
    void record(const ripple::Callback& e)
    {
        const ripple::CallbackEvent& fired = e.event;
        Json record{{"callback", fired.callback}, {"event", std::string(fired.event_name())}};
        if (fired.board) record["board"] = board(*fired.board);
        if (fired.node) record["node"] = *fired.node;
        if (fired.name) record["name"] = *fired.name;
        if (fired.from && !fired.from->is_null()) record["from"] = to_json(*fired.from);
        if (fired.to && !fired.to->is_null()) record["to"] = to_json(*fired.to);
        if (fired.index) record["index"] = static_cast<std::int64_t>(*fired.index);
        into_.callbacks.push_back(record);
    }
    template <class Other>
    void record(const Other&)
    {
    }

    std::vector<std::string> boards_;
    const ripple::Campaign& campaign_;
    Played& into_;
};

bool over(const std::optional<ripple::Effect>& turn)
{
    return !turn || ripple::is_over(*turn);
}

ripple::SessionOptions fixture_options()
{
    ripple::SessionOptions options;
    options.step_budget = kStepBudget;
    return options;
}

// The same verbs whichever API is underneath, and where the story is.
class Host
{
  public:
    virtual ~Host() = default;
    virtual const std::optional<ripple::Effect>& turn() const = 0;
    virtual void start() = 0;
    virtual void choose(std::optional<std::uint32_t> index) = 0;
    virtual void answer(const std::optional<ripple::Value::Map>& values) = 0;
    virtual void elapsed() = 0;
    virtual void set_locale(const std::string& locale) = 0;
    virtual std::optional<std::string> pause() = 0;
    virtual void resume(const std::string& save) = 0;
    virtual void restart() = 0;
    virtual std::vector<ripple::Entity> entities() = 0;
};

// Through a Session: every batch settled, while it ends on a hold, by saying
// `elapsed` at once — a fixture's clock never waits.
class SessionHost : public Host
{
  public:
    SessionHost(const ripple::Campaign& campaign, ripple::Session session, std::uint64_t seed,
                Recorder& record)
        : campaign_(campaign), session_(std::move(session)), seed_(seed), record_(record)
    {
    }
    const std::optional<ripple::Effect>& turn() const override { return turn_; }
    void start() override { settle(session_.start()); }
    void choose(std::optional<std::uint32_t> index) override { settle(session_.choose(index)); }
    void answer(const std::optional<ripple::Value::Map>& values) override
    {
        settle(session_.answer(values));
    }
    void elapsed() override { settle(session_.elapsed()); }
    void set_locale(const std::string& locale) override { settle(session_.set_locale(locale)); }
    std::optional<std::string> pause() override
    {
        ripple::Paused paused = session_.pause();
        settle(paused.effects);
        return paused.save;
    }
    void resume(const std::string& save) override
    {
        const ripple::Generator generator = session_.generator();
        session_ = ripple::Session::resume(campaign_, save, seed_, fixture_options());
        session_.set_generator(generator);
        settle(session_.start());
    }
    void restart() override { settle(session_.restart()); }
    std::vector<ripple::Entity> entities() override { return session_.entities(); }

  private:
    void settle(ripple::Effects batch)
    {
        for (;;)
        {
            for (const auto& effect : batch)
            {
                record_(effect);
                if (ripple::is_turn(effect)) turn_ = effect;
            }
            if (batch.empty() || !std::holds_alternative<ripple::Hold>(batch.back())) return;
            batch = session_.elapsed();
        }
    }

    const ripple::Campaign& campaign_;
    ripple::Session session_;
    std::uint64_t seed_;
    Recorder& record_;
    std::optional<ripple::Effect> turn_;
};

// Through a Player, on a clock that is a number: every hold it is waiting on
// is run to its end by moving the clock and calling `update()`.
class PlayerHost : public Host
{
  public:
    PlayerHost(const ripple::Campaign& campaign, ripple::Session session, std::uint64_t seed,
               Recorder& record)
        : campaign_(campaign), seed_(seed), record_(record)
    {
        make(std::move(session));
    }
    const std::optional<ripple::Effect>& turn() const override { return player_->turn(); }
    void start() override
    {
        run([&] { player_->start(); });
    }
    void choose(std::optional<std::uint32_t> index) override
    {
        run([&] { player_->choose(index); });
    }
    void answer(const std::optional<ripple::Value::Map>& values) override
    {
        run([&] { player_->answer(values); });
    }
    void elapsed() override
    {
        for (const auto& effect : player_->session().elapsed()) record_(effect);
    }
    void set_locale(const std::string& locale) override
    {
        run([&] { player_->set_locale(locale); });
    }
    std::optional<std::string> pause() override
    {
        std::optional<std::string> save = player_->pause();
        flush();
        return save;
    }
    void resume(const std::string& save) override
    {
        const ripple::Generator generator = player_->session().generator();
        player_->dispose();
        ripple::Session session =
            ripple::Session::resume(campaign_, save, seed_, fixture_options());
        session.set_generator(generator);
        make(std::move(session));
        run([&] { player_->start(); });
    }
    void restart() override
    {
        run([&] { player_->restart(); });
    }
    std::vector<ripple::Entity> entities() override { return player_->session().entities(); }

  private:
    void make(ripple::Session session)
    {
        ripple::PlayerHandlers handlers;
        handlers.on_effect = [this](const ripple::Effect& effect) { record_(effect); };
        handlers.on_error = [](const ripple::Failure& failure)
        { ADD_FAILURE() << failure.message; };
        player_ = std::make_unique<ripple::Player>(std::move(session), std::move(handlers),
                                                   [this] { return now_; });
    }
    template <class Verb>
    void run(Verb&& verb)
    {
        verb();
        flush();
    }
    void flush()
    {
        while (auto due = player_->hold_due())
        {
            now_ = std::max(now_, *due);
            player_->update();
        }
    }

    const ripple::Campaign& campaign_;
    std::uint64_t seed_;
    Recorder& record_;
    std::uint64_t now_ = 0;
    std::unique_ptr<ripple::Player> player_;
};

Played play(const fs::path& fixture, const Json& input, bool through_player)
{
    const ripple::Campaign campaign = ripple::Campaign::open_dir((fixture / "bundle").string());
    const ripple::Manifest manifest = campaign.manifest();
    std::vector<std::string> boards;
    boards.reserve(manifest.boards.size());
    for (const auto& board : manifest.boards) boards.push_back(board.id);
    Played played;
    Recorder record(boards, campaign, played);

    ripple::SessionConfig config;
    config.board = manifest.entries.empty() ? 0 : manifest.entries.front();
    config.seed = input.contains("seed") ? input["seed"].get<std::uint64_t>() : 0;
    if (input.contains("locale")) config.locale = input["locale"].get<std::string>();
    config.options = fixture_options();
    ripple::Session session = ripple::Session::create(campaign, config);

    std::unique_ptr<Host> host;
    if (through_player)
        host = std::make_unique<PlayerHost>(campaign, std::move(session), config.seed, record);
    else
        host = std::make_unique<SessionHost>(campaign, std::move(session), config.seed, record);

    std::optional<std::string> saved;
    host->start();
    for (const auto& move : input["moves"])
    {
        if (over(host->turn()) && !saved) break;
        const std::string verb = move["do"].get<std::string>();
        if (verb == "choose")
        {
            const auto* shown = host->turn() ? std::get_if<ripple::Show>(&*host->turn()) : nullptr;
            if (!shown) throw std::runtime_error("a move chooses where no passage is showing");
            host->choose(move["choice"].get<std::uint32_t>());
        }
        else if (verb == "answer")
        {
            ripple::Value::Map values;
            for (auto it = move["values"].begin(); it != move["values"].end(); ++it)
                values.emplace_back(it.key(), from_json(it.value()));
            host->answer(values);
        }
        else if (verb == "locale")
            host->set_locale(move["locale"].get<std::string>());
        else if (verb == "pause")
        {
            saved = host->pause();
            if (!saved) throw std::runtime_error("a move pauses where the story is not waiting");
        }
        else if (verb == "resume")
        {
            host->resume(*saved);
            saved.reset();
        }
        else if (verb == "restart")
            host->restart();
    }
    // The reader has left: the story goes on by itself until it is over.
    while (!over(host->turn()))
    {
        const ripple::Effect& turn = *host->turn();
        if (std::holds_alternative<ripple::Show>(turn))
            host->choose(ripple::leave);
        else if (std::holds_alternative<ripple::Ask>(turn))
            host->answer(std::nullopt);
        else
            host->elapsed();
    }
    for (const auto& entity : host->entities())
        played.entities[entity.key] = to_json(entity.values);
    return played;
}

std::vector<fs::path> fixtures()
{
    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(RIPPLE_FIXTURES))
        if (fs::exists(entry.path() / "input.json")) found.push_back(entry.path());
    std::sort(found.begin(), found.end());
    return found;
}

bool needs_python(const Json& input)
{
    if (!input.contains("needs")) return false;
    for (const auto& need : input["needs"])
        if (need == "python") return true;
    return false;
}

void compare(const std::string& name, const Played& got, const Json& wanted)
{
    const Json& trace = wanted.at("trace");
    ASSERT_EQ(got.trace.size(), trace.size()) << name << ": the trace has a different length";
    for (std::size_t i = 0; i < trace.size(); ++i)
        EXPECT_EQ(canonical(got.trace[i]), canonical(trace[i])) << name << ": trace event " << i;
    const Json callbacks = wanted.contains("callbacks") ? wanted["callbacks"] : Json::array();
    ASSERT_EQ(got.callbacks.size(), callbacks.size()) << name << ": the callbacks differ in number";
    for (std::size_t i = 0; i < callbacks.size(); ++i)
        EXPECT_EQ(canonical(got.callbacks[i]), canonical(callbacks[i]))
            << name << ": callback " << i;
    const Json entities = wanted.contains("entities") ? wanted["entities"] : Json::object();
    EXPECT_EQ(canonical(got.entities), canonical(entities)) << name << ": the entities";
}

void play_all(bool through_player)
{
    const bool python = ripple::engine_has_python();
    std::size_t played = 0;
    std::size_t skipped = 0;
    for (const fs::path& fixture : fixtures())
    {
        const std::string name = fixture.filename().string();
        const Json input = read_json(fixture / "input.json");
        if (needs_python(input) && !python)
        {
            ++skipped;
            continue;
        }
        SCOPED_TRACE(name);
        compare(name, play(fixture, input, through_player), read_json(fixture / "expect.json"));
        ++played;
    }
    // Twenty-four fixtures, three of which need the interpreter.
    EXPECT_EQ(played + skipped, 24u);
    EXPECT_EQ(skipped, python ? 0u : 3u);
}

}  // namespace

TEST(Conformance, EveryFixturePlaysThroughTheSession)
{
    play_all(false);
}

TEST(Conformance, EveryFixturePlaysThroughThePlayer)
{
    play_all(true);
}

TEST(Conformance, NothingTheEngineHandedOutIsLeftOver)
{
    play_all(false);
    EXPECT_EQ(ripple_outstanding(), 0u);
}
