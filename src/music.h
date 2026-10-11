// music.h - an original score, synthesized when the screensaver starts.
//
// Two pieces in D, both built on the same bright "space" harmony (the major
// chord a step above D sounding over a held D):
//   - the cruise theme, a soaring brass-like tune over pulsing bass, a
//     sparkling arpeggio and timpani, for flying at warp;
//   - the orbit theme, slow and calm, a soft gliding lead over drifting pads
//     and scattered bells, for circling a planet.
// Each is rendered once into a seamless loop. A streaming thread mixes them,
// crossfading as the ship's situation changes; the cruise theme starts again
// from its first bar every time the ship jumps to warp.
//
// Part of main.cpp.

static const int kMusicRate = 44100;

// ---------------------------------------------------------------------------
// Score

struct Note {
    float beat;  // when it starts
    float len;   // how long it lasts, beats
    int pitch;   // MIDI note number
};

// One chord per bar: a bass note and three notes for the pads.
struct BarChord {
    int bass;
    int pad[3];
};

// D, E/D, G, A, Bm, F#m and Em.
static const BarChord kD   = {38, {50, 54, 57}};
static const BarChord kEoD = {38, {52, 56, 59}};
static const BarChord kG   = {43, {55, 59, 62}};
static const BarChord kA   = {45, {57, 61, 64}};
static const BarChord kBm  = {47, {59, 62, 66}};
static const BarChord kFsm = {42, {54, 57, 61}};
static const BarChord kEm  = {40, {52, 55, 59}};

static const BarChord kCruiseChords[16] = {kD, kEoD, kD, kEoD, kG, kA, kBm, kA,
                                        kG, kFsm, kEm, kA, kD, kEoD, kG, kA};

static const Note kCruiseTune[] = {
    // An opening call, answered by the bright raised fourth.
    {0, 2, 69}, {2, 1, 66}, {3, 1, 69},
    {4, 3, 71}, {7, 1, 68},
    {8, 2, 69}, {10, 2, 74},
    {12, 2, 73}, {14, 1, 71}, {15, 1, 68},
    // Climbing.
    {16, 1.5f, 71}, {17.5f, 0.5f, 74}, {18, 2, 76},
    {20, 1, 73}, {21, 1, 76}, {22, 2, 81},
    {24, 3, 78}, {27, 1, 74},
    {28, 4, 76},
    // Turning back down.
    {32, 1, 74}, {33, 1, 71}, {34, 2, 67},
    {36, 1, 69}, {37, 1, 73}, {38, 2, 78},
    {40, 1.5f, 79}, {41.5f, 0.5f, 78}, {42, 1, 76}, {43, 1, 71},
    {44, 2, 73}, {46, 1, 76}, {47, 1, 69},
    // Home.
    {48, 2, 78}, {50, 1, 76}, {51, 1, 74},
    {52, 3, 80}, {55, 1, 76},
    {56, 1, 74}, {57, 1, 71}, {58, 1, 67}, {59, 1, 71},
    {60, 4, 69},
};

static const BarChord kOrbitChords[8] = {kD, kEoD, kBm, kG, kD, kEoD, kG, kA};

static const Note kOrbitTune[] = {
    {0, 3, 69}, {3, 1, 66},
    {4, 2, 68}, {6, 2, 71},
    {8, 3, 74}, {11, 1, 73},
    {12, 4, 71},
    {16, 2, 69}, {18, 2, 74},
    {20, 3, 76}, {23, 1, 68},
    {24, 2, 71}, {26, 1, 74}, {27, 1, 76},
    {28, 4, 73},
};

// ---------------------------------------------------------------------------
// Synthesis

static float NoteHz(float pitch) { return 440.0f * powf(2.0f, (pitch - 69.0f) / 12.0f); }

// A stereo loop; notes running past its end wrap round to its start.
struct Loop {
    std::vector<float> l, r;
    size_t loopStart = 0;  // after the end, play carries on from here
    void Init(size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }
    size_t Size() const { return l.size(); }
    size_t Next(size_t i) const { return i + 1 < l.size() ? i + 1 : loopStart; }
    void Add(size_t i, float a, float pan)  // pan -1 (left) .. 1 (right)
    {
        i %= l.size();
        l[i] += a * (1.0f - pan) * 0.5f;
        r[i] += a * (1.0f + pan) * 0.5f;
    }
};

static float Env(float t, float len, float attack, float release)
{
    if (t < 0) return 0;
    float a = std::min(1.0f, t / attack);
    if (t > len) a *= std::max(0.0f, 1.0f - (t - len) / release);
    return a;
}

// Soft, detuned sawtooth pads through a gentle low-pass filter.
static void Pad(Loop& out, float start, float len, int pitch, float amp, float attack, float release, float cutoff)
{
    const float hz = NoteHz((float)pitch);
    const float detune[3] = {0.9985f, 1.0f, 1.0016f};
    const float pans[3] = {-0.7f, 0.0f, 0.7f};
    const float k = 1.0f - expf(-6.2831853f * cutoff / kMusicRate);
    size_t s0 = (size_t)(start * kMusicRate), n = (size_t)((len + release) * kMusicRate);
    for (int v = 0; v < 3; v++) {
        float ph = v * 0.31f, lp = 0, lp2 = 0;
        float step = hz * detune[v] / kMusicRate;
        for (size_t i = 0; i < n; i++) {
            ph += step;
            ph -= floorf(ph);
            float saw = 2.0f * ph - 1.0f;
            lp += (saw - lp) * k;
            lp2 += (lp - lp2) * k;
            float t = (float)i / kMusicRate;
            out.Add(s0 + i, lp2 * amp * Env(t, len, attack, release), pans[v]);
        }
    }
}

// A brassy lead: sawtooth and square through a filter that opens as each note
// starts, with vibrato that grows as the note is held.
static void Brass(Loop& out, float start, float len, int pitch, float amp, float pan)
{
    const float hz = NoteHz((float)pitch);
    size_t s0 = (size_t)(start * kMusicRate), n = (size_t)((len + 0.3f) * kMusicRate);
    float ph = 0, ph2 = 0, lp = 0, lp2 = 0;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kMusicRate;
        float vib = 1.0f + 0.004f * std::min(1.0f, std::max(0.0f, t - 0.25f) / 0.4f) * sinf(6.2831853f * 5.5f * t);
        ph += hz * vib / kMusicRate;
        ph -= floorf(ph);
        ph2 += hz * 0.5f * vib / kMusicRate;
        ph2 -= floorf(ph2);
        float saw = 2.0f * ph - 1.0f, sq = ph < 0.5f ? 0.6f : -0.6f;
        float sub = 2.0f * ph2 - 1.0f;
        float cutoff = 900.0f + 2600.0f * expf(-t * 4.0f) + 900.0f;
        float k = 1.0f - expf(-6.2831853f * cutoff / kMusicRate);
        lp += ((saw + sq * 0.5f + sub * 0.35f) - lp) * k;
        lp2 += (lp - lp2) * k;
        out.Add(s0 + i, lp2 * amp * Env(t, len, 0.06f, 0.25f), pan);
    }
}

// A soft gliding lead: a sine with a touch of its octave and a wide vibrato.
static void Glide(Loop& out, float start, float len, int pitch, float amp)
{
    const float hz = NoteHz((float)pitch);
    size_t s0 = (size_t)(start * kMusicRate), n = (size_t)((len + 0.8f) * kMusicRate);
    float ph = 0;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kMusicRate;
        float vib = 1.0f + 0.006f * std::min(1.0f, t / 0.8f) * sinf(6.2831853f * 4.5f * t);
        ph += hz * vib / kMusicRate;
        ph -= floorf(ph);
        float s = sinf(6.2831853f * ph) + 0.15f * sinf(12.566371f * ph);
        out.Add(s0 + i, s * amp * Env(t, len, 0.25f, 0.8f), 0.0f);
    }
}

// A bell or glockenspiel note.
static void Bell(Loop& out, float start, int pitch, float amp, float pan, float decay)
{
    const float hz = NoteHz((float)pitch);
    size_t s0 = (size_t)(start * kMusicRate), n = (size_t)(decay * 5.0f * kMusicRate);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kMusicRate;
        float s = sinf(6.2831853f * hz * t) + 0.35f * sinf(6.2831853f * hz * 2.76f * t) * expf(-t * 6.0f);
        out.Add(s0 + i, s * amp * expf(-t / decay) * std::min(1.0f, t * 400.0f), pan);
    }
}

// A plucked bass note.
static void Bass(Loop& out, float start, float len, int pitch, float amp)
{
    const float hz = NoteHz((float)pitch);
    size_t s0 = (size_t)(start * kMusicRate), n = (size_t)((len + 0.1f) * kMusicRate);
    float ph = 0, lp = 0;
    const float k = 1.0f - expf(-6.2831853f * 500.0f / kMusicRate);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kMusicRate;
        ph += hz / kMusicRate;
        ph -= floorf(ph);
        lp += ((2.0f * ph - 1.0f) - lp) * k;
        float s = sinf(6.2831853f * ph) * 0.8f + lp * 0.5f;
        out.Add(s0 + i, s * amp * Env(t, len, 0.005f, 0.1f) * (0.55f + 0.45f * expf(-t * 6.0f)), 0.0f);
    }
}

// A timpani stroke: a sine that settles onto its pitch, and a thump of noise.
static void Timpani(Loop& out, float start, int pitch, float amp, unsigned& seed)
{
    const float hz = NoteHz((float)pitch);
    size_t s0 = (size_t)(start * kMusicRate), n = (size_t)(2.0f * kMusicRate);
    float ph = 0, lp = 0;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kMusicRate;
        ph += hz * (1.0f + 0.5f * expf(-t * 40.0f)) / kMusicRate;
        seed = seed * 1664525u + 1013904223u;
        float noise = (seed >> 8) / 8388608.0f - 1.0f;
        lp += (noise - lp) * 0.08f;
        float s = sinf(6.2831853f * ph) * expf(-t * 2.2f) + lp * 2.0f * expf(-t * 25.0f);
        out.Add(s0 + i, s * amp, 0.0f);
    }
}

// A cymbal: a swell of bright noise leading into a crash, or just a crash.
static void Cymbal(Loop& out, float start, float swell, float amp, unsigned& seed)
{
    size_t s0 = (size_t)((start - swell) * kMusicRate), n = (size_t)((swell + 3.0f) * kMusicRate);
    float prev = 0, hp = 0;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kMusicRate - swell;  // 0 at the crash
        seed = seed * 1664525u + 1013904223u;
        float noise = (seed >> 8) / 8388608.0f - 1.0f;
        hp = 0.92f * (hp + noise - prev);
        prev = noise;
        float e = t < 0 ? powf(1.0f + t / swell, 3.0f) * 0.6f : expf(-t * 1.6f);
        out.Add(s0 + i, hp * amp * e, ((seed >> 4) & 1) ? 0.3f : -0.3f);
    }
}

// Schroeder reverb, run twice round the loop so the tail of its end carries
// on into its start.
static void Reverb(Loop& loop, float wet)
{
    const int combLen[4] = {1557, 1617, 1491, 1422};
    const int apLen[2] = {556, 225};
    std::vector<float>* chans[2] = {&loop.l, &loop.r};
    for (int c = 0; c < 2; c++) {
        std::vector<float>& x = *chans[c];
        std::vector<float> combs[4], aps[2];
        int ci[4] = {}, ai[2] = {};
        float damp[4] = {};
        for (int k = 0; k < 4; k++) combs[k].assign(combLen[k] + c * 23, 0.0f);
        for (int k = 0; k < 2; k++) aps[k].assign(apLen[k] + c * 11, 0.0f);
        std::vector<float> out(x.size());
        for (int pass = 0; pass < 2; pass++) {
            for (size_t i = 0; i < x.size(); i++) {
                float in = x[i] * 0.25f, sum = 0;
                for (int k = 0; k < 4; k++) {
                    float y = combs[k][ci[k]];
                    damp[k] = y * 0.6f + damp[k] * 0.4f;
                    combs[k][ci[k]] = in + damp[k] * 0.84f;
                    ci[k] = (ci[k] + 1) % (int)combs[k].size();
                    sum += y;
                }
                for (int k = 0; k < 2; k++) {
                    float b = aps[k][ai[k]];
                    float y = -sum + b;
                    aps[k][ai[k]] = sum + b * 0.5f;
                    ai[k] = (ai[k] + 1) % (int)aps[k].size();
                    sum = y;
                }
                out[i] = sum;
            }
        }
        for (size_t i = 0; i < x.size(); i++) x[i] += out[i] * wet;
    }
}

static void Normalize(Loop& loop, float peak)
{
    float m = 1e-6f;
    for (size_t i = 0; i < loop.Size(); i++) m = std::max(m, std::max(fabsf(loop.l[i]), fabsf(loop.r[i])));
    for (size_t i = 0; i < loop.Size(); i++) {
        loop.l[i] *= peak / m;
        loop.r[i] *= peak / m;
    }
}

static void ComposeCruise(Loop& out)
{
    const float beat = 0.6f;  // 100 beats a minute
    const int bars = 16;
    out.Init((size_t)(bars * 4 * beat * kMusicRate));
    unsigned seed = 12345;
    for (int b = 0; b < bars; b++) {
        const BarChord& c = kCruiseChords[b];
        float t0 = b * 4 * beat;
        for (int p : c.pad) Pad(out, t0, 4 * beat, p, 0.05f, 0.25f, 0.6f, 1800.0f);
        // Driving eighth-note bass, leaning on the first and third beats.
        for (int e = 0; e < 8; e++) Bass(out, t0 + e * beat * 0.5f, beat * 0.4f, c.bass, (e % 4 == 0) ? 0.30f : 0.20f);
        // Sparkling sixteenth-note arpeggio high above.
        static const int order[8] = {0, 1, 2, 1, 2, 0, 2, 1};
        for (int s = 0; s < 16; s++) {
            int pitch = c.pad[order[s % 8]] + (s % 8 < 4 ? 24 : 36);
            Bell(out, t0 + s * beat * 0.25f, pitch, 0.035f, (s % 2) ? 0.5f : -0.5f, 0.18f);
        }
        if (b % 4 == 0) Timpani(out, t0, c.bass, 0.45f, seed);
        if (b % 4 == 2) Timpani(out, t0 + 2 * beat, c.bass, 0.25f, seed);
    }
    // A timpani roll and a cymbal swell back into the top of the loop.
    for (int s = 0; s < 8; s++) Timpani(out, (bars * 4 - 2) * beat + s * beat * 0.25f, 45, 0.1f + 0.03f * s, seed);
    Cymbal(out, bars * 4 * beat, 4 * beat, 0.09f, seed);
    Cymbal(out, 8 * 4 * beat, 2 * beat, 0.05f, seed);
    for (const Note& n : kCruiseTune) {
        Brass(out, n.beat * beat, n.len * beat * 0.95f, n.pitch, 0.16f, 0.0f);
        Brass(out, n.beat * beat, n.len * beat * 0.95f, n.pitch - 12, 0.07f, 0.0f);
    }
    Reverb(out, 0.35f);
    Normalize(out, 0.9f);
}

static void ComposeOrbit(Loop& out)
{
    const float beat = 1.0f;  // 60 beats a minute
    const int bars = 8;
    out.Init((size_t)(bars * 4 * beat * kMusicRate));
    unsigned seed = 777;
    for (int b = 0; b < bars; b++) {
        const BarChord& c = kOrbitChords[b];
        float t0 = b * 4 * beat;
        for (int p : c.pad) Pad(out, t0, 4 * beat, p, 0.05f, 1.5f, 2.0f, 1100.0f);
        Pad(out, t0, 4 * beat, c.pad[0] - 12, 0.035f, 1.5f, 2.0f, 700.0f);
        Glide(out, t0, 4 * beat * 0.9f, c.bass, 0.10f);
        // Bells, scattered over the chord.
        for (int s = 0; s < 8; s++) {
            seed = seed * 1664525u + 1013904223u;
            if ((seed >> 16) % 10 < 4) continue;
            int pitch = c.pad[(seed >> 8) % 3] + 24 + ((seed >> 20) % 2) * 12;
            Bell(out, t0 + s * beat * 0.5f, pitch, 0.05f, ((seed >> 12) % 200) / 100.0f - 1.0f, 0.9f);
        }
    }
    for (const Note& n : kOrbitTune) Glide(out, n.beat * beat, n.len * beat * 0.95f, n.pitch, 0.13f);
    Reverb(out, 0.55f);
    Normalize(out, 0.55f);  // about as loud overall as the cruise theme
}

// ---------------------------------------------------------------------------
// A recording to fly to: if this file is beside the program, it plays at
// warp in place of the cruise theme, from the beginning each time the ship
// jumps to warp, and starts over if it ends while the ship is still at warp.

static const wchar_t* kTrackFile = L"geoffharvey-starfleet-command-150605.mp3";
static const float kTrackLoop = 0.0f;  // where it starts over from, seconds
static const float kTrackEnd = 0.0f;   // where it stops, seconds; 0 for the end of the file

// Decodes an audio file (anything Windows can play), or the first endAt
// seconds of it, into a loop.
static bool LoadTrack(const wchar_t* path, float loopAt, float endAt, Loop& out)
{
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return false;
    if (FAILED(MFStartup(MF_VERSION))) return false;
    std::vector<int16_t> pcm;
    UINT32 rate = 0, channels = 0;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* type = nullptr;
    if (SUCCEEDED(MFCreateSourceReaderFromURL(path, nullptr, &reader)) &&
        SUCCEEDED(MFCreateMediaType(&type))) {
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        IMFMediaType* actual = nullptr;
        if (SUCCEEDED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type)) &&
            SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual))) {
            actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            actual->Release();
        }
        const size_t want = endAt > 0 ? (size_t)(endAt * rate) * std::max(1u, channels) : SIZE_MAX;
        while (rate && channels && pcm.size() < want) {
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample)))
                break;
            if (sample) {
                IMFMediaBuffer* buf = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                        pcm.insert(pcm.end(), (int16_t*)data, (int16_t*)(data + len));
                        buf->Unlock();
                    }
                    buf->Release();
                }
                sample->Release();
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
    }
    if (type) type->Release();
    if (reader) reader->Release();
    MFShutdown();
    if (!rate || !channels || pcm.size() < (size_t)rate * channels) return false;

    // Resample to the music's rate, in stereo.
    const size_t frames = pcm.size() / channels;
    const double step = (double)rate / kMusicRate;
    size_t n = (size_t)((frames - 1) / step);
    if (endAt > 0) n = std::min(n, (size_t)(endAt * kMusicRate));
    out.Init(n);
    for (size_t i = 0; i < n; i++) {
        double src = i * step;
        size_t a = (size_t)src;
        float f = (float)(src - a);
        for (int c = 0; c < 2; c++) {
            int ch = std::min(c, (int)channels - 1);
            float v = (pcm[a * channels + ch] * (1 - f) + pcm[(a + 1) * channels + ch] * f) / 32768.0f;
            (c ? out.r : out.l)[i] = v;
        }
    }
    out.loopStart = std::min((size_t)(loopAt * kMusicRate), n - 1);

    // Blend the last moment before the end into what leads up to the loop
    // point, so going round again doesn't click.
    const size_t blend = std::min<size_t>(2048, out.loopStart);
    for (size_t j = 0; j < blend; j++) {
        float w = (float)j / blend;
        size_t at = n - blend + j, from = out.loopStart - blend + j;
        out.l[at] = out.l[at] * (1 - w) + out.l[from] * w;
        out.r[at] = out.r[at] * (1 - w) + out.r[from] * w;
    }

    // About as loud overall as the orbit theme, without clipping.
    double sum = 0;
    float peak = 1e-6f;
    for (size_t i = 0; i < n; i++) {
        sum += out.l[i] * out.l[i] + out.r[i] * out.r[i];
        peak = std::max(peak, std::max(fabsf(out.l[i]), fabsf(out.r[i])));
    }
    float gain = std::min(0.15f / (float)sqrt(sum / (2.0 * n) + 1e-12), 0.95f / peak);
    for (size_t i = 0; i < n; i++) {
        out.l[i] *= gain;
        out.r[i] *= gain;
    }
    return true;
}

// The folder the program is in.
static std::wstring ProgramFolder()
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, len);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : s.substr(0, slash + 1);
}

// ---------------------------------------------------------------------------
// Playback

class Music {
public:
    // Composes the music and starts playing it, in the background.
    void Start()
    {
        thread = CreateThread(nullptr, 0, Run, this, 0, nullptr);
    }

    // true while flying at warp, false while at rest or orbiting. The warp
    // music starts from its beginning each time this turns true.
    void SetCruising(bool on)
    {
        if (on && !cruising) jumps++;
        cruising = on;
    }

    void Stop()
    {
        if (!thread) return;
        quit = true;
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        thread = nullptr;
    }

private:
    static DWORD WINAPI Run(void* self)
    {
        ((Music*)self)->Play();
        return 0;
    }

    void Play()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Loop cruise, orbit;
        ComposeOrbit(orbit);
        if (quit) return;
        if (!LoadTrack((ProgramFolder() + kTrackFile).c_str(), kTrackLoop, kTrackEnd, cruise)) ComposeCruise(cruise);
        if (quit) return;

        WAVEFORMATEX fmt = {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = kMusicRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = kMusicRate * 4;
        HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HWAVEOUT dev = nullptr;
        if (waveOutOpen(&dev, WAVE_MAPPER, &fmt, (DWORD_PTR)ready, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
            CloseHandle(ready);
            return;
        }

        const int kBuffers = 4, kFrames = 2048;
        std::vector<int16_t> data[kBuffers];
        WAVEHDR headers[kBuffers] = {};
        size_t posCruise = 0, posOrbit = 0;
        unsigned jumpsHeard = 0;
        float gainCruise = 0, gainOrbit = 0, master = 0;
        const float fade = 1.0f - expf(-1.0f / (2.5f * kMusicRate));  // crossfade over a few seconds

        auto fill = [&](int b) {
            for (int i = 0; i < kFrames; i++) {
                float wantCruise = cruising ? 1.0f : 0.0f;
                // The warp music begins again from the top each time the
                // ship jumps to warp.
                if (jumps != jumpsHeard) {
                    jumpsHeard = jumps;
                    posCruise = 0;
                    gainCruise = 0;
                }
                gainCruise += (wantCruise - gainCruise) * fade;
                gainOrbit += ((1.0f - wantCruise) - gainOrbit) * fade;
                master += (1.0f - master) * fade;
                float l = cruise.l[posCruise] * gainCruise + orbit.l[posOrbit] * gainOrbit;
                float r = cruise.r[posCruise] * gainCruise + orbit.r[posOrbit] * gainOrbit;
                posCruise = cruise.Next(posCruise);
                posOrbit = orbit.Next(posOrbit);
                const float vol = 0.7f * master * 32767.0f;
                data[b][i * 2] = (int16_t)std::clamp(l * vol, -32767.0f, 32767.0f);
                data[b][i * 2 + 1] = (int16_t)std::clamp(r * vol, -32767.0f, 32767.0f);
            }
        };

        for (int b = 0; b < kBuffers; b++) {
            data[b].resize(kFrames * 2);
            fill(b);
            headers[b].lpData = (LPSTR)data[b].data();
            headers[b].dwBufferLength = kFrames * 4;
            waveOutPrepareHeader(dev, &headers[b], sizeof(WAVEHDR));
            waveOutWrite(dev, &headers[b], sizeof(WAVEHDR));
        }
        while (!quit) {
            WaitForSingleObject(ready, 100);
            for (int b = 0; b < kBuffers && !quit; b++) {
                if (!(headers[b].dwFlags & WHDR_DONE)) continue;
                fill(b);
                headers[b].dwFlags &= ~WHDR_DONE;
                waveOutWrite(dev, &headers[b], sizeof(WAVEHDR));
            }
        }
        waveOutReset(dev);
        for (int b = 0; b < kBuffers; b++) waveOutUnprepareHeader(dev, &headers[b], sizeof(WAVEHDR));
        waveOutClose(dev);
        CloseHandle(ready);
    }

    HANDLE thread = nullptr;
    std::atomic<bool> quit{false};
    std::atomic<bool> cruising{false};
    std::atomic<unsigned> jumps{0};
};
