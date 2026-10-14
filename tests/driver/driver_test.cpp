// The soundscape driver against a speaker that writes down what it is told,
// on a clock that is a number. Nothing here makes a sound: what is checked is
// what a speaker would have been asked to do — which a person still has to
// hear before a real speaker is trusted.
//
// The fixture is `the-choir`: `tavern`, a bed with a one-second fade-in and a
// two-second fade-out whose clip runs to the file's end, and `rain`, a
// ten-second loop. Its media are named in the manifest and not shipped, which
// is all a recording speaker needs.
#include <ripple/ripple.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct Call
{
    std::string what;  // open, play, gain, pause, resume, stop
    std::uint64_t at = 0;
    std::uint64_t voice = 0;
    std::string path;
    ripple::VoicePlay play;
    double gain = 0;
};

struct Recording
{
    std::uint64_t now = 0;
    std::uint64_t latency = 0;
    std::uint64_t opened = 0;
    std::vector<Call> calls;
    std::vector<ripple::VoiceEvents> events;
};

class RecordingVoice : public ripple::Voice
{
  public:
    RecordingVoice(Recording& log, std::uint64_t voice, std::string path)
        : log_(log), voice_(voice), path_(std::move(path))
    {
    }
    void play(const ripple::VoicePlay& request) override
    {
        note("play", [&](Call& c) { c.play = request; });
    }
    void set_gain(double gain) override
    {
        note("gain", [&](Call& c) { c.gain = gain; });
    }
    void pause() override { note("pause"); }
    void resume() override { note("resume"); }
    void stop() override { note("stop"); }

  private:
    template <class Fill = void (*)(Call&)>
    void note(const char* what, Fill fill = [](Call&) {})
    {
        Call call;
        call.what = what;
        call.at = log_.now;
        call.voice = voice_;
        call.path = path_;
        fill(call);
        log_.calls.push_back(call);
    }
    Recording& log_;
    std::uint64_t voice_;
    std::string path_;
};

// Opens every voice it is asked for, `latency` milliseconds late, and keeps
// each voice's events so a test can report an end or a length.
class RecordingSpeaker : public ripple::Speaker
{
  public:
    explicit RecordingSpeaker(Recording& log) : log_(log) {}
    std::unique_ptr<ripple::Voice> open(const ripple::VoiceSource& source, ripple::VoiceRole,
                                        ripple::VoiceEvents events) override
    {
        log_.now += log_.latency;
        const std::uint64_t voice = ++log_.opened;
        Call call;
        call.what = "open";
        call.at = log_.now;
        call.voice = voice;
        call.path = source.path;
        log_.calls.push_back(call);
        log_.events.push_back(std::move(events));
        return std::make_unique<RecordingVoice>(log_, voice, source.path);
    }

  private:
    Recording& log_;
};

std::string bundle()
{
    return (std::filesystem::path(RIPPLE_FIXTURES) / "the-choir" / "bundle").string();
}

ripple::AudioCommand play(const ripple::Soundscape& scape)
{
    ripple::AudioCommand command;
    command.kind = ripple::AudioKind::play;
    command.soundscape = scape;
    return command;
}

ripple::AudioCommand stop()
{
    ripple::AudioCommand command;
    command.kind = ripple::AudioKind::stop;
    return command;
}

struct Rig
{
    Recording log;
    RecordingSpeaker speaker{log};
    ripple::Campaign campaign = ripple::Campaign::open_dir(bundle());
    std::unique_ptr<ripple::SoundscapeDriver> driver;

    explicit Rig(std::uint64_t latency = 0)
    {
        log.latency = latency;
        ripple::SoundscapeOptions options;
        options.now = [this] { return log.now; };
        driver = ripple::SoundscapeDriver::create(campaign, speaker, std::move(options));
    }

    ripple::Soundscape named(const std::string& name)
    {
        const ripple::Manifest manifest = campaign.manifest();
        for (std::uint32_t i = 0; i < manifest.soundscapes.size(); ++i)
            if (manifest.soundscapes[i].name == name) return campaign.soundscape(i);
        throw std::runtime_error("no soundscape " + name);
    }

    // The host's loop, every 25 ms, until `until`.
    void run_to(std::uint64_t until)
    {
        while (log.now < until)
        {
            log.now += 25;
            driver->update();
        }
    }

    std::vector<Call> calls(const std::string& what, const std::string& path = "") const
    {
        std::vector<Call> found;
        for (const Call& call : log.calls)
            if (call.what == what && (path.empty() || call.path == path)) found.push_back(call);
        return found;
    }
};

}  // namespace

TEST(Driver, ASoundscapePlayedOpensItsVoiceAndFadesItIn)
{
    Rig rig;
    rig.log.now = 1000;
    rig.driver->apply(play(rig.named("tavern")));
    rig.run_to(3000);
    const auto opened = rig.calls("open", "media/music/tavern.ogg");
    ASSERT_EQ(opened.size(), 1u);
    const auto plays = rig.calls("play");
    ASSERT_EQ(plays.size(), 1u);
    EXPECT_FALSE(plays[0].play.to_ms.has_value())
        << "an auto bed with no clip end plays to the file's end";
    // The fade-in is the scheduler's, written a step at a time, up and only up.
    const auto gains = rig.calls("gain");
    ASSERT_GT(gains.size(), 3u);
    for (std::size_t i = 1; i < gains.size(); ++i) EXPECT_GE(gains[i].gain, gains[i - 1].gain);
    EXPECT_EQ(rig.driver->playing(), rig.named("tavern").id);
}

TEST(Driver, AVoiceThatOpensLateStartsLaterInItsFile)
{
    Rig prompt(0);
    Rig late(120);
    for (Rig* rig : {&prompt, &late})
    {
        rig->log.now = 1000;
        rig->driver->apply(play(rig->named("rain")));
        rig->run_to(1500);
    }
    const auto on_time = prompt.calls("play");
    const auto behind = late.calls("play");
    ASSERT_EQ(on_time.size(), 1u);
    ASSERT_EQ(behind.size(), 1u);
    // The time `open` took, times the rate, is added to `from` (§9).
    EXPECT_DOUBLE_EQ(behind[0].play.from_ms - on_time[0].play.from_ms,
                     120.0 * on_time[0].play.rate);
    // A `loop` track is a fresh voice per pass; only a gapless `auto` bed is
    // one looping voice — so this one is not.
    EXPECT_FALSE(behind[0].play.looping);
}

TEST(Driver, AHandoverFadesTheOldOutBeforeTheNewOpens)
{
    Rig rig;
    rig.log.now = 1000;
    rig.driver->apply(play(rig.named("tavern")));
    rig.run_to(4000);
    const std::size_t before = rig.log.calls.size();
    rig.driver->apply(play(rig.named("rain")));
    rig.run_to(9000);

    // The tavern's fade-out, down and only down, then its stop.
    std::vector<double> down;
    std::uint64_t stopped = 0;
    for (std::size_t i = before; i < rig.log.calls.size(); ++i)
    {
        const Call& call = rig.log.calls[i];
        if (call.path != "media/music/tavern.ogg") continue;
        if (call.what == "gain") down.push_back(call.gain);
        if (call.what == "stop") stopped = call.at;
    }
    ASSERT_GT(down.size(), 3u);
    for (std::size_t i = 1; i < down.size(); ++i) EXPECT_LE(down[i], down[i - 1]);
    ASSERT_GT(stopped, 0u) << "the tavern was never stopped";
    // Sequential handover: rain opens only once the tavern has gone.
    const auto rain = rig.calls("open", "media/sfx/rain.ogg");
    ASSERT_EQ(rain.size(), 1u);
    EXPECT_GE(rain[0].at, stopped);
    EXPECT_GE(stopped - 4000, 1900u) << "the fade-out is the soundscape's two seconds";
}

TEST(Driver, AVoiceReportedEndedIsLetGoAndAStoppedOneIsNotHeardFrom)
{
    Rig rig;
    rig.log.now = 1000;
    rig.driver->apply(play(rig.named("tavern")));
    rig.run_to(1100);
    ASSERT_EQ(rig.driver->voices(), 1u);
    // The file's length, once the decoder knows it, then its end.
    rig.log.events[0].duration(30000.0);
    rig.log.events[0].ended();
    rig.run_to(1200);
    // A gapless auto bed comes round on the same voice rather than a new one.
    EXPECT_EQ(rig.calls("open").size(), 1u);
    EXPECT_GE(rig.calls("play").size(), 2u);

    rig.driver->apply(stop());
    rig.run_to(5000);
    EXPECT_EQ(rig.driver->voices(), 0u);
    const std::size_t after = rig.log.calls.size();
    rig.log.events[0].ended();  // a stopped voice's end, late: ignored
    rig.run_to(5100);
    EXPECT_EQ(rig.log.calls.size(), after);
    EXPECT_TRUE(rig.driver->idle());
}

TEST(Driver, MutingWritesEveryGainAsZero)
{
    Rig rig;
    rig.log.now = 1000;
    rig.driver->apply(play(rig.named("rain")));
    rig.run_to(1500);
    rig.driver->set_muted(true);
    rig.run_to(2000);
    const auto gains = rig.calls("gain");
    ASSERT_FALSE(gains.empty());
    EXPECT_EQ(gains.back().gain, 0.0);
    EXPECT_TRUE(rig.driver->muted());
}

TEST(Driver, PausingHoldsEveryVoiceAndTheTimeline)
{
    Rig rig;
    rig.log.now = 1000;
    rig.driver->apply(play(rig.named("rain")));
    rig.run_to(1500);
    rig.driver->pause();
    EXPECT_EQ(rig.calls("pause").size(), 1u);
    const std::size_t paused = rig.log.calls.size();
    rig.run_to(20000);
    EXPECT_EQ(rig.log.calls.size(), paused) << "nothing moves while paused";
    rig.driver->resume();
    EXPECT_EQ(rig.calls("resume").size(), 1u);
}
