/*
 * TheXTech Dreamcast — sound effects on the AICA.
 *
 * Implements the subset of the SDL_mixer-ish API that TheXTech actually calls,
 * on top of KallistiOS's snd_sfx manager.
 *
 * SOUND EFFECTS work: the host converter turns the gamepack's .ogg effects into
 * Yamaha ADPCM WAVs (see utils/convertkit/gfx-convert-dc.py), which is one of
 * the formats snd_sfx_load understands natively, and which fits ~100 effects
 * into the AICA's 2 MB of sound RAM.
 *
 * MUSIC streams through KallistiOS's sndoggvorbis (kos-ports libtremor), which
 * decodes Ogg Vorbis on the SH4 with integer maths. The host converter turns
 * every track — including the SPC/NSF/IT ones, via ffmpeg's libgme demuxer —
 * into mono 22 kHz Ogg, which keeps the decode cost low enough to sit
 * alongside the game.
 *
 * Panning is accepted and ignored — everything plays centred. The engine's
 * spatial audio therefore has no effect, but nothing breaks.
 */

#include <cstring>
#include <cstdlib>
#include <new>
#include <string>

#include <kos.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <vorbis/sndoggvorbis.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

#include <SDL2/SDL_rwops.h>

#include "../mixer.h"
#include "globals.h"


struct Mix_Chunk
{
    sfxhnd_t sfx;
};

struct Mix_Music
{
    std::string path;

    //! Only Ogg tracks can go through sndoggvorbis.
    //
    // The engine also routes long sound effects through the music API
    // (Mix_LoadMUS on a file under sound/), and those are ADPCM WAVs here.
    // Letting them near the stream would stop whatever music is playing on
    // every single effect, which is exactly what silenced the game.
    bool streamable = false;
};

static bool s_audio_up = false;

// Counters for the startup diagnostic (see THEXTECH_DC_BOOT_PROBE).
int g_dc_sfx_loaded = 0;
int g_dc_sfx_failed = 0;

//! SDL volumes run 0..MIX_MAX_VOLUME (128); the AICA wants 0..255.
static int s_volume(int mix_volume)
{
    if(mix_volume < 0)
        mix_volume = 0;
    if(mix_volume > MIX_MAX_VOLUME)
        mix_volume = MIX_MAX_VOLUME;

    return mix_volume * 255 / MIX_MAX_VOLUME;
}

int Mix_Init(int flags)
{
    return flags;
}

void Mix_Quit()
{}

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
    (void)frequency;
    (void)format;
    (void)channels;
    (void)chunksize;

    if(s_audio_up)
        return 0;

    if(snd_init() < 0)
        return -1;

    // Spins up the Ogg streaming thread used for music.
    sndoggvorbis_init();

    s_audio_up = true;

    return 0;
}

int Mix_QuerySpecEx(SDL_AudioSpec* out_spec)
{
    memset(out_spec, 0, sizeof(SDL_AudioSpec));
    out_spec->channels = 2;
    out_spec->samples = 2048;
    out_spec->freq = 44100;
    out_spec->format = AUDIO_S16SYS;
    return 0;
}

void Mix_CloseAudio()
{
    if(!s_audio_up)
        return;

    sndoggvorbis_stop();
    sndoggvorbis_shutdown();
    snd_sfx_stop_all();
    snd_sfx_unload_all();
    snd_shutdown();
    s_audio_up = false;
}

int Mix_AllocateChannels(int numchans)
{
    return numchans;
}

int Mix_ReserveChannels(int channels)
{
    (void)channels;
    return 0;
}

Mix_Chunk* Mix_LoadWAV(const char* path)
{
    if(!s_audio_up || !path)
        return nullptr;

    sfxhnd_t sfx = snd_sfx_load(path);
    if(sfx == SFXHND_INVALID)
    {
        g_dc_sfx_failed++;
        return nullptr;
    }

    g_dc_sfx_loaded++;

    auto *chunk = new(std::nothrow) Mix_Chunk;
    if(!chunk)
    {
        snd_sfx_unload(sfx);
        return nullptr;
    }

    chunk->sfx = sfx;
    return chunk;
}

Mix_Chunk* Mix_LoadWAV_RW(struct SDL_RWops* rwops, int free_me)
{
    // snd_sfx_load works from a path or a file descriptor, neither of which we
    // can get back out of an RWops here.
    if(free_me)
        SDL_RWclose(rwops);

    return nullptr;
}

void Mix_FreeChunk(Mix_Chunk* chunk)
{
    if(!chunk)
        return;

    if(s_audio_up && chunk->sfx != SFXHND_INVALID)
        snd_sfx_unload(chunk->sfx);

    delete chunk;
}

int Mix_PlayChannelVol(int which, Mix_Chunk* chunk, int loops, int volume)
{
    // snd_sfx has no looping; effects that ask for it just play once.
    (void)loops;

    if(!s_audio_up || !chunk || chunk->sfx == SFXHND_INVALID)
        return -1;

    const int vol = s_volume(volume);
    const int pan = 128; // centred

    if(which < 0)
        return snd_sfx_play(chunk->sfx, vol, pan);

    return snd_sfx_play_chn(which, chunk->sfx, vol, pan);
}

int Mix_PlayChannel(int channel, Mix_Chunk* chunk, int loops)
{
    return Mix_PlayChannelVol(channel, chunk, loops, MIX_MAX_VOLUME);
}

int Mix_HaltChannel(int channel)
{
    if(!s_audio_up)
        return 0;

    if(channel < 0)
        snd_sfx_stop_all();
    else
        snd_sfx_stop(channel);

    return 0;
}

int Mix_SetPanning(int channel, uint8_t left, uint8_t right)
{
    // Accepted so the engine's spatial code keeps working; playback is centred.
    (void)channel;
    (void)left;
    (void)right;
    return 0;
}

void Mix_PauseAudio(int pause)
{
    if(s_audio_up && pause)
        snd_sfx_stop_all();
}

const char* Mix_GetError()
{
    return "";
}

void Mix_ChannelFinished(void (*cb)(int))
{
    // snd_sfx gives us no completion callback; the engine copes without it.
    UNUSED(cb);
}

// ---------------------------------------------------------------------------
// music — streamed from the disc by sndoggvorbis
// ---------------------------------------------------------------------------

int Mix_VolumeMusic(int volume)
{
    if(s_audio_up)
        sndoggvorbis_volume(s_volume(volume));
    return volume;
}

Mix_Music* Mix_LoadMUS(const char* path)
{
    if(!s_audio_up || !path)
        return nullptr;

    // Nothing is decoded up front: sndoggvorbis streams straight off the disc,
    // so a "loaded" track is just its path.
    auto *music = new(std::nothrow) Mix_Music;
    if(music)
    {
        music->path = path;
        music->streamable = music->path.size() > 4 &&
            music->path.compare(music->path.size() - 4, 4, ".ogg") == 0;
    }

    return music;
}

Mix_Music* Mix_LoadMUS_RW(struct SDL_RWops* rwops, int free_me)
{
    if(free_me)
        SDL_RWclose(rwops);
    return nullptr;
}

Mix_Music* Mix_LoadMUS_RW_ARG(struct SDL_RWops* rwops, int free_me, const char*)
{
    if(free_me)
        SDL_RWclose(rwops);
    return nullptr;
}

double Mix_MusicDuration(Mix_Music* music)
{
    UNUSED(music);
    return 0;
}

int Mix_VolumeMusicStream(Mix_Music* music, int volume)
{
    if(s_audio_up && music && music->streamable)
        sndoggvorbis_volume(s_volume(volume));
    return 0;
}

int Mix_HaltMusicStream(Mix_Music* music)
{
    // A non-streamable handle never started anything; stopping on its behalf
    // would cut off the real music.
    if(s_audio_up && music && music->streamable)
        sndoggvorbis_stop();
    return 0;
}

int Mix_FadeOutMusicStream(Mix_Music* music, int ms)
{
    UNUSED(ms);
    return Mix_HaltMusicStream(music);
}

int Mix_PlayingMusicStream(Mix_Music* music)
{
    if(!s_audio_up || !music || !music->streamable)
        return 0;

    return sndoggvorbis_isplaying();
}

int Mix_PausedMusicStream(Mix_Music* music)
{
    UNUSED(music);
    return -1;
}

int Mix_RewindMusicStream(Mix_Music* music)
{
    UNUSED(music);
    return -1;
}

int Mix_SetMusicEffectPanning(Mix_Music* music, uint8_t left, uint8_t right)
{
    UNUSED(music);
    UNUSED(left);
    UNUSED(right);
    return -1;
}

int Mix_PauseMusicStream(Mix_Music* music)
{
    UNUSED(music);
    return 0;
}

int Mix_ResumeMusicStream(Mix_Music* music)
{
    UNUSED(music);
    return 0;
}

int Mix_PlayMusic(Mix_Music* music, int loops)
{
    return Mix_PlayMusicStream(music, loops);
}

int Mix_PlayMusicStream(Mix_Music* music, int loops)
{
    if(!s_audio_up || !music || !music->streamable)
        return -1;

    sndoggvorbis_stop();

    // SDL uses -1 for "forever"; sndoggvorbis just wants a flag.
    int rc = sndoggvorbis_start(music->path.c_str(), loops != 0);

    return rc == 0 ? 0 : -1;
}

int Mix_SetFreeOnStop(Mix_Music* music, int free_on_stop)
{
    UNUSED(music);
    UNUSED(free_on_stop);
    return 0;
}

int Mix_FadeInMusic(Mix_Music* music, int loops, int fadeInMs)
{
    // No fading in the streamer; start at full volume.
    UNUSED(fadeInMs);
    return Mix_PlayMusicStream(music, loops);
}

const char* Mix_GetMusicTitle(Mix_Music* music)
{
    UNUSED(music);
    return "";
}

int Mix_GetMusicTracks(Mix_Music* music)
{
    UNUSED(music);
    return 1;
}

int Mix_SetMusicTrackMute(Mix_Music* music, int track, int mute)
{
    UNUSED(music);
    UNUSED(track);
    UNUSED(mute);
    return 0;
}

void Mix_FreeMusic(Mix_Music* music)
{
    delete music;
}

void Mix_GME_SetSpcEchoDisabled(Mix_Music* music, int disable)
{
    UNUSED(music);
    UNUSED(disable);
}

#ifdef THEXTECH_ENABLE_AUDIO_FX
void Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void* arg)
{
    UNUSED(chan);
    UNUSED(f);
    UNUSED(d);
    UNUSED(arg);
}

void Mix_UnregisterEffect(int chan, Mix_EffectFunc_t f)
{
    UNUSED(chan);
    UNUSED(f);
}
#endif

void Mix_ADLMIDI_setEmulator(int emu)
{
    UNUSED(emu);
}

void Mix_ADLMIDI_setChipsCount(int chips)
{
    UNUSED(chips);
}

void Mix_OPNMIDI_setEmulator(int emu)
{
    UNUSED(emu);
}

void Mix_OPNMIDI_setChipsCount(int chips)
{
    UNUSED(chips);
}
