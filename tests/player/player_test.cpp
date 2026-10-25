// The Player's own clock, on a clock that is a number: a passage's countdown
// begun and run out from `update()`, and the time and the reader's answers a
// story is told before it begins. The conformance suite plays the same
// fixtures with the player's countdowns off, because a fixture's moves say
// when; this is the half of the player a fixture cannot reach.
//
// `the-hesitation` is a rope bridge that counts down two seconds after it is
// shown, for five, and takes `Wait`; `the-weathervane` reads the clock and
// the reader's position, and asks about both before it begins.
#include <ripple/ripple.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

struct Bench
{
    std::uint64_t now = 0;
    std::vector<std::string> fired;
    std::unique_ptr<ripple::Campaign> campaign;
    std::unique_ptr<ripple::Player> player;

    Bench(const char* fixture, ripple::PlayerHandlers handlers)
    {
        campaign = std::make_unique<ripple::Campaign>(
            ripple::Campaign::open_dir((fs::path(RIPPLE_FIXTURES) / fixture / "bundle").string()));
        ripple::SessionConfig config;
        config.board = campaign->manifest().entries.front();
        handlers.on_error = [](const ripple::Failure& failure)
        { ADD_FAILURE() << failure.message; };
        player = std::make_unique<ripple::Player>(ripple::Session::create(*campaign, config),
                                                  std::move(handlers), [this] { return now; });
        player->on_any_callback([this](const ripple::CallbackEvent& event)
                                { fired.emplace_back(event.event_name()); });
    }

    const ripple::Effect* turn() const
    {
        const auto& turn = player->turn();
        return turn ? &*turn : nullptr;
    }

    const ripple::Passage* shown() const
    {
        const auto* show = turn() ? std::get_if<ripple::Show>(turn()) : nullptr;
        return show ? &show->passage : nullptr;
    }

    void at(std::uint64_t ms)
    {
        now = ms;
        player->update();
    }
};

// The countdown the player says it is keeping, or a failure and an empty one.
ripple::Countdown counting(const ripple::Player& player)
{
    const std::optional<ripple::Countdown>& countdown = player.countdown();
    if (!countdown)
    {
        ADD_FAILURE() << "the player keeps no countdown";
        return {};
    }
    return *countdown;
}

}  // namespace

TEST(Player, RunsAPassagesCountdownOnItsOwnClock)
{
    Bench bench("the-hesitation", {});
    bench.player->start();
    ASSERT_NE(bench.shown(), nullptr);
    EXPECT_EQ(bench.shown()->node, "narration_2");
    EXPECT_EQ(counting(*bench.player).timer.after_ms, 2000u);
    EXPECT_EQ(counting(*bench.player).timer.default_choice, 1u);
    EXPECT_FALSE(counting(*bench.player).begun);
    EXPECT_EQ(counting(*bench.player).due, 2000u);

    bench.at(1999);
    EXPECT_TRUE(bench.fired.empty());
    bench.at(2000);
    EXPECT_EQ(bench.fired, std::vector<std::string>{"countdown.started"});
    EXPECT_TRUE(counting(*bench.player).begun);
    EXPECT_EQ(counting(*bench.player).due, 7000u);

    // Another language is the same visit: the countdown goes on where it was.
    bench.player->set_locale("it-IT");
    EXPECT_EQ(bench.shown()->text, "Il ponte di corda oscilla.");
    EXPECT_TRUE(counting(*bench.player).begun);
    EXPECT_EQ(counting(*bench.player).due, 7000u);

    bench.at(6999);
    EXPECT_EQ(bench.shown()->node, "narration_2");
    bench.at(7000);
    ASSERT_NE(bench.shown(), nullptr);
    EXPECT_EQ(bench.shown()->node, "narration_3");
    EXPECT_EQ(bench.fired.back(), "countdown.expired");
    // The next passage's own countdown, from now: it begins at once.
    EXPECT_FALSE(counting(*bench.player).begun);
    EXPECT_EQ(counting(*bench.player).due, 7000u);

    // The reader chose: that countdown is gone, whatever comes next.
    bench.player->choose(1);
    const ripple::Passage* next = bench.shown();
    if (next && next->timer)
        EXPECT_FALSE(counting(*bench.player).begun);
    else
        EXPECT_FALSE(bench.player->countdown());
}

TEST(Player, WithCountdownsOffItsClockRunsNone)
{
    ripple::PlayerHandlers handlers;
    handlers.countdowns = false;
    Bench bench("the-hesitation", std::move(handlers));
    bench.player->start();
    bench.at(1000000);
    EXPECT_EQ(bench.shown()->node, "narration_2");
    EXPECT_TRUE(bench.fired.empty());

    // The host says when, and the player still keeps the count for drawing.
    bench.player->begin_countdown();
    EXPECT_EQ(bench.fired, std::vector<std::string>{"countdown.started"});
    EXPECT_TRUE(counting(*bench.player).begun);
    EXPECT_EQ(counting(*bench.player).due, 1005000u);
    bench.at(2000000);
    EXPECT_EQ(bench.shown()->node, "narration_2");
    bench.player->expire();
    EXPECT_EQ(bench.shown()->node, "narration_3");
}

TEST(Player, AsksBeforeTheStoryBeginsAndTellsItTheTime)
{
    // 2026-10-19T18:30:00+02:00, as the fixture records it.
    const ripple::Moment instant{1792427400000, 120};
    ripple::PlayerHandlers handlers;
    handlers.moment = [&] { return std::optional<ripple::Moment>(instant); };
    Bench bench("the-weathervane", std::move(handlers));
    EXPECT_EQ(bench.campaign->manifest().permissions,
              (std::vector<std::string>{"dateTime", "location"}));

    bench.player->start();
    ASSERT_NE(bench.turn(), nullptr);
    const auto* asked = std::get_if<ripple::Permit>(bench.turn());
    ASSERT_NE(asked, nullptr);
    EXPECT_EQ(asked->kinds,
              (std::vector<ripple::PermissionKind>{ripple::PermissionKind::date_time,
                                                   ripple::PermissionKind::location}));
    EXPECT_FALSE(ripple::is_over(*bench.turn()));

    // A stand-in that does not read is refused, saying why.
    try
    {
        bench.player->session().set_stand_in(ripple::PermissionKind::date_time, "teatime");
        ADD_FAILURE() << "an unreadable stand-in was taken";
    }
    catch (const ripple::HostError& error)
    {
        EXPECT_EQ(error.code(), ripple::Code::stand_in);
        EXPECT_EQ(error.failure().subject, "dateTime");
    }

    // A fix handed in before the answer is kept, and read once it is yes.
    ripple::Position rome;
    rome.latitude = 41.9028;
    rome.longitude = 12.4964;
    bench.player->session().set_position(rome);
    bench.player->permit(ripple::PermissionKind::date_time, true);
    EXPECT_NE(std::get_if<ripple::Permit>(bench.turn()), nullptr);
    bench.player->permit(ripple::PermissionKind::location, true);
    ASSERT_NE(bench.shown(), nullptr);
    EXPECT_NE(bench.shown()->text.find("2026-10-19T18:30:00+02:00"), std::string::npos)
        << bench.shown()->text;

    bench.player->choose(0);
    ASSERT_NE(bench.shown(), nullptr);
    EXPECT_NE(bench.shown()->text.find("41.9028"), std::string::npos) << bench.shown()->text;

    // Once location is taken back, a fix is refused rather than kept.
    bench.player->permit(ripple::PermissionKind::location, false);
    try
    {
        bench.player->session().set_position(rome);
        ADD_FAILURE() << "a fix was taken for a kind denied";
    }
    catch (const ripple::HostError& error)
    {
        EXPECT_EQ(error.code(), ripple::Code::not_permitted);
    }
}
