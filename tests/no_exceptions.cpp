// ripple.hpp under RIPPLE_HPP_NO_EXCEPTIONS and -fno-exceptions, as a game
// engine builds it: every call answers a ripple::Result, a failure is read
// rather than caught, and the story still plays to its end.
#include <ripple/ripple.hpp>

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;

    // A failure is a value: a folder with no bundle in it.
    const auto missing = ripple::Campaign::open_dir("/nonexistent/ripple/bundle");
    if (missing.ok() || missing.failure().code != ripple::Code::bundle_format) return 1;

    auto campaign = ripple::Campaign::open_dir(argv[1]);
    if (!campaign) return 1;
    auto manifest = campaign->manifest();
    if (!manifest) return 1;
    ripple::SessionConfig config;
    config.board = manifest->entries.empty() ? 0 : manifest->entries.front();
    auto session = ripple::Session::create(*campaign, config);
    if (!session) return 1;

    int turns = 0;
    auto batch = session->start();
    while (batch && !batch->empty())
    {
        ++turns;
        const ripple::Effect& turn = batch->back();
        if (ripple::is_over(turn)) break;
        if (std::holds_alternative<ripple::Show>(turn))
            batch = session->choose(0);
        else if (std::holds_alternative<ripple::Ask>(turn))
            batch = session->answer(std::nullopt);
        else
            batch = session->elapsed();
    }
    if (!batch)
    {
        std::fprintf(stderr, "%s\n", batch.failure().message.c_str());
        return 1;
    }
    // A verb the story is not asking for, read as a code.
    const auto refused = session->elapsed();
    if (refused.ok()) return 1;
    std::printf("played %d turns without an exception; then: %s\n", turns,
                refused.failure().message.c_str());
    return 0;
}
