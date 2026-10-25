/*
 * The C header alone, in C99: open a bundle from its folder, play it to its
 * end by taking every passage's first available choice, leaving every form
 * and waiting out every hold at once, and give back everything the engine
 * handed out. A host with no C++ in it writes exactly this loop. The first
 * passage that counts down is let run out instead — begun, then expired at
 * once — and a story that asks before it begins is told no to every kind, at
 * a fixed instant handed in first.
 *
 *   ripple_c99 <bundle folder>
 */
#include <ripple/ripple.h>

#include <stdio.h>
#include <string.h>

static RippleStr span(const char* text)
{
    RippleStr s;
    s.ptr = text;
    s.len = strlen(text);
    return s;
}

static int fail(const char* what, RippleResult code, const RippleError* error)
{
    if (error != NULL)
    {
        fprintf(stderr, "%s: %d: %.*s\n", what, (int)code, (int)error->message.len,
                error->message.ptr);
        ripple_free(error);
    }
    else
    {
        fprintf(stderr, "%s: %d\n", what, (int)code);
    }
    return 1;
}

static uint32_t first_available(const RipplePassage* passage)
{
    size_t i;
    for (i = 0; i < passage->choice_count; i++)
    {
        if (passage->choices[i].available)
        {
            return (uint32_t)i;
        }
    }
    return RIPPLE_CHOICE_NONE;
}

/* Says no to every kind the story asked about: each answer but the last
 * answers nothing, and the last begins the story. */
static RippleResult refuse_all(RippleSession* session, const RipplePermit* permit,
                               const RippleEffects** next, const RippleError** error)
{
    RippleResult code = RIPPLE_SUCCESS;
    size_t i;
    for (i = 0; i < permit->kind_count && code == RIPPLE_SUCCESS; i++)
    {
        ripple_free(*next);
        *next = NULL;
        code = ripple_session_permit(session, permit->kinds[i], false, next, error);
    }
    return code;
}

/* A countdown's two verbs, as a host's clock would say them. */
static RippleResult run_out(RippleSession* session, const RippleEffects** next,
                            const RippleError** error)
{
    const RippleEffects* begun = NULL;
    RippleResult code = ripple_session_begin_countdown(session, &begun, error);
    ripple_free(begun);
    if (code != RIPPLE_SUCCESS)
    {
        return code;
    }
    return ripple_session_expire(session, next, error);
}

int main(int argc, char** argv)
{
    RippleCampaign* campaign = NULL;
    RippleSession* session = NULL;
    const RippleManifest* manifest = NULL;
    const RippleEffects* batch = NULL;
    const RippleError* error = NULL;
    RippleSessionConfig config;
    RippleMoment now;
    RippleResult code;
    int turns = 0;
    int ran_out = 0;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <bundle folder>\n", argv[0]);
        return 2;
    }
    if (strcmp(ripple_engine_contract(), "1.6") != 0)
    {
        fprintf(stderr, "this program binds contract 1.6, not %s\n", ripple_engine_contract());
        return 1;
    }

    code = ripple_campaign_open_dir(span(argv[1]), &campaign, &error);
    if (code != RIPPLE_SUCCESS)
    {
        return fail("open", code, error);
    }
    code = ripple_campaign_manifest(campaign, &manifest, &error);
    if (code != RIPPLE_SUCCESS)
    {
        return fail("manifest", code, error);
    }
    memset(&config, 0, sizeof config);
    config.board = manifest->entry_count > 0 ? manifest->entries[0] : 0;
    ripple_free(manifest);
    ripple_session_options_default(&config.options);
    code = ripple_session_create(campaign, &config, &session, &error);
    if (code != RIPPLE_SUCCESS)
    {
        return fail("create", code, error);
    }

    /* 2026-10-19T18:30:00+02:00, which a story reads only if it is let. */
    now.utc_ms = 1792427400000LL;
    now.offset_minutes = 120;
    code = ripple_session_set_now(session, &now, &error);
    if (code != RIPPLE_SUCCESS)
    {
        return fail("set_now", code, error);
    }

    code = ripple_session_start(session, &batch, &error);
    while (code == RIPPLE_SUCCESS && batch != NULL)
    {
        const RippleEffect* turn = batch->count > 0 ? &batch->items[batch->count - 1] : NULL;
        const RippleEffects* next = NULL;
        turns++;
        if (turn == NULL)
        {
            break;
        }
        switch (turn->kind)
        {
            case RIPPLE_EFFECT_KIND_SHOW:
                if (turn->data.show->timer != NULL && !ran_out)
                {
                    ran_out = 1;
                    code = run_out(session, &next, &error);
                }
                else
                {
                    code = ripple_session_choose(session, first_available(turn->data.show), &next,
                                                 &error);
                }
                break;
            case RIPPLE_EFFECT_KIND_PERMIT:
                code = refuse_all(session, turn->data.permit, &next, &error);
                break;
            case RIPPLE_EFFECT_KIND_ASK:
                code = ripple_session_answer(session, NULL, &next, &error);
                break;
            case RIPPLE_EFFECT_KIND_HOLD:
                code = ripple_session_elapsed(session, &next, &error);
                break;
            case RIPPLE_EFFECT_KIND_ENDED:
            case RIPPLE_EFFECT_KIND_FAILED:
            case RIPPLE_EFFECT_KIND_STOPPED:
            default:
                break;
        }
        ripple_free(batch);
        batch = next;
    }
    if (code != RIPPLE_SUCCESS)
    {
        return fail("play", code, error);
    }

    ripple_session_destroy(session);
    ripple_campaign_destroy(campaign);
    if (ripple_outstanding() != 0)
    {
        fprintf(stderr, "%lu things the engine handed out were never freed\n",
                (unsigned long)ripple_outstanding());
        return 1;
    }
    printf("played to the end in %d turns on engine %s\n", turns, ripple_engine_version());
    return turns > 1 ? 0 : 1;
}
