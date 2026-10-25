/*
 * A campaign in a terminal, in C99 on ripple/ripple.h alone.
 *
 *   ripple_c_player <bundle folder> [locale]
 *
 * Shows each passage and its choices, reads a number, and answers a form by
 * leaving it. A hold is waited out for real; everything the engine hands out
 * goes back through ripple_free(). The loop is the one every C host writes.
 *
 * A story that reads the clock or where the reader is asks first, and is told
 * the time if the answer is yes; this player knows no position. It waits for
 * the reader as long as they like, so it runs no countdown: a host that does
 * calls ripple_session_begin_countdown() and ripple_session_expire() from its
 * own clock (examples/cpp_player has the Player do it).
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

/* Prints what a read of the world got instead of the real value. */
static void print_notices(const RippleEffects* batch)
{
    size_t i;
    for (i = 0; i < batch->count; i++)
    {
        const RippleEffect* effect = &batch->items[i];
        if (effect->kind == RIPPLE_EFFECT_KIND_NOTICE)
        {
            printf("  [%.*s] %.*s\n", (int)effect->data.notice->code.len,
                   effect->data.notice->code.ptr, (int)effect->data.notice->message.len,
                   effect->data.notice->message.ptr);
        }
    }
}

/* Asks about each kind the story reads; each answer but the last answers
 * nothing, and the last begins the story. 0 is the date and time, 1 the
 * location — RipplePermissionKind. */
static RippleResult ask_permits(RippleSession* session, const RipplePermit* permit,
                                const RippleEffects** next, const RippleError** error)
{
    RippleResult code = RIPPLE_SUCCESS;
    size_t i;
    for (i = 0; i < permit->kind_count && code == RIPPLE_SUCCESS; i++)
    {
        char line[16];
        int granted;
        printf("May this story read %s? [y/N] ",
               permit->kinds[i] == 1 ? "where you are" : "the date and time");
        fflush(stdout);
        granted = fgets(line, sizeof line, stdin) != NULL && (line[0] == 'y' || line[0] == 'Y');
        ripple_free(*next);
        *next = NULL;
        code = ripple_session_permit(session, permit->kinds[i], granted != 0, next, error);
    }
    return code;
}

/* This machine's clock, at UTC: a C99 program has no portable way to its
 * offset, which a host that has one passes instead of 0. */
static void tell_time(RippleSession* session)
{
    RippleMoment now;
    now.utc_ms = (int64_t)time(NULL) * 1000;
    now.offset_minutes = 0;
    ripple_session_set_now(session, &now, NULL);
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

    tell_time(session);
    code = ripple_session_start(session, &batch, &error);
    while (code == RIPPLE_SUCCESS && batch != NULL && batch->count > 0)
    {
        const RippleEffect* turn = &batch->items[batch->count - 1];
        const RippleEffects* next = NULL;
        print_notices(batch);
        tell_time(session);
        switch (turn->kind)
        {
            case RIPPLE_EFFECT_KIND_PERMIT:
                code = ask_permits(session, turn->data.permit, &next, &error);
                break;
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
