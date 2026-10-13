/*
 * A campaign in a terminal, in C99 on ripple/ripple.h alone.
 *
 *   ripple_c_player <bundle folder> [locale]
 *
 * Shows each passage and its choices, reads a number, and answers a form by
 * leaving it. A hold is waited out for real; everything the engine hands out
 * goes back through ripple_free(). The loop is the one every C host writes.
 */
#include <ripple/ripple.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static RippleStr span(const char* text)
{
    RippleStr s;
    s.ptr = text;
    s.len = strlen(text);
    return s;
}

static void wait_ms(uint64_t ms)
{
    clock_t until = clock() + (clock_t)((double)ms * CLOCKS_PER_SEC / 1000.0);
    while (clock() < until)
    {
    }
}

static int refused(const char* what, RippleResult code, const RippleError* error)
{
    fprintf(stderr, "%s refused (%d): %.*s\n", what, (int)code, error ? (int)error->message.len : 0,
            error ? error->message.ptr : "");
    ripple_free(error);
    return 1;
}

static uint32_t ask_choice(const RipplePassage* passage)
{
    char line[32];
    size_t i;
    printf("\n== %.*s ==\n%.*s\n\n", (int)passage->title.len, passage->title.ptr,
           (int)passage->text.len, passage->text.ptr);
    if (passage->choice_count == 0)
    {
        return RIPPLE_CHOICE_NONE;
    }
    for (i = 0; i < passage->choice_count; i++)
    {
        printf("  %lu. %.*s%s\n", (unsigned long)(i + 1), (int)passage->choices[i].text.len,
               passage->choices[i].text.ptr, passage->choices[i].available ? "" : " (spent)");
    }
    for (;;)
    {
        unsigned long picked;
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL)
        {
            return RIPPLE_CHOICE_NONE;
        }
        picked = strtoul(line, NULL, 10);
        if (picked >= 1 && picked <= passage->choice_count &&
            passage->choices[picked - 1].available)
        {
            return (uint32_t)(picked - 1);
        }
    }
}

int main(int argc, char** argv)
{
    RippleCampaign* campaign = NULL;
    RippleSession* session = NULL;
    const RippleManifest* manifest = NULL;
    const RippleEffects* batch = NULL;
    const RippleError* error = NULL;
    RippleSessionConfig config;
    RippleResult code;

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <bundle folder> [locale]\n", argv[0]);
        return 2;
    }
    code = ripple_campaign_open_dir(span(argv[1]), &campaign, &error);
    if (code != RIPPLE_SUCCESS)
    {
        return refused("open", code, error);
    }
    ripple_campaign_manifest(campaign, &manifest, NULL);
    printf("%.*s — engine %s\n", (int)manifest->project.title.len, manifest->project.title.ptr,
           ripple_engine_version());

    memset(&config, 0, sizeof config);
    config.board = manifest->entry_count > 0 ? manifest->entries[0] : 0;
    config.seed = (uint64_t)time(NULL);
    if (argc > 2)
    {
        config.locale = span(argv[2]);
    }
    ripple_session_options_default(&config.options);
    code = ripple_session_create(campaign, &config, &session, &error);
    ripple_free(manifest);
    if (code != RIPPLE_SUCCESS)
    {
        return refused("create", code, error);
    }

    code = ripple_session_start(session, &batch, &error);
    while (code == RIPPLE_SUCCESS && batch != NULL && batch->count > 0)
    {
        const RippleEffect* turn = &batch->items[batch->count - 1];
        const RippleEffects* next = NULL;
        switch (turn->kind)
        {
            case RIPPLE_EFFECT_KIND_SHOW:
                code = ripple_session_choose(session, ask_choice(turn->data.show), &next, &error);
                break;
            case RIPPLE_EFFECT_KIND_ASK:
                printf("\n(a form: %.*s — left unanswered)\n", (int)turn->data.ask->title.len,
                       turn->data.ask->title.ptr);
                code = ripple_session_answer(session, NULL, &next, &error);
                break;
            case RIPPLE_EFFECT_KIND_HOLD:
                wait_ms(turn->data.hold_ms);
                code = ripple_session_elapsed(session, &next, &error);
                break;
            case RIPPLE_EFFECT_KIND_ENDED:
                printf("\n— The end%s%.*s —\n", turn->data.ended->title.ptr ? ": " : "",
                       (int)turn->data.ended->title.len,
                       turn->data.ended->title.ptr ? turn->data.ended->title.ptr : "");
                break;
            case RIPPLE_EFFECT_KIND_FAILED:
                printf("\nThe story could not go on: %.*s\n", (int)turn->data.failed->message.len,
                       turn->data.failed->message.ptr);
                break;
            default:
                break;
        }
        ripple_free(batch);
        batch = next;
    }
    if (code != RIPPLE_SUCCESS)
    {
        return refused("a verb", code, error);
    }
    ripple_session_destroy(session);
    ripple_campaign_destroy(campaign);
    return 0;
}
