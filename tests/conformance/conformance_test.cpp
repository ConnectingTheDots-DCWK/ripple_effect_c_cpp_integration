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
#include <cctype>
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
        // Written only when the passage counts down, so every fixture before
        // countdowns reads as it did.
        if (const auto& timer = e.passage.timer)
            event["timer"] = {{"after", static_cast<std::int64_t>(timer->after_ms)},
                              {"timeout", static_cast<std::int64_t>(timer->timeout_ms)},
                              {"visible", timer->visible},
                              {"default", static_cast<std::int64_t>(timer->default_choice)}};
        into_.trace.push_back(event);
    }
    void record(const ripple::Ask& e)
    {
        Json fields = Json::array();
        for (const auto& field : e.prompt.fields)
        {
            Json one{{"name", field.name}, {"type", field.type}};
            // Each written only when there is something to say, for the same reason.
            if (field.optional) one["optional"] = true;
            if (!field.options.empty())
            {
                Json ids = Json::array();
                for (const auto& option : field.options) ids.push_back(option.id);
                one["options"] = ids;
            }
            fields.push_back(one);
        }
        into_.trace.push_back({{"kind", "prompt"}, {"node", e.prompt.node}, {"fields", fields}});
    }
    void record(const ripple::Permit& e)
    {
        Json kinds = Json::array();
        for (const auto kind : e.kinds) kinds.push_back(ripple::permission_kind_name(kind));
        into_.trace.push_back({{"kind", "permit"}, {"kinds", kinds}});
    }
    void record(const ripple::Notice& e)
    {
        // `of`, because `kind` is the event's own.
        Json event{
            {"kind", "notice"}, {"code", e.code}, {"of", ripple::permission_kind_name(e.kind)}};
        if (e.node) event["node"] = *e.node;
        event["message"] = e.message;
        into_.trace.push_back(event);
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

// What a fixture's host knows before a session starts, told again to a
// resumed one: the instant it is played at, the reader's answers so far —
// the fixture's, then every `permit` move's — and their stand-ins.
struct Told
{
    std::optional<ripple::Moment> now;
    std::vector<std::pair<ripple::PermissionKind, bool>> answered;
    std::vector<std::pair<ripple::PermissionKind, std::string>> stand_ins;
};

ripple::PermissionKind kind_of(const std::string& wire)
{
    if (wire == "dateTime") return ripple::PermissionKind::date_time;
    if (wire == "location") return ripple::PermissionKind::location;
    throw std::runtime_error("\"" + wire + "\" is not a kind a story may ask for");
}

// A fixture's `now`, ISO 8601 with its offset — or at UTC without one, as
// the engine's runner reads it: `2026-10-19T18:30:00+02:00`.
ripple::Moment moment_of(const std::string& text)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, consumed = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2d%*1[T ]%2d:%2d:%2d%n", &year, &month, &day, &hour,
                    &minute, &second, &consumed) != 6)
        throw std::runtime_error("now is not an ISO 8601 time: " + text);
    std::size_t at = static_cast<std::size_t>(consumed);
    if (at < text.size() && text[at] == '.')
    {
        ++at;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
    }
    int offset = 0;
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
    {
        int hours = 0, minutes = 0;
        if (std::sscanf(text.c_str() + at + 1, "%2d:%2d", &hours, &minutes) != 2)
            throw std::runtime_error("now has an unreadable offset: " + text);
        offset = (text[at] == '-' ? -1 : 1) * (hours * 60 + minutes);
    }
    const std::int64_t days = ripple::detail::days_from_civil(year, static_cast<unsigned>(month),
                                                              static_cast<unsigned>(day));
    const std::int64_t local = ((days * 24 + hour) * 60 + minute) * 60 + second;
    return ripple::Moment{(local - std::int64_t{offset} * 60) * 1000, offset};
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
    virtual void begin_countdown() = 0;
    virtual void expire() = 0;
    virtual void permit(ripple::PermissionKind kind, bool granted) = 0;
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
                Recorder& record, const Told& told)
        : campaign_(campaign),
          session_(std::move(session)),
          seed_(seed),
          record_(record),
          told_(told)
    {
        tell();
    }
    const std::optional<ripple::Effect>& turn() const override { return turn_; }
    void start() override { settle(session_.start()); }
    void choose(std::optional<std::uint32_t> index) override { settle(session_.choose(index)); }
    void answer(const std::optional<ripple::Value::Map>& values) override
    {
        settle(session_.answer(values));
    }
    void elapsed() override { settle(session_.elapsed()); }
    void begin_countdown() override { settle(session_.begin_countdown()); }
    void expire() override { settle(session_.expire()); }
    void permit(ripple::PermissionKind kind, bool granted) override
    {
        settle(session_.permit(kind, granted));
    }
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
        tell();
        settle(session_.start());
    }
    void restart() override { settle(session_.restart()); }
    std::vector<ripple::Entity> entities() override { return session_.entities(); }

  private:
    // Before `start`, so an answer answers nothing; a save carries no yes.
    void tell()
    {
        if (told_.now) session_.set_now(told_.now);
        for (const auto& [kind, granted] : told_.answered) settle(session_.permit(kind, granted));
        for (const auto& [kind, typed] : told_.stand_ins) session_.set_stand_in(kind, typed);
    }

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
    const Told& told_;
    std::optional<ripple::Effect> turn_;
};

// Through a Player, on a clock that is a number: every hold it is waiting on
// is run to its end by moving the clock and calling `update()`.
class PlayerHost : public Host
{
  public:
    PlayerHost(const ripple::Campaign& campaign, ripple::Session session, std::uint64_t seed,
               Recorder& record, const Told& told)
        : campaign_(campaign), seed_(seed), record_(record), told_(told)
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
    void begin_countdown() override
    {
        run([&] { player_->begin_countdown(); });
    }
    void expire() override
    {
        run([&] { player_->expire(); });
    }
    void permit(ripple::PermissionKind kind, bool granted) override
    {
        run([&] { player_->permit(kind, granted); });
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
        // The instant through the player's own hook, asked before every verb;
        // and no countdown of its own — a fixture's moves say when.
        handlers.moment = [this] { return told_.now; };
        handlers.countdowns = false;
        player_ = std::make_unique<ripple::Player>(std::move(session), std::move(handlers),
                                                   [this] { return now_; });
        for (const auto& answer : told_.answered)
            run([&] { player_->permit(answer.first, answer.second); });
        for (const auto& [kind, typed] : told_.stand_ins)
            player_->session().set_stand_in(kind, typed);
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
    const Told& told_;
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

    Told told;
    if (input.contains("now")) told.now = moment_of(input["now"].get<std::string>());
    if (input.contains("permit"))
        for (auto it = input["permit"].begin(); it != input["permit"].end(); ++it)
            told.answered.emplace_back(kind_of(it.key()), it.value().get<bool>());
    if (input.contains("standIns"))
        for (auto it = input["standIns"].begin(); it != input["standIns"].end(); ++it)
            told.stand_ins.emplace_back(kind_of(it.key()), it.value().get<std::string>());

    std::unique_ptr<Host> host;
    if (through_player)
        host =
            std::make_unique<PlayerHost>(campaign, std::move(session), config.seed, record, told);
    else
        host =
            std::make_unique<SessionHost>(campaign, std::move(session), config.seed, record, told);

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
        // A fixture's clock never runs a countdown out by itself — it waits on
        // nothing, and a countdown that ran out at once would only ever show
        // the default. The moves say when.
        else if (verb == "countdown" || verb == "expire")
        {
            if (!host->turn() || !std::holds_alternative<ripple::Show>(*host->turn()))
                throw std::runtime_error("a move runs a countdown where no passage is showing");
            if (verb == "countdown")
                host->begin_countdown();
            else
                host->expire();
        }
        else if (verb == "permit")
        {
            const ripple::PermissionKind kind = kind_of(move["kind"].get<std::string>());
            const bool granted = move.value("granted", false);
            auto& answered = told.answered;
            answered.erase(std::remove_if(answered.begin(), answered.end(),
                                          [&](const auto& one) { return one.first == kind; }),
                           answered.end());
            answered.emplace_back(kind, granted);
            host->permit(kind, granted);
        }
        else
            throw std::runtime_error("\"" + verb + "\" is not something a reader can do");
    }
    // The reader has left: the story goes on by itself until it is over — or,
    // when nobody answered what it asked before it began, it never begins.
    while (!over(host->turn()) && !std::holds_alternative<ripple::Permit>(*host->turn()))
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
    // Thirty-three fixtures, six of which need the interpreter.
    EXPECT_EQ(played + skipped, 33u);
    EXPECT_EQ(skipped, python ? 0u : 6u);
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
