// A campaign in a terminal, in C++17 over ripple.hpp — the Player, the
// soundscape driver, and a speaker that prints what it would play instead of
// playing it: the seam a real speaker (miniaudio, SDL, FMOD, Wwise, an
// engine's own) is plugged into. docs/speaker.md is the contract.
//
//   ripple_cpp_player <bundle folder> [locale]
#include <ripple/ripple.hpp>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace
{

// Prints every voice command it is given. A real one opens the file and
// plays it; this one says what it would have done.
class PrintingVoice : public ripple::Voice
{
  public:
    explicit PrintingVoice(std::string path) : path_(std::move(path)) {}
    void play(const ripple::VoicePlay& r) override
    {
        std::printf("  [sound] play %s from %.0f ms at gain %.2f%s\n", path_.c_str(), r.from_ms,
                    r.gain, r.looping ? ", looping" : "");
    }
    void set_gain(double) override {}
    void pause() override { std::printf("  [sound] pause %s\n", path_.c_str()); }
    void resume() override { std::printf("  [sound] resume %s\n", path_.c_str()); }
    void stop() override { std::printf("  [sound] stop %s\n", path_.c_str()); }

  private:
    std::string path_;
};

class PrintingSpeaker : public ripple::Speaker
{
  public:
    std::unique_ptr<ripple::Voice> open(const ripple::VoiceSource& source, ripple::VoiceRole role,
                                        ripple::VoiceEvents) override
    {
        std::printf("  [sound] open %s (%s)\n", source.file.value_or(source.path).c_str(),
                    role == ripple::VoiceRole::cue ? "cue" : "track");
        return std::make_unique<PrintingVoice>(source.path);
    }
};

std::optional<std::uint32_t> ask(const ripple::Passage& passage)
{
    std::cout << "\n== " << passage.title << " ==\n" << passage.text << "\n\n";
    if (passage.choices.empty()) return ripple::leave;
    for (std::size_t i = 0; i < passage.choices.size(); ++i)
        std::cout << "  " << i + 1 << ". " << passage.choices[i].text
                  << (passage.choices[i].available ? "" : " (spent)") << "\n";
    for (std::string line; std::cout << "> " && std::getline(std::cin, line);)
    {
        const std::size_t picked = std::strtoul(line.c_str(), nullptr, 10);
        if (picked >= 1 && picked <= passage.choices.size() &&
            passage.choices[picked - 1].available)
            return static_cast<std::uint32_t>(picked - 1);
    }
    return ripple::leave;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <bundle folder> [locale]\n";
        return 2;
    }
    try
    {
        const ripple::Campaign campaign = ripple::Campaign::open_dir(argv[1]);
        const ripple::Manifest manifest = campaign.manifest();
        std::cout << manifest.project.title << " — engine " << ripple::engine_version() << "\n";

        PrintingSpeaker speaker;
        auto audio = ripple::SoundscapeDriver::create(campaign, speaker);

        ripple::SessionConfig config;
        config.board = manifest.entries.empty() ? 0 : manifest.entries.front();
        config.seed =
            static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        if (argc > 2) config.locale = argv[2];

        ripple::PlayerHandlers handlers;
        handlers.audio = audio.get();
        handlers.on_error = [](const ripple::Failure& failure)
        { std::cerr << failure.message << "\n"; };
        ripple::Player player(ripple::Session::create(campaign, config), handlers);
        player.on_any_callback(
            [](const ripple::CallbackEvent& fired) {
                std::cout << "  [callback] " << fired.callback << " (" << fired.event_name()
                          << ")\n";
            });

        player.start();
        // The host's loop: a frame every 25 ms, the driver and the player told
        // the time has moved, a turn answered when one is showing.
        for (;;)
        {
            audio->update();
            player.update();
            const auto& turn = player.turn();
            if (!turn || ripple::is_over(*turn)) break;
            if (const auto* shown = std::get_if<ripple::Show>(&*turn))
                player.choose(ask(shown->passage));
            else if (std::holds_alternative<ripple::Ask>(*turn))
                player.answer(std::nullopt);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        const auto& last = player.turn();
        if (!last) return 0;
        if (const auto* ended = std::get_if<ripple::Ended>(&*last))
            std::cout << "\n— The end" << (ended->title ? ": " + *ended->title : "") << " —\n";
        else if (const auto* failed = std::get_if<ripple::Failed>(&*last))
            std::cout << "\nThe story could not go on: " << failed->message << "\n";
    }
    catch (const ripple::Error& error)
    {
        std::cerr << "ripple: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
