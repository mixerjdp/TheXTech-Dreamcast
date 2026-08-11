/*
 * TheXTech Dreamcast — sound effects on the AICA.
 *
 * SOUND EFFECTS: host-converted Yamaha ADPCM WAVs via snd_sfx.
 *
 * MUSIC: sndoggvorbis (libtremor). Looping seeks (ov_raw_seek) are unreliable
 * on /cd and GD-ROM contention used to yield 2–8 s stubs of variable length.
 *
 * Strategy that actually sticks:
 *  1. Reserve a 768 KB buffer at Mix_OpenAudio.
 *  2. Under a CD lock (textures wait), copy the whole Ogg into that buffer
 *     using KOS fs_total + fs_read, verifying OggS + length.
 *  3. Play with fmemopen() + sndoggvorbis_start_fd() — seeks hit RAM, never
 *     the GD-ROM. Reopens reuse the SAME buffer; we never re-read /cd for the
 *     current track.
 *  4. Looping tracks never stream from /cd.
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <string>
#include <malloc.h>

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
    //! Last AICA channel this chunk used (for SDL "single-channel" SFX).
    int last_chn = -1;
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

// Biggest episode track today is ~400 KB at 24 kbps. Reserve once at boot.
static const std::size_t k_music_stage_cap = 768u * 1024u;
static unsigned char *s_music_stage_buf = nullptr;
static std::size_t s_music_buf_len = 0; // valid bytes currently in the buffer

// >0 while music is reading the GD-ROM — render must not open /cd textures.
static volatile int s_cd_lock = 0;

static char s_music_src_path[256];
static bool s_music_loop = false;
static bool s_music_active = false;
static bool s_music_pending = false;
static bool s_music_from_mem = false;
static int  s_music_reopen_cooldown = 0;
static int  s_music_stage_cooldown = 0;

bool Mix_DC_IsCdLocked(void)
{
    return s_cd_lock > 0;
}

static void s_cd_lock_acquire()
{
    ++s_cd_lock;
}

static void s_cd_lock_release()
{
    if(s_cd_lock > 0)
        --s_cd_lock;
}

static void s_clear_music_watch()
{
    s_music_src_path[0] = '\0';
    s_music_loop = false;
    s_music_active = false;
    s_music_pending = false;
    s_music_from_mem = false;
    s_music_reopen_cooldown = 0;
    s_music_stage_cooldown = 0;
    // Keep s_music_buf_len — buffer can be reused/replaced on next load.
}

static void s_watch_music(const char *src_path, bool loop, bool from_mem, bool pending)
{
    std::strncpy(s_music_src_path, src_path, sizeof(s_music_src_path) - 1);
    s_music_src_path[sizeof(s_music_src_path) - 1] = '\0';
    s_music_loop = loop;
    s_music_active = true;
    s_music_from_mem = from_mem;
    s_music_pending = pending;
    s_music_reopen_cooldown = pending ? 0 : 90;
    s_music_stage_cooldown = pending ? 60 : 0; // let textures settle before first pending try
}

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

    // Reserve the staging scratchpad while the heap is still empty. Doing this
    // at StartMusic (after OpenLevel) often fails malloc on 16 MB retail.
    if(!s_music_stage_buf)
        s_music_stage_buf = static_cast<unsigned char *>(memalign(32, k_music_stage_cap));

    s_audio_up = true;
    s_clear_music_watch();

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

    s_clear_music_watch();
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

    // CRITICAL: SDL mixer channel indices (0, 1, … from single-channel=1 in
    // sounds.ini) are NOT AICA hardware channels. Channels 0/1 are owned by
    // sndoggvorbis's stream — feeding them to snd_sfx_play_chn() is what made
    // every jump cut the music. Always auto-pick a free SFX channel.
    if(which >= 0 && chunk->last_chn >= 0)
        snd_sfx_stop(chunk->last_chn);

    const int chn = snd_sfx_play(chunk->sfx, vol, pan);
    if(chn >= 0)
        chunk->last_chn = chn;
    return chn;
}

int Mix_PlayChannel(int channel, Mix_Chunk* chunk, int loops)
{
    return Mix_PlayChannelVol(channel, chunk, loops, MIX_MAX_VOLUME);
}

int Mix_HaltChannel(int channel)
{
    if(!s_audio_up)
        return 0;

    // channel < 0 => stop all SFX (KOS already spares stream-owned channels).
    // channel >= 0 is an SDL mixer index, not an AICA channel — do not call
    // snd_sfx_stop(channel) or we kill the music stream on ch 0/1.
    if(channel < 0)
        snd_sfx_stop_all();

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
    {
        s_clear_music_watch();
        sndoggvorbis_stop();
    }
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

    // Pending = StartMusic accepted; in-memory copy still in flight.
    if(s_music_pending && s_music_active)
        return 1;

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

static long s_expected_music_bytes(const char *ogg_path)
{
    file_t fd = fs_open(ogg_path, O_RDONLY);
    if(fd < 0)
        return -1;

    const ssize_t total = fs_total(fd);
    fs_close(fd);
    return (total > 0) ? static_cast<long>(total) : -1;
}

static bool s_ogg_looks_sane(const unsigned char *buf, std::size_t expected)
{
    if(expected < 24u * 1024u)
        return false;
    if(!(buf[0] == 'O' && buf[1] == 'g' && buf[2] == 'g' && buf[3] == 'S'))
        return false;

    // Last Ogg page should also start with OggS somewhere near the end.
    const std::size_t tail_from = (expected > 64u * 1024u) ? (expected - 64u * 1024u) : 0;
    for(std::size_t i = expected - 4; i > tail_from; --i)
    {
        if(buf[i] == 'O' && buf[i + 1] == 'g' && buf[i + 2] == 'g' && buf[i + 3] == 'S')
            return true;
    }
    // Single-page tiny files still OK if header matched and size is large enough.
    return expected < 48u * 1024u;
}

static bool s_read_exact(const char *src_path, unsigned char *dst, std::size_t expected, int reopen_budget)
{
    std::size_t got = 0;

    while(got < expected && reopen_budget-- > 0)
    {
        file_t fd = fs_open(src_path, O_RDONLY);
        if(fd < 0)
        {
            thd_sleep(20);
            continue;
        }

        if(fs_total(fd) != static_cast<ssize_t>(expected))
        {
            fs_close(fd);
            thd_sleep(20);
            continue;
        }

        if(got > 0 && fs_seek(fd, static_cast<off_t>(got), SEEK_SET) < 0)
        {
            fs_close(fd);
            thd_sleep(20);
            continue;
        }

        int zero_streak = 0;
        while(got < expected)
        {
            const ssize_t n = fs_read(fd, dst + got, expected - got);
            if(n < 0)
                break;
            if(n == 0)
            {
                if(++zero_streak > 40)
                    break;
                thd_sleep(5);
                if(fs_seek(fd, static_cast<off_t>(got), SEEK_SET) < 0)
                    break;
                continue;
            }
            zero_streak = 0;
            got += static_cast<std::size_t>(n);
        }

        fs_close(fd);

        if(got == expected)
            return s_ogg_looks_sane(dst, expected);

        thd_sleep(30);
    }

    return false;
}

//! Copy the whole track into s_music_stage_buf. CD lock held by caller.
static bool s_load_music_buf(const char *src_path, int max_attempts)
{
    if(!s_music_stage_buf)
        return false;

    const long expected_l = s_expected_music_bytes(src_path);
    if(expected_l <= 0 || static_cast<std::size_t>(expected_l) > k_music_stage_cap)
        return false;

    const std::size_t expected = static_cast<std::size_t>(expected_l);
    if(expected < 24u * 1024u)
        return false;

    for(int attempt = 0; attempt < max_attempts; ++attempt)
    {
        if(attempt > 0)
            thd_sleep(40);

        if(s_read_exact(src_path, s_music_stage_buf, expected,
                        max_attempts > 1 ? 80 : 10))
        {
            s_music_buf_len = expected;
            return true;
        }
    }

    s_music_buf_len = 0;
    return false;
}

static bool s_play_mem(bool loop)
{
    if(!s_music_stage_buf || s_music_buf_len == 0)
        return false;

    sndoggvorbis_stop();
    // start_fd requires STATUS_READY; stop is async on the ogg thread.
    sndoggvorbis_wait_start();

    FILE *f = fmemopen(s_music_stage_buf, s_music_buf_len, "rb");
    if(!f)
        return false;

    if(sndoggvorbis_start_fd(f, loop ? 1 : 0) != 0)
        return false;

    return true;
}

void Mix_DC_PumpMusic()
{
    if(!s_audio_up || !s_music_active || s_music_src_path[0] == '\0')
        return;

    if(s_music_reopen_cooldown > 0)
        --s_music_reopen_cooldown;
    if(s_music_stage_cooldown > 0)
        --s_music_stage_cooldown;

    if(s_music_pending)
    {
        if(s_music_stage_cooldown > 0)
            return;

        s_music_stage_cooldown = 45;
        s_cd_lock_acquire();
        const bool ok = s_load_music_buf(s_music_src_path, /*max_attempts=*/1);
        s_cd_lock_release();
        if(!ok)
            return;

        if(!s_play_mem(/*loop=*/true))
        {
            s_music_active = false;
            s_music_pending = false;
            return;
        }

        s_music_pending = false;
        s_music_from_mem = true;
        s_music_reopen_cooldown = 90;
        return;
    }

    if(!s_music_from_mem)
        return;

    if(sndoggvorbis_isplaying())
        return;

    if(!s_music_loop || s_music_reopen_cooldown > 0)
        return;

    // Reopen from the in-memory copy — never touch /cd again for this track.
    s_music_reopen_cooldown = 60;
    if(!s_play_mem(/*loop=*/true))
        s_music_active = false;
}

int Mix_PlayMusicStream(Mix_Music* music, int loops)
{
    if(!s_audio_up || !music || !music->streamable)
        return -1;

    sndoggvorbis_stop();
    s_clear_music_watch();

    const bool want_loop = (loops != 0);
    const char *src_path = music->path.c_str();

    if(want_loop)
    {
        s_cd_lock_acquire();
        const bool ok = s_load_music_buf(src_path, /*max_attempts=*/12);
        s_cd_lock_release();

        if(ok && s_play_mem(/*loop=*/true))
        {
            s_watch_music(src_path, true, true, false);
            return 0;
        }

        // Stay silent until the pump can load a clean copy with the bus quiet.
        s_music_buf_len = 0;
        s_watch_music(src_path, true, false, true);
        return 0;
    }

    // One-shot jingles: still prefer memory so seeks/stop are clean; fall back
    // to a single /cd play only if the file is tiny enough to risk it.
    s_cd_lock_acquire();
    const bool ok = s_load_music_buf(src_path, /*max_attempts=*/4);
    s_cd_lock_release();
    if(ok && s_play_mem(/*loop=*/false))
    {
        s_watch_music(src_path, false, true, false);
        return 0;
    }

    sndoggvorbis_wait_start();
    if(sndoggvorbis_start(src_path, /*loop=*/0) != 0)
        return -1;
    s_watch_music(src_path, false, false, false);
    return 0;
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
