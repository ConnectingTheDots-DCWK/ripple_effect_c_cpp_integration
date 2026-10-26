# The speaker contract

How a campaign's soundscapes reach a host's audio — miniaudio, SDL, FMOD,
Wwise, XAudio, OpenAL, or a game engine's own mixer. **This repository ships
no speaker.** The audio APIs a C or C++ host might use have nothing in
common, and every one of them is a dependency worth more than the engine.
What ships is the seam: the engine's scheduler decides every voice, and a
speaker of yours plays them. It binds **Ripple Effect contract 1.6**, and
transcribes the engine's `host-audio-system.md` §9, the source of truth.

## Three layers, and the one you write

1. **The session** says what the story asks for, as `audio` effects: `play`
   a soundscape, `stop` it, `cut` everything, `cue` a sound.
2. **The scheduler** decides every voice from those: which file, from where,
   how loud, when it fades, when one soundscape hands over to the next. It is
   the engine's mixer (`ripple_mixer_*`, `ripple::Mixer`), with no clock, no
   file and no voice of its own. **`ripple::SoundscapeDriver`** drives it: it
   forwards the session's commands, ticks it from your loop, and does every
   duty the scheduler asks of a host.
3. **The speaker** makes sounds. It is the only part that touches an audio
   API, and the only part you write.

```cpp
MySpeaker speaker;                                     // yours
auto audio = ripple::SoundscapeDriver::create(campaign, speaker);
ripple::PlayerHandlers handlers;
handlers.audio = audio.get();
ripple::Player player(ripple::Session::create(campaign, config), handlers);
// then, every frame:  audio->update();  player.update();
```

A host in plain C drives `ripple_mixer_*` itself and owes the scheduler what
the driver does — the list under *What the driver does* below.

## The interface

```cpp
class Speaker {
  public:
    virtual std::unique_ptr<Voice> open(const VoiceSource& source, VoiceRole role,
                                        VoiceEvents events) = 0;
    virtual bool resolves(const VoiceSource& source) { return true; }
};

class Voice {
  public:
    virtual void play(const VoicePlay& request) = 0;
    virtual void set_gain(double gain) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
};

struct VoicePlay {
    double from_ms;               // into the file
    std::optional<double> to_ms;  // the file's end when absent
    double gain;                  // 0–1, on the engine's gain scale (below)
    double rate;                  // 1 is as recorded
    bool looping;
    double loop_from_ms;          // where each turn of a loop comes round to
};

struct VoiceSource {
    std::uint32_t media;              // the medium, by index
    std::string path;                 // inside the bundle
    std::optional<std::string> file;  // on disk, for a campaign opened from a folder
};
```

## What each part must do

- **`open(source, role, events)`** returns once the voice can begin *at
  once*: the file opened, or enough of it decoded. It returns **nullptr**
  when the file cannot be played, and the driver tells the scheduler, which
  hears that as silence and never retries. A `track` may be minutes long and
  is usually better streamed; a `cue` is a short sound that must start on
  time and is usually better decoded whole. The role is a hint about
  strategy, not a different contract. **Keep `events`** with the voice.
- **`play(request)`** begins at once and replaces whatever the voice was
  playing. It may be called again on the same voice: a gapless bed whose file
  ended, coming round, which should follow on as closely as the API allows.
  It plays from `from_ms` to `to_ms`, or to the file's end. When `looping`,
  reaching `to_ms` comes round to **`loop_from_ms`, not `from_ms`**: a bed
  joined partway in loops over its whole clip.
- **`events.ended()`** once per play, when a non-looping voice reaches the
  end of what it was given. Never after `stop()`, never for a looping voice.
- **`events.duration(ms)`** once, when the file's length is known. A track
  played to its file's end has no end the scheduler can plan a fade-out or a
  gapless turn for until then.
- **Both `events` calls are thread-safe** and may come from an audio
  thread: the driver hears them on its next `update()`. A call from a voice
  that was stopped meanwhile is ignored.
- **`set_gain`** writes a new gain. The scheduler writes one every tick a
  fade moves, a step at a time, so smooth between them over a few
  milliseconds, or a fade is a staircase.
- **`pause` and `resume`** hold a voice exactly where it is and let it go
  again. The driver pauses every voice together.
- **`stop`** ends the voice for good and lets its resources go.
- **`resolves(source)`** is asked before a soundscape plays: a track whose
  file your speaker cannot play is told to the scheduler as silence up front,
  rather than tried. Yes unless you override it.

**The gain scale is cubic.** The editor writes a gain to mpv's volume
control, whose curve is cubic, so a speaker writing amplitude writes **`gain
* gain * gain`**: half the gain is an eighth of the amplitude. A speaker that
writes the gain as amplitude mixes every soundscape louder in its quiet parts
than its author heard it.

## What the driver does, so the speaker does not

All of this is `ripple::SoundscapeDriver`'s and must not be repeated in a
speaker — and is what a C host driving `ripple_mixer_*` itself owes:
- **the clock and the tick**: `update()` every frame or so; the editor ticks
  every 25 ms, and nothing is lost by more often;
- **the scheduler's commands**, applied in order;
- **a late voice**: the time `open` took, times `rate`, is added to `from_ms`,
  and a looping clip wraps inside its loop. A clip already over plays
  nothing and is reported ended;
- **voices reported ended or measured**, told to the scheduler;
- **tracks with no file**, told as silence rather than tried;
- **mute**, which is the scheduler's: it writes zero and remembers;
- **the handover between soundscapes**, which never overlap: the one
  leaving fades out over its own `fade_out` before the next begins.

## Mapping notes

These are notes, not code: nothing here links an audio library.

- **miniaudio**: a `ma_sound` per voice, from `ma_sound_init_from_file`
  with `MA_SOUND_FLAG_STREAM` for a track and `MA_SOUND_FLAG_DECODE` for a
  cue; `ma_sound_seek_to_pcm_frame` for `from_ms`, `ma_sound_set_pitch` for
  `rate`, `ma_sound_set_volume(gain³)`, and `ma_sound_set_end_callback`
  posting `events.ended()`. A loop to `loop_from_ms` is
  `ma_data_source_set_loop_point_in_pcm_frames` on the sound's data source.
- **SDL3**: an `SDL_AudioStream` per voice fed from a decoder you choose;
  the gain is `SDL_SetAudioStreamGain(gain³)`.
- **FMOD Core**: `FMOD_CREATESTREAM` for a track, `FMOD_CREATESAMPLE` for a
  cue; `Channel::setPosition`, `setPitch`, `setVolume(gain³)`,
  `setLoopPoints(loop_from_ms, to_ms)`, and an `FMOD_CHANNELCONTROL_CALLBACK_END`
  callback posting `events.ended()`. `Sound::getLength` is `duration`.
- **A game engine's own audio** usually has all of this under other names;
  the cubic gain is the line most often missed.

## Proving a speaker

`tests/driver/driver_test.cpp` is how the driver is held to the scheduler, on
a speaker that writes down what it is told. A speaker of your own should
pass the shape the JS binding's Web Audio speaker is held to in three
browsers:
- a file ends near its length, and reports it;
- a clip ends near its `to_ms`;
- a loop never ends;
- a cue ends near its length;
- a paused cue ends later by the pause;
- a stopped voice never ends.

**And hear it**, with `examples/cpp_player` over a real campaign and your
speaker in place of the printing one: a test can count voices, but only a
person can hear a seam.
