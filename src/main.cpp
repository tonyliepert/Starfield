// Starfield - a Windows screensaver.
//
// A ship flying through deep space at extreme speed. Stars stream out of the
// vanishing point and streak past, stretched into lines by their motion,
// blue-shifted ahead and red-shifted behind, and the ship flies on through
// faint nebulae. Passing close to a star, the ship is pulled round by its
// gravity and changes course; if it comes very close, the ship drops out of
// warp, circles twice round a planet of that star and flies on. The star may
// be a red giant, a yellow star, a brown dwarf, a neutron star, or a red
// giant with a neutron star pulling a stream of gas off it, and the
// planet any of ten kinds, each a little different every time, with up to
// three moons, and lights, fields and roads on its land if it is habitable.
//
// Command line (standard screensaver protocol):
//   /s          run full screen
//   /p <hwnd>   run inside the preview window of the Screen Saver Settings dialog
//   /c          settings (there are none, shows an about box)
// Extra switches:
//   /w          run in a normal resizable window (Esc closes); this is also
//               the default when the program is named Starfield.exe
//   /x <name> [seconds]
//               fly for that many seconds, then render three frames to
//               <name>_0.bmp .. <name>_2.bmp and exit
// In a window, pressing S saves what is on the screen as a JPEG in the
// Downloads folder.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <shlobj.h>
#include <GL/gl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

// Prefer the discrete GPU on hybrid-graphics laptops.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// ---------------------------------------------------------------------------
// OpenGL 2/3 entry points (gl.h on Windows only covers OpenGL 1.1)

typedef char GLchar;
#define GL_CLAMP_TO_EDGE          0x812F
#define GL_TEXTURE0               0x84C0
#define GL_RGBA16F                0x881A
#define GL_FRAGMENT_SHADER        0x8B30
#define GL_VERTEX_SHADER          0x8B31
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5
#define GL_COLOR_ATTACHMENT0      0x8CE0
#define GL_FRAMEBUFFER            0x8D40
#define GL_RENDERBUFFER           0x8D41
#define GL_DEPTH_ATTACHMENT       0x8D00
#define GL_DEPTH_COMPONENT24      0x81A6

#define GL_FUNCTIONS(X) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void,   glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void,   glCompileShader, (GLuint)) \
    X(void,   glGetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void,   glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void,   glAttachShader, (GLuint, GLuint)) \
    X(void,   glBindAttribLocation, (GLuint, GLuint, const GLchar*)) \
    X(void,   glLinkProgram, (GLuint)) \
    X(void,   glGetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void,   glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void,   glUseProgram, (GLuint)) \
    X(GLint,  glGetUniformLocation, (GLuint, const GLchar*)) \
    X(void,   glUniform1i, (GLint, GLint)) \
    X(void,   glUniform1f, (GLint, GLfloat)) \
    X(void,   glUniform2f, (GLint, GLfloat, GLfloat)) \
    X(void,   glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void,   glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void,   glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void,   glEnableVertexAttribArray, (GLuint)) \
    X(void,   glDisableVertexAttribArray, (GLuint)) \
    X(void,   glGenFramebuffers, (GLsizei, GLuint*)) \
    X(void,   glBindFramebuffer, (GLenum, GLuint)) \
    X(void,   glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void,   glGenerateMipmap, (GLenum)) \
    X(void,   glActiveTexture, (GLenum)) \
    X(void,   glGenRenderbuffers, (GLsizei, GLuint*)) \
    X(void,   glBindRenderbuffer, (GLenum, GLuint)) \
    X(void,   glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void,   glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))

#define DECLARE_GL(ret, name, args) typedef ret (APIENTRY* PFN_##name) args; static PFN_##name name;
GL_FUNCTIONS(DECLARE_GL)

typedef BOOL (APIENTRY* PFN_wglSwapIntervalEXT)(int);
static PFN_wglSwapIntervalEXT wglSwapIntervalEXT;

static bool LoadGL()
{
    bool ok = true;
#define LOAD_GL(ret, name, args) \
    name = (PFN_##name)(void*)wglGetProcAddress(#name); \
    if (!name) ok = false;
    GL_FUNCTIONS(LOAD_GL)
    wglSwapIntervalEXT = (PFN_wglSwapIntervalEXT)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    return ok;
}

// ---------------------------------------------------------------------------
// Shaders

// Star streaks, positioned in pixels (y down). Each streak is a quad around
// the segment from the star's position a moment ago (the tail) to where it is
// now (the head); vLocal is the pixel position along and across the segment.
static const char* kStreakVS = R"(#version 130
in vec2 aPos;
in vec2 aLocal;
in vec2 aSeg;
in vec3 aColor;
uniform vec2 uRes;
out vec2 vLocal;
out vec2 vSeg;
out vec3 vColor;
void main() {
    vLocal = aLocal;
    vSeg = aSeg;
    vColor = aColor;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";

static const char* kStreakFS = R"(#version 130
in vec2 vLocal;
in vec2 vSeg;      // length of the segment and width of the streak, px
in vec3 vColor;
uniform float uWarp;
void main() {
    float len = vSeg.x, w = vSeg.y;
    float a = vLocal.x;
    float da = a < 0.0 ? -a : max(a - len, 0.0);
    float d2 = (da * da + vLocal.y * vLocal.y) / (w * w);
    float t = len > 0.0 ? clamp(a / len, 0.0, 1.0) : 1.0;
    // Brightest at the head, fading out along the tail.
    float ramp = mix(0.08, 1.0, t * t);
    // Light from ahead is blue-shifted, light from the tail end red-shifted.
    vec3 shift = mix(vec3(1.3, 0.75, 0.55), vec3(0.7, 0.9, 1.35), t);
    vec3 col = vColor * mix(vec3(1.0), shift, uWarp * 0.7);
    gl_FragColor = vec4(col * exp(-d2) * ramp, 0.0);
}
)";

static const char* kFullscreenVS = R"(#version 130
in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";

// Faint nebulae that the ship flies through. Sheets of cloud come out of the
// distance, swell and stream past the edges of the screen as the ship nears
// them, fading in far off and out as they go by; four are in view at once,
// each an octave in scale ahead of the one in front, and they are smeared
// outwards at speed just as the stars are.
static const char* kNebulaFS = R"(#version 130
uniform vec2 uRes;
uniform vec2 uCenter;   // vanishing point, in the same pixels as gl_FragCoord
uniform float uZoom;    // grows as the ship travels, an octave per sheet passed
uniform float uSmear;   // how far the clouds move while a streak is drawn, in octaves
uniform float uRoll;
uniform vec2 uPan;      // how far the ship has turned, in screen heights

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) {
        s += a * noise(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p;
        a *= 0.5;
    }
    return s;
}

vec3 clouds(vec2 q) {
    float n = fbm(q * 1.4);
    float cloud = pow(smoothstep(0.38, 0.9, n), 1.5);
    float hue = fbm(q * 0.7 + 9.1);
    vec3 col = mix(vec3(0.16, 0.05, 0.22), vec3(0.03, 0.10, 0.20), smoothstep(0.35, 0.65, hue));
    col = mix(col, vec3(0.22, 0.07, 0.10), smoothstep(0.6, 0.8, fbm(q * 0.5 - 4.0)) * 0.7);
    float lanes = smoothstep(0.25, 0.65, fbm(q * 3.1 + 3.7));
    return col * cloud * lanes;
}

void main() {
    vec2 p = (gl_FragCoord.xy - uCenter) / uRes.y;
    float c = cos(uRoll), s = sin(uRoll);
    p = mat2(c, -s, s, c) * p;
    vec3 col = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 4; i++) {
        float z = uZoom + float(i) * 0.25;
        float k = fract(z);
        float w = smoothstep(0.0, 0.3, k) * (1.0 - smoothstep(0.65, 1.0, k));
        vec2 seed = vec2(floor(z) * 17.31 + float(i) * 3.1, floor(z) * 7.77 - float(i) * 5.3);
        vec3 c = clouds(p * 2.5 * exp2(-k) + uPan * 1.8 + seed);
        c += clouds(p * 2.5 * exp2(-(k - uSmear)) + uPan * 1.8 + seed);
        col += c * 0.5 * w;
        wsum += w;
    }
    col /= max(wsum, 1e-3);
    gl_FragColor = vec4(vec3(0.004, 0.005, 0.011) + col * 1.8, 1.0);
}
)";

static const char* kDownFS = R"(#version 130
uniform sampler2D uTex;
uniform vec2 uRes;
void main() { gl_FragColor = vec4(textureLod(uTex, gl_FragCoord.xy / uRes, 2.0).rgb, 1.0); }
)";

// 13-tap separable gaussian using linear sampling.
static const char* kBlurFS = R"(#version 130
uniform sampler2D uTex;
uniform vec2 uRes;
uniform vec2 uDir;
void main() {
    vec2 uv = gl_FragCoord.xy / uRes;
    vec3 c = texture(uTex, uv).rgb * 0.1964825501511404;
    c += (texture(uTex, uv + uDir * 1.411764705882353).rgb + texture(uTex, uv - uDir * 1.411764705882353).rgb) * 0.2969069646728344;
    c += (texture(uTex, uv + uDir * 3.294117647058823).rgb + texture(uTex, uv - uDir * 3.294117647058823).rgb) * 0.0944703978504473;
    c += (texture(uTex, uv + uDir * 5.176470588235294).rgb + texture(uTex, uv - uDir * 5.176470588235294).rgb) * 0.0103813624011481;
    gl_FragColor = vec4(c, 1.0);
}
)";

// Final image: nebulae, stars and their glow, a haze of light around the
// vanishing point while at warp, and the flash of a jump.
static const char* kCompositeFS = R"(#version 130
uniform sampler2D uNebula;
uniform sampler2D uStars;
uniform sampler2D uBloom;
uniform vec2 uRes;
uniform vec2 uCenter;
uniform float uWarp;
uniform float uFlash;
void main() {
    vec2 uv = gl_FragCoord.xy / uRes;
    // A planet in the stars layer hides the nebulae behind it.
    vec4 stars = texture(uStars, uv);
    vec3 col = texture(uNebula, uv).rgb * (1.0 - clamp(stars.a, 0.0, 1.0)) + stars.rgb;
    col += texture(uBloom, uv).rgb * 0.9;

    vec2 d = (gl_FragCoord.xy - uCenter) / uRes.y;
    float r2 = dot(d, d);
    col += vec3(0.25, 0.35, 0.6) * exp(-r2 * 40.0) * uWarp * 0.12;
    col += vec3(0.75, 0.85, 1.0) * (exp(-r2 * 30.0) * 2.0 + exp(-r2 * 5.0) * 0.4) * uFlash;

    col = 1.0 - exp(-col * 1.8);  // tone map
    vec2 v = uv - 0.5;
    col *= 1.0 - 0.45 * dot(v, v);
    gl_FragColor = vec4(col, 1.0);
}
)";

// A star seen up close, drawn over the whole screen. A red giant
// is a huge mottled disc, a yellow star a blazing one, a brown dwarf a dim
// banded ball, and a neutron star a tiny fierce point sweeping two beams of
// light round the sky.
static const char* kSunFS = R"(#version 130
uniform vec2 uCenter;   // pixels, y up
uniform float uRadius;  // pixels
uniform vec3 uColor;
uniform float uGlow;
uniform int uKind;      // 0 red giant, 1 yellow star, 2 brown dwarf, 3 neutron star
uniform float uTime;
uniform vec3 uBeam;     // neutron star's beam: direction on screen (xy), and how much it points at us (z)

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main() {
    vec2 d = gl_FragCoord.xy - uCenter;
    float r = length(d) / uRadius;
    float mu = sqrt(max(1.0 - r * r, 0.0));
    float beyond = max(r - 1.0, 0.0);
    float inside = smoothstep(1.0 + 1.5 / uRadius, 1.0 - 1.5 / uRadius, r);
    vec2 p = d / uRadius;  // on the disc, -1..1
    vec3 col;
    if (uKind == 0) {
        float cells = noise(p * 6.0 + uTime * 0.05) * 0.6 + noise(p * 15.0 - uTime * 0.08) * 0.4;
        vec3 disc = uColor * (0.25 + 0.75 * pow(mu, 0.8)) * (0.75 + 0.5 * cells) * 1.3;
        vec3 corona = uColor * (exp(-beyond * 3.0) * 0.6 + exp(-beyond * 0.5) * 0.15);
        col = mix(corona, disc, inside);
    } else if (uKind == 1) {
        vec3 disc = mix(uColor, vec3(1.0), 0.35) * (0.3 + 0.7 * pow(mu, 0.6)) * 1.6;
        vec3 corona = uColor * (exp(-beyond * 5.0) * 0.8 + exp(-beyond * 0.8) * 0.2 + 0.15 / (1.0 + r * r * 0.3));
        col = mix(corona, disc, inside);
    } else if (uKind == 2) {
        float band = sin(p.y / max(mu, 0.2) * 5.0 + noise(p * 4.0 + uTime * 0.03) * 2.0);
        vec3 disc = uColor * (0.35 + 0.65 * mu) * (0.8 + 0.25 * band) * 0.6;
        vec3 corona = uColor * exp(-beyond * 6.0) * 0.25;
        col = mix(corona, disc, inside);
    } else {
        float rp = length(d);
        vec3 core = vec3(1.0) * exp(-rp * rp / (uRadius * uRadius * 4.0)) * 6.0;
        vec3 halo = uColor * (exp(-rp / (uRadius * 6.0)) * 1.2 + 25.0 / (25.0 + rp * 0.5)) * 0.25;
        // Two beams, opposite ways along uBeam.xy, flaring when they swing towards us.
        vec2 dir = normalize(uBeam.xy + vec2(1e-4));
        float along = dot(d, dir), across = abs(d.x * dir.y - d.y * dir.x);
        float width = uRadius * 2.0 + abs(along) * 0.02;
        float beam = exp(-across * across / (width * width)) * exp(-abs(along) / (uRadius * 400.0)) * length(uBeam.xy);
        float flare = pow(abs(uBeam.z), 30.0);
        col = core + halo * (1.0 + flare * 4.0) + uColor * beam * 1.2;
    }
    // The disc is solid and hides what is behind it; the glow round it adds
    // light (premultiplied: alpha is how much of the pixel the disc covers).
    gl_FragColor = vec4(col * uGlow, inside * uGlow);
}
)";

// A planet: a sphere traced per pixel, lit by its star, with a glowing
// atmosphere and perhaps rings. Output is premultiplied, alpha being how much
// of the pixel the planet and rings cover, so they hide what is behind them.
static const char* kPlanetFS = R"(#version 130
uniform vec2 uCenter;     // where the line of sight meets the screen, pixels, y up
uniform float uFocal;     // pixels
uniform float uRoll;
uniform vec3 uPos;        // planet centre, seen from the ship (before roll)
uniform float uRadius;
uniform vec3 uAxis0, uAxis1, uAxis2;  // the planet's own axes; it spins about uAxis1
uniform float uSpin;
uniform vec3 uSunDir;     // from the planet towards its star
uniform vec3 uLight;      // colour and strength of the star's light
uniform int uKind;        // see kPlanetKinds
uniform vec3 uColA, uColB, uColC;
uniform vec3 uAtm;        // atmosphere colour (black for none)
uniform vec3 uSeed;
uniform int uRings;
uniform vec3 uRingCol;
uniform float uFade;
uniform int uOctaves;     // finer detail when seen up close
uniform int uCivilized;   // a habitable world with people on it
uniform float uStraighten; // 1 to draw it round wherever it is on the screen (see main)

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x),
                   mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
                   mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 10; i++) {
        if (i >= uOctaves) break;
        s += a * noise(p);
        p = p * 2.03 + vec3(1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return s;
}

// Small-scale relief, for fine detail and for the play of light over hills.
float relief(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) {
        s += a * noise(p);
        p = p * 2.17 + vec3(4.1, 1.3, 7.7);
        a *= 0.5;
    }
    return s / 0.875;
}

// How rugged each kind of planet is: rock and ice are craggy, seas and
// cloud tops smooth.
float ruggedness() {
    if (uKind == 1) return 1.0;   // desert
    if (uKind == 3) return 0.6;   // ice
    if (uKind == 4) return 0.8;   // lava
    if (uKind == 8) return 1.2;   // cratered rock
    if (uKind == 0 || uKind == 7) return 0.7;  // temperate, jungle
    if (uKind == 5) return 0.3;   // ocean
    return 0.0;                   // gas giant, toxic clouds, ice giant
}

// Distance to the nearest of a scatter of points: craters.
float cells(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    float best = 9.0;
    for (int z = -1; z <= 1; z++)
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) {
                vec3 o = vec3(x, y, z);
                vec3 h = vec3(hash(i + o), hash(i + o + 17.1), hash(i + o + 31.7));
                best = min(best, length(o + h - f));
            }
    return best;
}

// Light from towns and roads, shown only on the night side (set by surface()).
vec3 gNight = vec3(0.0);

// Signs of people on a habitable world's land: patchworks of planted fields,
// grey sprawl of towns and cities, roads winding between them; and at night,
// glittering city lights and threads of lit roads. Only in scattered settled
// regions, away from the poles.
void civilization(vec3 q, vec3 p, float land, inout vec3 c) {
    float settled = smoothstep(0.42, 0.58, fbm(p * 1.4 + 31.0)) * land * (1.0 - smoothstep(0.55, 0.72, abs(q.y)));
    if (settled <= 0.0) return;
    // Towns and cities: scattered clusters, densest at their hearts and
    // thinning out into suburbs, with open country between.
    float town = pow(smoothstep(0.6, 0.92, noise(p * 12.0 + 71.0)), 2.0);
    float city = max(town, 0.6 * pow(smoothstep(0.7, 0.95, noise(p * 31.0 + 13.0)), 2.0)) * settled;
    // Fields: a patchwork of plots, each sown with its own crop.
    vec3 cell = floor(q * 300.0);
    float crop = hash(cell + uSeed);
    vec3 field = crop < 0.33 ? vec3(0.3, 0.45, 0.14) : crop < 0.66 ? vec3(0.62, 0.55, 0.24) : vec3(0.42, 0.32, 0.2);
    field *= 0.85 + 0.3 * hash(cell * 2.3 + 1.0);
    if (uKind == 7) field = mix(field.gbr, uColB, 0.35);  // stranger crops on an alien world
    vec3 fe = fract(q * 300.0);
    float border = min(min(min(fe.x, 1.0 - fe.x), min(fe.y, 1.0 - fe.y)), min(fe.z, 1.0 - fe.z));
    field *= mix(0.72, 1.0, smoothstep(0.0, 0.06, border));  // hedgerows between plots
    float farm = settled * (1.0 - smoothstep(0.05, 0.3, city)) * step(0.2, hash(cell * 1.7 + 5.0));
    c = mix(c, field, farm * 0.8);
    c = mix(c, vec3(0.5, 0.5, 0.52), smoothstep(0.05, 0.6, city) * 0.8);
    // Roads: a well-warped net, wandering between the settlements.
    vec3 warp = vec3(noise(p * 2.5), noise(p * 2.5 + 7.0), noise(p * 2.5 + 13.0)) - 0.5;
    warp += (vec3(noise(p * 9.0), noise(p * 9.0 + 3.0), noise(p * 9.0 + 5.0)) - 0.5) * 0.3;
    vec3 rf = abs(fract(q * 28.0 + warp * 3.0) - 0.5);
    float road = (1.0 - smoothstep(0.0, 0.025, 0.5 - max(rf.x, max(rf.y, rf.z)))) * smoothstep(0.15, 0.6, settled);
    c = mix(c, vec3(0.33, 0.32, 0.31), road * 0.75);
    // At night: many small lights, thickest at the cities' hearts and
    // scattered thinly through their suburbs; roads glow faintly.
    vec3 spot = floor(q * 1100.0);
    float lit = step(1.0 - city * 0.95, hash(spot + uSeed.yzx)) * (0.5 + 0.5 * hash(spot * 1.3 + 2.0));
    gNight = vec3(1.0, 0.72, 0.38) * (lit * (0.8 + city * 1.6) + road * settled * 0.12);
}

// Surface colour at a point q on the unit sphere (the planet's own frame).
// Light the planet gives off itself goes in glow.
vec3 surface(vec3 q, out vec3 glow) {
    vec3 p = q + uSeed;
    glow = vec3(0.0);
    gNight = vec3(0.0);
    if (uKind == 0) {  // temperate: seas, continents, ice caps and clouds
        float h = fbm(p * 2.2);
        vec3 land = mix(uColB, uColC, smoothstep(0.58, 0.72, h));
        vec3 sea = mix(uColA * 0.6, uColA * 1.5, smoothstep(0.3, 0.5, h));
        vec3 c = h > 0.52 ? land : sea;
        float ice = smoothstep(0.75, 0.85, abs(q.y) + (h - 0.5) * 0.3);
        if (uCivilized != 0) civilization(q, p, step(0.52, h) * (1.0 - ice), c);
        c = mix(c, vec3(0.9), ice);
        float cloud = smoothstep(0.5, 0.75, fbm(p * 3.5 + vec3(uSpin * 0.3, 0.0, 0.0)));
        gNight *= 1.0 - cloud * 0.7;
        return mix(c, vec3(0.95), cloud * 0.85);
    }
    if (uKind == 1) {  // desert, with dune fields and frosted poles
        float h = fbm(p * 2.5);
        vec3 c = mix(uColA, uColB, h) * (0.75 + 0.5 * fbm(p * 9.0));
        c *= 0.92 + 0.08 * sin(dot(q, vec3(40.0, 13.0, 27.0)) + fbm(p * 4.0) * 12.0);
        return mix(c, uColC, smoothstep(0.88, 0.95, abs(q.y)));
    }
    if (uKind == 2) {  // gas giant: bands, swirls and a great storm
        float band = q.y * 9.0 + (fbm(p * vec3(2.0, 6.0, 2.0)) - 0.5) * 3.0;
        vec3 c = mix(uColA, uColB, 0.5 + 0.5 * sin(band));
        c = mix(c, uColC, smoothstep(0.6, 1.0, sin(band * 0.37 + 1.0)) * 0.6);
        vec3 spot = normalize(vec3(0.8, -0.35, 0.5));
        float e = length((q - spot) * vec3(1.0, 2.4, 1.0)) + (fbm(p * 8.0) - 0.5) * 0.1;
        return mix(c, uColC * 1.15, smoothstep(0.24, 0.12, e) * 0.85);
    }
    if (uKind == 3) {  // ice world, crazed with cracks
        float h = fbm(p * 3.0);
        vec3 c = mix(uColA, uColB, h);
        float crack = 1.0 - smoothstep(0.0, 0.015, abs(fbm(p * 5.0) - 0.5));
        return mix(c, uColC, crack * 0.7);
    }
    if (uKind == 4) {  // lava world: black crust split by glowing rivers of rock
        vec3 c = mix(uColA, uColB, fbm(p * 4.0));
        float crack = 1.0 - smoothstep(0.0, 0.035, abs(fbm(p * 3.0) - 0.5));
        float pools = smoothstep(0.66, 0.72, fbm(p * 2.0 + 5.0));
        glow = uColC * (crack + pools) * (0.7 + 0.3 * fbm(p * 12.0)) * 1.6;
        return c * (1.0 - max(crack, pools));
    }
    if (uKind == 5) {  // ocean world under swirling storms
        float h = fbm(p * 3.0);
        vec3 c = mix(uColA, uColB, smoothstep(0.4, 0.7, h));
        c = mix(c, uColC, smoothstep(0.72, 0.75, h));
        if (uCivilized != 0) civilization(q, p, smoothstep(0.73, 0.75, h), c);
        vec3 w = p * 2.5 + vec3(fbm(p * 2.0), fbm(p * 2.0 + 3.3), 0.0) * 2.5;
        float cloud = smoothstep(0.52, 0.75, fbm(w + vec3(uSpin * 0.4, 0.0, 0.0)));
        gNight *= 1.0 - cloud * 0.7;
        return mix(c, vec3(0.95), cloud * 0.9);
    }
    if (uKind == 6) {  // shrouded in thick, poisonous cloud
        float w = fbm(p * 2.0 + vec3(0.0, q.y * 3.0, 0.0));
        vec3 c = mix(uColA, uColB, fbm(p * 3.0 + w * 3.0));
        return mix(c, uColC, smoothstep(0.58, 0.8, fbm(p * 5.0 + w)));
    }
    if (uKind == 7) {  // alien jungle world: strange-coloured seas and forests
        float h = fbm(p * 2.4);
        vec3 land = mix(uColB, uColB * 0.45, fbm(p * 9.0));
        land = mix(land, uColC, smoothstep(0.66, 0.76, h));
        vec3 sea = mix(uColA * 0.7, uColA * 1.4, smoothstep(0.3, 0.5, h));
        vec3 c = h > 0.5 ? land : sea;
        if (uCivilized != 0) civilization(q, p, step(0.5, h), c);
        float cloud = smoothstep(0.58, 0.8, fbm(p * 4.0 + vec3(uSpin * 0.3, 0.0, 0.0)));
        gNight *= 1.0 - cloud * 0.5;
        return mix(c, vec3(0.9, 0.9, 0.95), cloud * 0.6);
    }
    if (uKind == 8) {  // airless, cratered rock
        vec3 c = mix(uColA, uColB, fbm(p * 3.0));
        for (int i = 0; i < 2; i++) {
            float sc = i == 0 ? 4.0 : 11.0;
            float f = cells(p * sc);
            float size = 0.3 + 0.15 * hash(floor(p * sc));
            c *= mix(1.0, 0.7, smoothstep(size, size - 0.05, f));          // crater floor
            c += uColB * 0.35 * smoothstep(0.04, 0.0, abs(f - size));      // bright rim
        }
        return c * (0.85 + 0.3 * fbm(p * 20.0));
    }
    // 9: ice giant, smooth and softly banded
    float band = q.y * 6.0 + (fbm(p * vec3(1.0, 4.0, 1.0)) - 0.5) * 1.5;
    vec3 c = mix(uColA, uColB, 0.5 + 0.5 * sin(band));
    vec3 spot = normalize(vec3(-0.6, -0.4, 0.7));
    return mix(c, uColC, smoothstep(0.2, 0.1, length((q - spot) * vec3(1.0, 2.0, 1.0))) * 0.8);
}

// How much of the star's light the rings let through to point x.
float ringShadow(vec3 x) {
    if (uRings == 0) return 1.0;
    float dn = dot(uSunDir, uAxis1);
    if (abs(dn) < 1e-4) return 1.0;
    float t = dot(uPos - x, uAxis1) / dn;
    if (t <= 0.0) return 1.0;
    float rr = length(x + uSunDir * t - uPos) / uRadius;
    float dens = smoothstep(1.35, 1.45, rr) * (1.0 - smoothstep(2.1, 2.25, rr)) * (0.45 + 0.55 * noise(vec3(rr * 30.0, uSeed.x, 0.0)));
    return 1.0 - dens * 0.75;
}

void main() {
    vec2 s = (gl_FragCoord.xy - uCenter) / uFocal;
    float c = cos(uRoll), sn = sin(uRoll);
    vec2 v = vec2(s.x * c + s.y * sn, -s.x * sn + s.y * c);
    vec3 d = normalize(vec3(v, 1.0));
    // A wide view stretches a sphere far off to the side into an oval. Seen
    // as if looking straight at it, at the scale the view has there, it
    // stays round.
    if (uStraighten > 0.0 && uPos.z > 0.0) {
        vec3 f = normalize(uPos);
        vec2 off = v - uPos.xy / uPos.z;
        vec3 across = normalize(vec3(f.z, 0.0, -f.x));
        vec3 up = cross(f, across);
        vec3 straight = normalize(f + (across * off.x + up * off.y) * f.z);
        d = normalize(mix(d, straight, uStraighten * smoothstep(0.1, 0.35, f.z)));
    }

    float b = dot(d, uPos);
    float h = sqrt(max(dot(uPos, uPos) - b * b, 0.0));  // how close the ray passes to the centre
    float px = max(b, 0.1) / uFocal * 1.5;               // a pixel's width at the planet
    float cover = b > 0.0 ? smoothstep(uRadius + px, uRadius - px, h) : 0.0;
    float back = pow(max(dot(d, uSunDir), 0.0), 6.0);   // looking towards the star past the planet

    // Atmosphere: lit where the planet is lit, blazing round the rim when the
    // star is behind the planet.
    vec3 glow = vec3(0.0);
    // (Measured where the ray passes closest to the planet: for a ray
    // heading away from it, that is where it starts, at the camera, so the
    // glow fades smoothly rather than stopping dead.)
    {
        float bc = max(b, 0.0);
        vec3 near = d * bc - uPos;
        vec3 limbN = normalize(near);
        float limbLit = clamp(dot(limbN, uSunDir) * 1.5 + 0.3, 0.0, 1.0);
        float haze = exp(-max(length(near) - uRadius, 0.0) / (uRadius * 0.05));
        glow = uAtm * uLight * haze * (limbLit * 0.6 + back * 2.5);
    }

    vec3 pc = vec3(0.0);
    float tPlanet = 1e9;
    if (b > 0.0 && h < uRadius + px) {
        tPlanet = b - sqrt(max(uRadius * uRadius - h * h, 0.0));
        vec3 x = d * tPlanet;
        vec3 n = normalize(x - uPos);
        vec3 q = vec3(dot(n, uAxis0), dot(n, uAxis1), dot(n, uAxis2));
        float cs = cos(uSpin), ss = sin(uSpin);
        q = vec3(q.x * cs - q.z * ss, q.y, q.x * ss + q.z * cs);

        // Hills and hollows: tilt the surface by the slope of the fine
        // relief, so the light picks them out.
        float rugged = ruggedness();
        if (rugged > 0.0) {
            const float e = 0.0015, scale = 30.0;
            vec3 base = (q + uSeed) * scale;
            float h0 = relief(base);
            vec3 g = vec3(relief(base + vec3(e * scale, 0.0, 0.0)), relief(base + vec3(0.0, e * scale, 0.0)),
                          relief(base + vec3(0.0, 0.0, e * scale))) - h0;
            g /= e;
            g -= q * dot(g, q);
            vec3 gp = vec3(g.x * cs + g.z * ss, g.y, -g.x * ss + g.z * cs);  // undo the spin
            n = normalize(n - (uAxis0 * gp.x + uAxis1 * gp.y + uAxis2 * gp.z) * rugged * 0.02);
        }
        float ndl = dot(n, uSunDir);
        float lit = smoothstep(-0.08, 0.25, ndl) * (0.25 + 0.75 * max(ndl, 0.0)) * ringShadow(x);
        vec3 own;
        vec3 surf = surface(q, own);
        // Fine mottling of the colour; on banded and cloudy worlds, streaks
        // drawn out along the bands.
        bool streaky = uKind == 2 || uKind == 6 || uKind == 9;
        vec3 fq = streaky ? vec3(q.x * 10.0, q.y * 90.0, q.z * 10.0) : q * 35.0;
        surf *= (0.86 + 0.28 * relief(fq + uSeed)) * (0.9 + 0.2 * relief(fq * 3.7 + uSeed * 1.3));
        pc = surf * uLight * lit * 0.55 + surf * 0.008 + own + gNight * (1.0 - smoothstep(-0.05, 0.12, ndl));
        float rim = pow(clamp(1.0 - dot(n, -d), 0.0, 1.0), 3.0);
        pc += uAtm * uLight * rim * (0.35 * smoothstep(-0.2, 0.4, ndl) + back * 1.5);
    }

    // Rings, in the planet's equatorial plane, shadowed by the planet.
    vec3 rc = vec3(0.0);
    float ra = 0.0, tRing = 1e9;
    if (uRings != 0) {
        float dn = dot(d, uAxis1);
        if (abs(dn) > 1e-5) {
            float t = dot(uPos, uAxis1) / dn;
            if (t > 0.0) {
                vec3 x = d * t;
                float rr = length(x - uPos) / uRadius;
                float dens = smoothstep(1.35, 1.45, rr) * (1.0 - smoothstep(2.1, 2.25, rr));
                dens *= 0.45 + 0.55 * noise(vec3(rr * 30.0, uSeed.x, 0.0));
                dens *= 1.0 - 0.6 * smoothstep(1.72, 1.74, rr) * (1.0 - smoothstep(1.78, 1.8, rr));  // a gap
                vec3 toC = uPos - x;
                float bb = dot(toC, uSunDir);
                float hh = length(toC - uSunDir * bb);
                float shade = bb > 0.0 ? smoothstep(uRadius * 0.97, uRadius * 1.03, hh) : 1.0;
                float lightAmt = 0.15 + 0.85 * abs(dot(uSunDir, uAxis1));
                rc = uRingCol * (0.7 + 0.3 * noise(vec3(rr * 80.0, 3.0, uSeed.y))) * uLight * shade * lightAmt * 0.6;
                ra = dens * 0.85;
                tRing = t;
            }
        }
    }

    vec3 col;
    float a;
    float pa = cover;
    if (tRing < tPlanet) {  // rings in front
        col = rc * ra + pc * pa * (1.0 - ra);
        a = ra + pa * (1.0 - ra);
    } else {
        col = pc * pa + rc * ra * (1.0 - pa);
        a = pa + ra * (1.0 - pa);
    }
    gl_FragColor = vec4((col + glow * (1.0 - a)) * uFade, a * uFade);
}
)";

// ---------------------------------------------------------------------------
// Tunables. Distances are in arbitrary space units; pixel values are for a
// 1080-pixel-tall screen and scale with it.

static const int   kStars       = 4000;     // stars in flight (1080p)
static const float kFar         = 600.0f;   // how far ahead stars appear
static const float kNear        = 0.3f;     // stars closer than this have passed the ship
static const float kFocal       = 0.6f;     // focal length, in screen heights
static const float kMaxSpeed    = 46.7f;  // space units per second at full warp
static const float kCoastSpeed  = 2.5f;     // drifting between jumps
static const float kShutter     = 0.12f;    // seconds of motion each streak shows
static const float kNebulaFlow  = 0.012f;   // nebula sheets passed per unit travelled
static const float kGravity     = 3500.0f;  // pull of a star on the ship, per unit of brightness cubed
static const float kReach       = 20.0f;    // how close a star must pass to pull the ship round
static const float kSwing       = 0.8f;     // seconds over which a star's pull swings the ship round
static const float kMaxTurn     = 0.3f;     // fastest the ship turns, radians per second
static const float kCapture     = 5.0f;     // a star passing closer than this captures the ship
static const float kCaptureRest = 15.0f;    // seconds after an orbit before the ship can be captured again
static const float kShipAt[3]  = {0.0f, -0.48f, 1.6f};  // where the ship flies, from the camera behind it
static const float kWideTime    = 8.0f;    // seconds for the camera to rise above the ship in orbit, or come back down
static const float kLowOrbit    = 1.05f;   // the ship's orbit, in planet radii from its centre
static const float kOverhead    = 0.13f;   // how high above the ship the camera rides, in planet radii
static const float kOrbitScale  = 2.5f;    // over a planet bigger than this the orbit and camera are no higher than over this one
static const float kBehind     = 1.2f;    // how far the camera trails the ship in orbit, for each unit it is above it
static const float kShipScale = 0.62f;    // the ship's model units to space units (see ship.h)
static const float kShipTilt = 0.3f;      // flying free, tipped back towards the camera, so it looks down on it
static const float kShipBow = 0.56f * kShipScale;  // the ship's bow, ahead of its centre
static const float kHubPort = 1.15f;      // the station's docking collar, ahead of its centre along its axis
static const float kRejoinTime = 6.0f;    // seconds for the camera to come in behind the ship after undocking
static const float kDockedTime  = 30.0f;   // seconds docked at the station before setting out
static const float kBackingTime = 10.0f;   // seconds backing away from it
static const float kEarthRadius = 150.0f;  // the Earth the station keeps company with
static const float kMoonDistance = 700.0f; // from the Earth's centre to its Moon's
static const float kLagrange   = 0.92f;    // the station rides between the Earth and Moon, this far from Earth to Moon
static const float kDockedFrom = 380.0f;   // the camera's distance from the station when it starts
static const float kDockedTo   = 7.0f;     // and when it has closed in, the wheel filling the screen
static const float kDockedWide[3] = {1.0f, 0.3f, 0.15f};   // the way the camera looks from when it starts, off to the side
static const float kDockedView[3] = {0.35f, 0.25f, 1.0f};  // and when it has closed in, in the station's axes
static const float kArriveAt    = 2.2f;    // how far above the planet's surface the ship stops before descending
static const float kOrbitTime   = 30.0f;    // seconds for one lap of the planet (longer round a huge one)
static const int   kOrbits      = 2;        // laps of an inhabited world before the ship flies on (one of an empty one)
static const float kBuoyDelay   = 10.0f;    // seconds into an orbit, once the ship has settled, before a wary world's buoys arrive
static const float kElevatorHeight = 0.5f;  // a space elevator's station, above its world's surface, in planet radii
static const float kElevatorScale  = 0.04f; // the station's model units to space units
static const float kElevatorShrink = 0.12f; // visiting it, the ship is drawn this much smaller and the camera that much
                                            // closer, which looks the same, so it is in scale with the station
static const float kElevatorPort   = 2.3f;  // the station's docking pad, out from its centre, model units
static const float kElevatorPadTop = 0.12f; // the pad's top, above the station's middle, model units
static const float kElevatorHold   = 0.35f; // where the ship waits before creeping in to dock, from its berth
static const float kElevatorSettle = 0.05f; // how far above the pad it creeps in before settling onto it
static const float kSaucerBottom[2] = {-0.089f, 0.2f};  // the bottom of the ship's saucer, model units (up, forward)
static const float kMooredTime     = 16.0f; // seconds docked at the elevator

// The stars that can capture the ship.
struct StarKind {
    const char* name;
    float radius;          // seen up close
    float color[3];
    float light[3];        // colour and strength of the light it sheds on its planet
    float planetDistance;  // how far from it the planet circles
    float inhabited;       // how often its planet is an inhabited world
};
static const StarKind kStarKinds[] = {
    {"red giant",     9.0f,  {1.00f, 0.42f, 0.22f}, {1.00f, 0.55f, 0.35f}, 45.0f, 0.0f},
    {"yellow star",   1.5f,  {1.00f, 0.90f, 0.70f}, {1.00f, 0.95f, 0.85f}, 25.0f, 0.5f},
    {"brown dwarf",   1.0f,  {0.70f, 0.30f, 0.20f}, {0.40f, 0.20f, 0.15f}, 15.0f, 0.7f},
    {"neutron star",  0.12f, {0.70f, 0.85f, 1.00f}, {0.70f, 0.80f, 1.00f}, 25.0f, 0.0f},
    {"red giant and neutron star", 9.0f, {1.00f, 0.42f, 0.22f}, {1.00f, 0.55f, 0.35f}, 60.0f, 0.0f},
    {"binary brown dwarf", 1.0f, {0.70f, 0.30f, 0.20f}, {0.55f, 0.28f, 0.2f}, 22.0f, 0.7f},
};
static const int kStarKindCount = sizeof(kStarKinds) / sizeof(kStarKinds[0]);
static const int kNeutronStar = 3;
static const int kRedGiant = 0;
static const int kYellowStar = 1;
static const int kBrownDwarf = 2;
static const int kDoubleStar = 4;   // a red giant, and a neutron star pulling gas off it
static const int kBinaryDwarf = 5;  // two brown dwarfs circling each other
static const float kPulsarChance = 0.05f;   // how often a star that captures the ship is a pulsar
static const float kDoubleChance = 0.025f;  // and how often a red giant with a neutron star
static const float kDoubleGap = 24.0f;  // from the giant's centre to the neutron star
static const float kBinaryGap = 5.0f;   // between the two brown dwarfs of a binary

// The planets the ship can orbit. Colours are sea/base, land/bands, detail
// and atmosphere; each is varied a little every time. Some kinds have more
// than one palette to pick from.
struct PlanetKind {
    const char* name;
    float radius;
    float ringChance;
    int palettes;
    float colors[3][4][3];
};
static const PlanetKind kPlanetKinds[] = {
    {"temperate", 1.25f, 0.08f, 1, {
        {{0.02f, 0.08f, 0.25f}, {0.18f, 0.32f, 0.12f}, {0.50f, 0.42f, 0.28f}, {0.35f, 0.60f, 1.00f}}}},
    {"desert", 1.2f, 0.08f, 2, {
        {{0.45f, 0.18f, 0.08f}, {0.85f, 0.55f, 0.30f}, {0.90f, 0.85f, 0.80f}, {1.00f, 0.65f, 0.40f}},
        {{0.55f, 0.45f, 0.30f}, {0.90f, 0.80f, 0.55f}, {0.95f, 0.95f, 0.90f}, {0.95f, 0.80f, 0.55f}}}},
    {"gas giant", 1.9f, 0.6f, 3, {
        {{0.50f, 0.33f, 0.20f}, {0.95f, 0.88f, 0.75f}, {0.70f, 0.30f, 0.15f}, {0.90f, 0.80f, 0.60f}},
        {{0.75f, 0.62f, 0.40f}, {0.95f, 0.90f, 0.70f}, {0.80f, 0.70f, 0.50f}, {0.95f, 0.85f, 0.60f}},
        {{0.35f, 0.20f, 0.50f}, {0.80f, 0.60f, 0.85f}, {0.90f, 0.50f, 0.40f}, {0.80f, 0.60f, 1.00f}}}},
    {"ice", 1.1f, 0.15f, 1, {
        {{0.55f, 0.70f, 0.85f}, {0.92f, 0.96f, 1.00f}, {0.30f, 0.45f, 0.60f}, {0.60f, 0.85f, 1.00f}}}},
    {"lava", 1.1f, 0.05f, 1, {
        {{0.08f, 0.06f, 0.05f}, {0.20f, 0.12f, 0.08f}, {1.00f, 0.35f, 0.05f}, {1.00f, 0.40f, 0.15f}}}},
    {"ocean", 1.3f, 0.08f, 1, {
        {{0.01f, 0.04f, 0.20f}, {0.05f, 0.25f, 0.45f}, {0.70f, 0.65f, 0.45f}, {0.40f, 0.65f, 1.00f}}}},
    {"toxic clouds", 1.2f, 0.1f, 2, {
        {{0.75f, 0.65f, 0.25f}, {0.45f, 0.50f, 0.15f}, {0.95f, 0.90f, 0.60f}, {0.90f, 0.85f, 0.40f}},
        {{0.30f, 0.55f, 0.25f}, {0.60f, 0.75f, 0.20f}, {0.85f, 0.95f, 0.55f}, {0.60f, 0.95f, 0.40f}}}},
    {"alien jungle", 1.25f, 0.1f, 2, {
        {{0.05f, 0.20f, 0.20f}, {0.35f, 0.08f, 0.30f}, {0.70f, 0.30f, 0.15f}, {0.80f, 0.50f, 1.00f}},
        {{0.25f, 0.06f, 0.12f}, {0.55f, 0.35f, 0.05f}, {0.30f, 0.60f, 0.55f}, {1.00f, 0.55f, 0.50f}}}},
    {"cratered rock", 1.0f, 0.12f, 2, {
        {{0.35f, 0.33f, 0.30f}, {0.60f, 0.58f, 0.55f}, {0.20f, 0.19f, 0.18f}, {0.00f, 0.00f, 0.00f}},
        {{0.40f, 0.28f, 0.22f}, {0.62f, 0.50f, 0.42f}, {0.25f, 0.18f, 0.15f}, {0.00f, 0.00f, 0.00f}}}},
    {"ice giant", 1.7f, 0.5f, 2, {
        {{0.25f, 0.55f, 0.80f}, {0.45f, 0.75f, 0.90f}, {0.15f, 0.30f, 0.60f}, {0.50f, 0.80f, 1.00f}},
        {{0.50f, 0.80f, 0.80f}, {0.70f, 0.92f, 0.90f}, {0.35f, 0.60f, 0.65f}, {0.60f, 0.95f, 0.95f}}}},
};
static const int kPlanetKindCount = sizeof(kPlanetKinds) / sizeof(kPlanetKinds[0]);

// ---------------------------------------------------------------------------
// GL helpers

static void Fail(const char* what, const char* detail = "")
{
    std::string msg = std::string(what) + "\n\n" + detail;
    MessageBoxA(nullptr, msg.c_str(), "Starfield", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        Fail("Shader compile failed", log);
    }
    return s;
}

static GLuint LinkProgram(const char* vs, const char* fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, CompileShader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, CompileShader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aLocal");
    glBindAttribLocation(p, 2, "aSeg");
    glBindAttribLocation(p, 3, "aColor");
    glBindAttribLocation(p, 4, "aNormal");
    glBindAttribLocation(p, 5, "aMat");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        Fail("Shader link failed", log);
    }
    return p;
}

struct Target {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
};

static Target MakeTarget(int w, int h, GLenum format, bool mipmapped)
{
    Target t;
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (mipmapped) glGenerateMipmap(GL_TEXTURE_2D);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Fail("Framebuffer incomplete");
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    return t;
}

static void BindTarget(const Target& t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.w, t.h);
}

static void DrawFullscreen()
{
    static const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void BindTexture(int unit, GLuint tex)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

#include "music.h"

// ---------------------------------------------------------------------------
// Flight: the ship's speed and heading, shared by every monitor.
//
// Everything is seen from the ship: it looks along +z (y up, before the roll
// of the view). It starts at rest, jumps to warp and cruises, its speed slowly
// wandering. A star passing very close captures it: the ship drops out of
// warp and slows.
// The ship flies to a planet circling that star, orbits the planet twice
// while looking at it (and at the star, when both fit in view), then turns
// away and jumps.

struct Star {
    float x, y, z;
    float lum;
    float age;            // seconds since it appeared
    float r, g, b;
};

// The ship turning by a (towards +x) then b (towards +y) swings everything
// around it the other way.
struct Rotation {
    float ca, sa, cb, sb;
    Rotation(float a, float b) : ca(cosf(a)), sa(sinf(a)), cb(cosf(b)), sb(sinf(b)) {}
    void Apply(float& x, float& y, float& z) const
    {
        float x1 = x * ca - z * sa, z1 = x * sa + z * ca;
        float y1 = y * cb - z1 * sb;
        z = y * sb + z1 * cb;
        x = x1;
        y = y1;
    }
    void Undo(float& x, float& y, float& z) const
    {
        float y0 = y * cb + z * sb, z1 = -y * sb + z * cb;
        float x0 = x * ca + z1 * sa;
        z = -x * sa + z1 * ca;
        x = x0;
        y = y0;
    }
};

// The turn that would face a direction: yaw first, then pitch (as in Rotation).
static void Aim(float x, float y, float z, float& yaw, float& pitch)
{
    yaw = atan2f(x, z);
    pitch = atan2f(y, hypotf(x, z));
}

struct Flight {
    enum Phase { Cruise, Coast, Jump, Approach, Orbit, Depart, Docked, Undock, Rejoin, Berth, Moored };
    Phase phase = Docked;
    float phaseTime = 0, phaseLength = kDockedTime;  // starts docked; see StartDocked
    double t = 0;
    float speed = kCoastSpeed;
    float velX = 0, velY = 0, velZ = kCoastSpeed;  // the ship's velocity
    float warp = 0;       // 0 coasting .. 1 at full speed
    float flash = 0;      // light of a jump
    float roll = 0, rollRate = 0;
    float zoom = 0;       // how far the nebulae have swelled
    float headingX = 0, headingY = 0;  // offset of the vanishing point, in screen heights

    // Steering by gravity. Directions are in the stars' frame (before roll).
    float gravX = 0, gravY = 0;          // sideways pull of nearby stars, set before Update
    float turnRateX = 0, turnRateY = 0;  // radians per second
    float turnX = 0, turnY = 0;          // turn this frame
    float lagX = 0, lagY = 0;            // how far the view lags behind the new course
    float panX = 0, panY = 0;            // total turn, for the nebulae

    // The star the ship has been captured by.
    struct {
        bool on = false;
        float x, y, z;
        int kind = 1;          // see kStarKinds
        float radius;
        float r, g, b;         // its colour
        float light[3];        // colour and strength of the light it sheds on its planet
        float age;
        float axis[3], side[3];  // neutron star: the axis it spins about, and a line across it
        float beamPhase;
    } sun;
    // The planet circling that star, which the ship orbits.
    struct {
        bool on = false;
        float x, y, z;
        float radius, orbitRadius;
        float lap;             // seconds for the ship to go once round it
        float axis[3][3];      // its own axes; it spins about the second
        float spin;
        int kind;              // see kPlanetKinds
        float colA[3], colB[3], colC[3], atm[3];
        float seed[3];
        bool rings;
        bool wary;         // its people will send buoys to escort the ship away
        bool elevator;     // it has a space elevator, which the ship docks at
        float elevatorUp[3], elevatorPort[3];  // the way its tether rises, and the way its docking collar faces
        float ringCol[3];
        float age;
        // Its moons.
        int moons;
        struct {
            float distance;   // from the planet's centre
            float angle, speed, tilt;
            float radius;
            int kind;         // as for planets: cratered rock, ice or desert
            float colA[3], colB[3], colC[3];
            float seed[3];
        } moon[3];
    } planet;
    float orbitAxis[3] = {0, 1, 0};     // the ship circles the planet about this
    // In orbit the camera rises to ride along high above the ship.
    float wide = 0;                      // 0 camera with the ship .. 1 camera overhead
    float wideRate = 0;                  // how fast that is changing, in orbit
    float shipPos[3] = {0, 0, 0};        // where the ship really is, seen from the camera
    float shipRadial[3] = {0, 1, 0};     // straight out from the planet through the ship
    float shipTangent[3] = {0, 0, 1};    // the way the ship is going round
    float sinceOrbit = kCaptureRest;     // seconds since the last orbit ended
    float blur = 1;                      // motion blur, off while in orbit
    float shipFwd[3] = {0, 0, 1};        // which way the ship points, as seen from the camera behind it
    float shipBank = 0;                  // how far it leans into a turn, radians
    float shipUp[3] = {0, 1, 0};         // which way its deck faces
    float shipOrbiting = 0;              // 0 flying free .. 1 settled in orbit, belly to the planet
    float shipDocked = 1;                // 1 docked at the station .. 0 under way
    bool leavingStation = false;         // swinging round to head out from the station
    bool atElevator = false;             // visiting a planet's space elevator, from berthing until it heads out
    int berthStage = 0;                  // berthing: 0 coming to wait off the pad, 1 creeping in over it,
                                         // 2 settling down onto it
    float engines = 1;                   // 1 main engines lit .. 0 dark, as while docking
    bool enginesOff = false;             // the main engines are shut down: from entering orbit or docking,
                                         // until the next jump to warp
    float departAccel = 1, departTop = kCoastSpeed;  // heading out before a jump: how fast it speeds up, and to what
    float jumpFrom = kCoastSpeed;        // the speed a jump starts from
    float drawFwd[3] = {0, 0, 1};        // the way the ship is drawn pointing, and its deck facing, turning
    float drawUp[3] = {0, 1, 0};         // smoothly towards ShipAim
    bool drawReady = false;              // set once they have been put where ShipAim says
    float stepDt = 0;                    // the time step of this Update, for smoothing the steering
    float shipShrink = 1;                // see kElevatorShrink
    float moorFrom[6], moorTo[6];        // moored, the camera's place and the way it looks, at first and at last,
                                         // along the elevator's axes (up the tether, out of the port, across)
    // The space station the ship sets out from.
    struct {
        bool on = false;
        float x, y, z;
        float axis[3][3];                // its own axes: across, up, and along its hub towards the docking side
        float spin;                      // how far the wheel has turned
    } station;
    float shipLight[3] = {0.38f, 0.4f, 0.45f};  // colour and strength of the light on the ship
    float shipKey[3] = {0.35f, 0.85f, -0.4f};   // the way that light comes from
    // Guide buoys sent up by a wary planet: they fly in to flank the ship as
    // it enters orbit, escort it away, and are left behind when it jumps.
    struct {
        bool on = false;
        bool escorting;
        float time;                      // since they appeared
        float released;                  // when the ship left them behind
        float pos[2][3];                 // where they are, seen from the camera
    } buoys;

    std::mt19937 rng{(unsigned)GetTickCount()};

    float Rand() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    static void Normalize(float* v)
    {
        float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int i = 0; i < 3; i++) v[i] /= n;
    }

    static void Cross(const float* a, const float* b, float* out)
    {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    }

    // A colour a little different each time.
    void Vary(const float* in, float* out, float amount)
    {
        float k = 1.0f + (Rand() - 0.5f) * amount;
        for (int i = 0; i < 3; i++) out[i] = std::clamp(in[i] * k * (1.0f + (Rand() - 0.5f) * amount), 0.0f, 1.0f);
    }

    // Two unit vectors at right angles to each other, in random directions.
    void RandomFrame(float* a, float* b)
    {
        float any[3] = {Rand() - 0.5f, Rand() - 0.5f, Rand() - 0.5f};
        float other[3] = {Rand() - 0.5f, Rand() - 0.5f, Rand() - 0.5f};
        for (int i = 0; i < 3; i++) a[i] = any[i];
        Normalize(a);
        Cross(a, other, b);
        Normalize(b);
    }

    bool Capture(const Star& s)
    {
        if (phase != Cruise || sun.on || phaseTime < 3.0f || sinceOrbit < kCaptureRest) return false;

        // Which sort of star: a pulsar (a lone neutron star) only one time
        // in twenty, a red giant with a neutron star one time in forty, the
        // other kinds equally often.
        const float pick = Rand();
        int starKind = kNeutronStar;
        if (pick >= kPulsarChance + kDoubleChance) {
            static const int kOthers[] = {kRedGiant, kYellowStar, kBrownDwarf, kBinaryDwarf};
            const float rest = (pick - kPulsarChance - kDoubleChance) / (1.0f - kPulsarChance - kDoubleChance);
            starKind = kOthers[std::min(3, (int)(rest * 4))];
        } else if (pick >= kPulsarChance) {
            starKind = kDoubleStar;
        }
        const StarKind& sk = kStarKinds[starKind];
        sun.on = true;
        sun.x = s.x;
        sun.y = s.y;
        sun.z = s.z;
        sun.kind = (int)(&sk - kStarKinds);
        sun.radius = sk.radius;
        sun.r = sk.color[0];
        sun.g = sk.color[1];
        sun.b = sk.color[2];
        for (int i = 0; i < 3; i++) sun.light[i] = sk.light[i];
        sun.age = 0;
        RandomFrame(sun.axis, sun.side);
        sun.beamPhase = 0;

        // What sort of planet, and its own colours, size and rings. How
        // often it is an inhabited world depends on the star: most often
        // round brown dwarfs, then yellow stars, never round red giants or
        // neutron stars. Otherwise it is any of the other kinds.
        static const int kHabitable[] = {0, 5, 7};
        static const int kWild[] = {1, 2, 3, 4, 6, 8, 9};
        int kind = Rand() < sk.inhabited ? kHabitable[std::min(2, (int)(Rand() * 3))] : kWild[std::min(6, (int)(Rand() * 7))];
        // A brown dwarf is too cool to keep a world molten.
        const bool brown = sun.kind == kBrownDwarf || sun.kind == kBinaryDwarf;
        while (kind == 4 && brown) kind = kWild[std::min(6, (int)(Rand() * 7))];
        const PlanetKind& pk = kPlanetKinds[kind];
        // Most planets are near their kind's usual size, a few much smaller
        // and some huge: giants most often, but now and then a vast rocky world.
        float big = kind == 2 || kind == 9 ? 0.35f : 0.15f;
        planet.radius = pk.radius * (Rand() < big ? 2.5f + 3.0f * Rand() : 0.55f + 0.9f * Rand());

        // Its planet is on the near side of it, off to one side, so the ship
        // reaches the planet first and sees the star beyond; a huge one
        // circles farther out, clear of the star.
        float toSun[3] = {s.x, s.y, s.z}, side[3], any[3] = {Rand() - 0.5f, Rand() - 0.5f, Rand() - 0.5f};
        Normalize(toSun);
        Cross(toSun, any, side);
        Normalize(side);
        float off[3];
        for (int i = 0; i < 3; i++) off[i] = -0.6f * toSun[i] + 0.8f * side[i];
        const float away = std::max(sk.planetDistance, sk.radius + 3.5f * planet.radius);
        planet.on = true;
        planet.x = s.x + off[0] * away;
        planet.y = s.y + off[1] * away;
        planet.z = s.z + off[2] * away;
        // Spinning about an axis tipped a little from upright.
        float* a0 = planet.axis[0];
        float* a1 = planet.axis[1];
        float* a2 = planet.axis[2];
        a1[0] = (Rand() - 0.5f) * 0.8f;
        a1[1] = 1.0f;
        a1[2] = (Rand() - 0.5f) * 0.8f;
        Normalize(a1);
        float fwd[3] = {0, 0, 1};
        Cross(a1, fwd, a0);
        Normalize(a0);
        Cross(a0, a1, a2);
        planet.spin = Rand() * 6.2831853f;

        int palette = std::min(pk.palettes - 1, (int)(Rand() * pk.palettes));
        planet.kind = kind;
        Vary(pk.colors[palette][0], planet.colA, 0.3f);
        Vary(pk.colors[palette][1], planet.colB, 0.3f);
        Vary(pk.colors[palette][2], planet.colC, 0.3f);
        Vary(pk.colors[palette][3], planet.atm, 0.2f);
        for (float& v : planet.seed) v = Rand() * 100.0f;
        planet.rings = Rand() < pk.ringChance;
        planet.wary = (kind == 0 || kind == 5 || kind == 7) && Rand() < 0.5f;  // half the inhabited worlds
        static const float kRingColors[2][3] = {{0.85f, 0.75f, 0.6f}, {0.75f, 0.8f, 0.85f}};
        Vary(kRingColors[Rand() < 0.5f], planet.ringCol, 0.25f);
        // Close to the surface, under any rings; over a huge world no higher
        // than over an ordinary one, so the ship stays in view.
        const float scale = std::min(planet.radius, kOrbitScale);
        planet.orbitRadius = planet.radius + (kLowOrbit - 1.0f) * scale;
        // Half the inhabited worlds (round yellow stars and brown dwarfs,
        // the only stars with any) have a space elevator: a tether rising
        // from the surface to a station the ship docks at. Such a world has
        // no rings for the tether to cut through, sends no buoys, and is
        // held still so the elevator stays put. The tether rises across the
        // line of sight on the side towards the star, tipped a little
        // towards the ship, so it stands out against the sky; the docking
        // pad faces the ship.
        planet.elevator = (kind == 0 || kind == 5 || kind == 7) && Rand() < 0.5f;
        if (planet.elevator) {
            planet.rings = false;
            planet.wary = false;
            float look[3] = {planet.x, planet.y, planet.z}, star[3] = {s.x - planet.x, s.y - planet.y, s.z - planet.z};
            Normalize(look);
            Normalize(star);
            float along = star[0] * look[0] + star[1] * look[1] + star[2] * look[2];
            float* up = planet.elevatorUp;
            for (int i = 0; i < 3; i++) up[i] = star[i] - look[i] * along;
            if (up[0] * up[0] + up[1] * up[1] + up[2] * up[2] < 1e-4f) Cross(look, planet.axis[0], up);
            Normalize(up);
            for (int i = 0; i < 3; i++) up[i] -= look[i] * 0.35f;
            Normalize(up);
            const float centre[3] = {planet.x, planet.y, planet.z};
            float toShip[3];  // from the station
            for (int i = 0; i < 3; i++) toShip[i] = -(centre[i] + up[i] * planet.radius * (1.0f + kElevatorHeight));
            float rise = toShip[0] * up[0] + toShip[1] * up[1] + toShip[2] * up[2];
            for (int i = 0; i < 3; i++) planet.elevatorPort[i] = toShip[i] - up[i] * rise;
            Normalize(planet.elevatorPort);
        }
        // Round a huge world the laps are longer, so the ground below
        // doesn't rush past too fast.
        planet.lap = kOrbitTime * sqrtf(planet.radius / scale);
        planet.age = 0;

        // Moons: up to three, the nearer ones quicker, all clear of any rings
        // and well clear of the star. A huge world's moons are no bigger and
        // no more spread out than an ordinary one's.
        float m = Rand();
        int moons = m < 0.25f ? 0 : m < 0.6f ? 1 : m < 0.85f ? 2 : 3;
        float reach = planet.radius * (planet.rings ? 2.8f : 2.0f);
        const float moonScale = std::min(planet.radius, 4.0f);
        static const int kMoonKinds[3] = {8, 3, 1};  // cratered rock, ice, desert
        planet.moons = 0;
        for (int i = 0; i < moons; i++) {
            auto& mn = planet.moon[i];
            reach += moonScale * (0.8f + 0.9f * Rand());
            mn.radius = moonScale * (0.12f + 0.16f * Rand());
            if (reach + mn.radius > away - sk.radius - 2.0f) break;
            planet.moons++;
            mn.distance = reach;
            mn.angle = Rand() * 6.2831853f;
            mn.speed = 0.15f * powf(2.0f * planet.radius / mn.distance, 1.5f);
            mn.tilt = (Rand() - 0.5f) * 0.4f;
            mn.kind = planet.kind == 4 ? 4 : kMoonKinds[std::min(2, (int)(Rand() * 3))];  // a lava world's moons are molten too
            const PlanetKind& mk = kPlanetKinds[mn.kind];
            int pal = std::min(mk.palettes - 1, (int)(Rand() * mk.palettes));
            Vary(mk.colors[pal][0], mn.colA, 0.3f);
            Vary(mk.colors[pal][1], mn.colB, 0.3f);
            Vary(mk.colors[pal][2], mn.colC, 0.3f);
            for (float& v : mn.seed) v = Rand() * 100.0f;
            reach = mn.distance;
        }

        phase = Approach;
        phaseTime = 0;
        return true;
    }

    // The opening scene: docked at a great space station on the line
    // between an Earth-like world and its Moon, not far from the Moon.
    // Laid out round the station first, its axis pointing straight out from
    // the Earth's centre towards the Moon, then seen from the camera, far off
    // to one side with the Earth and the Moon both in view.
    void StartDocked()
    {
        phase = Docked;
        phaseTime = 0;
        phaseLength = kDockedTime;
        speed = 0;
        velX = velY = velZ = 0;
        sinceOrbit = 0;

        // The sun, far off behind the camera and above, lighting the Earth's
        // near side.
        const StarKind& sk = kStarKinds[1];  // a yellow star
        float sunDir[3] = {0.6f, 0.5f, 0.62f};
        Normalize(sunDir);
        sun.on = true;
        sun.x = sunDir[0] * 2500.0f;
        sun.y = sunDir[1] * 2500.0f;
        sun.z = sunDir[2] * 2500.0f;
        sun.kind = 1;
        sun.radius = sk.radius;
        sun.r = sk.color[0];
        sun.g = sk.color[1];
        sun.b = sk.color[2];
        for (int i = 0; i < 3; i++) sun.light[i] = sk.light[i];
        sun.age = 10;
        RandomFrame(sun.axis, sun.side);
        sun.beamPhase = 0;

        // Earth, straight below the station, its equator turned to the camera.
        planet.on = true;
        planet.radius = kEarthRadius;
        planet.x = 0;
        planet.y = 0;
        planet.z = -kMoonDistance * kLagrange;
        float* a0 = planet.axis[0];
        float* a1 = planet.axis[1];
        float* a2 = planet.axis[2];
        a1[0] = 0.3f;
        a1[1] = 0.9f;
        a1[2] = 0.3f;
        Normalize(a1);
        float fwd[3] = {0, 0, 1};
        Cross(a1, fwd, a0);
        Normalize(a0);
        Cross(a0, a1, a2);
        planet.spin = Rand() * 6.2831853f;
        planet.kind = 0;
        const float sea[3] = {0.02f, 0.08f, 0.25f}, land[3] = {0.18f, 0.32f, 0.12f}, high[3] = {0.5f, 0.42f, 0.28f};
        const float air[3] = {0.35f, 0.6f, 1.0f};
        for (int i = 0; i < 3; i++) {
            planet.colA[i] = sea[i];
            planet.colB[i] = land[i];
            planet.colC[i] = high[i];
            planet.atm[i] = air[i];
        }
        for (float& v : planet.seed) v = Rand() * 100.0f;
        planet.rings = false;
        planet.wary = false;
        planet.elevator = false;
        planet.orbitRadius = planet.radius * kLowOrbit;
        planet.lap = kOrbitTime;
        planet.age = 10;
        // Its Moon, straight on beyond the station: the angle round the
        // Earth's equator and the tilt from it that point along the
        // station's axis. The station keeps pace with it, so it stays put.
        planet.moons = 1;
        auto& mn = planet.moon[0];
        mn.distance = kMoonDistance;
        mn.angle = atan2f(hypotf(a1[2], a2[2]), a0[2]);
        mn.speed = 0;
        mn.tilt = atan2f(a1[2], a2[2]);
        mn.radius = planet.radius * 0.18f;
        mn.kind = 8;
        const float grey[3][3] = {{0.4f, 0.39f, 0.37f}, {0.62f, 0.6f, 0.57f}, {0.25f, 0.24f, 0.23f}};
        for (int i = 0; i < 3; i++) {
            mn.colA[i] = grey[0][i];
            mn.colB[i] = grey[1][i];
            mn.colC[i] = grey[2][i];
        }
        for (float& v : mn.seed) v = Rand() * 100.0f;

        // The station, its docking face turned away from the Earth, and the
        // ship docked at its hub, nose in.
        station.on = true;
        station.x = station.y = station.z = 0;
        station.spin = 0;
        for (int i = 0; i < 3; i++)
            for (int k = 0; k < 3; k++) station.axis[i][k] = i == k ? 1.0f : 0.0f;
        shipFwd[0] = shipFwd[1] = 0;
        shipFwd[2] = -1;
        shipUp[0] = shipUp[2] = 0;
        shipUp[1] = 1;
        shipPos[0] = shipPos[1] = 0;
        shipPos[2] = kHubPort + kShipBow;
        shipDocked = 1;
        wide = 1;

        // Now as the camera sees it: far off to the side, looking between the
        // Earth and the Moon (see Update, Docked).
        float view[3] = {kDockedWide[0], kDockedWide[1], kDockedWide[2]};
        Normalize(view);
        float cam[3] = {view[0] * kDockedFrom, view[1] * kDockedFrom, view[2] * kDockedFrom};
        float first[3];
        DockedLook(cam, first);
        float yaw, pitch;
        Aim(first[0], first[1], first[2], yaw, pitch);
        const Rotation look(yaw, pitch);
        auto place = [&](float& x, float& y, float& z) {
            x -= cam[0];
            y -= cam[1];
            z -= cam[2];
            look.Apply(x, y, z);
        };
        auto turn = [&](float* v) { look.Apply(v[0], v[1], v[2]); };
        place(station.x, station.y, station.z);
        place(planet.x, planet.y, planet.z);
        place(sun.x, sun.y, sun.z);
        place(shipPos[0], shipPos[1], shipPos[2]);
        for (auto& v : station.axis) turn(v);
        for (auto& v : planet.axis) turn(v);
        turn(sun.axis);
        turn(sun.side);
        turn(shipFwd);
        turn(shipUp);
    }

    // Which way the camera first looks, from cam: midway between the Earth
    // and its Moon.
    void DockedLook(const float* cam, float* look) const
    {
        float earth[3] = {planet.x - cam[0], planet.y - cam[1], planet.z - cam[2]}, moon[3];
        const auto& mn = planet.moon[0];
        float c = cosf(mn.angle), s = sinf(mn.angle), ct = cosf(mn.tilt), st = sinf(mn.tilt);
        for (int k = 0; k < 3; k++)
            moon[k] = earth[k] + mn.distance * (planet.axis[0][k] * c + (planet.axis[2][k] * ct + planet.axis[1][k] * st) * s);
        Normalize(earth);
        Normalize(moon);
        for (int k = 0; k < 3; k++) look[k] = earth[k] + moon[k];
        Normalize(look);
    }

    // While the camera closes in on the station the stars, which are much
    // farther off than the field of stars has room for, hold still.
    bool StarsStill() const { return phase == Docked; }

    // Turns towards a direction (yaw and pitch off the line of sight).
    void Steer(float yaw, float pitch, float gain, float maxRate)
    {
        // Eased into, so a turn neither starts nor stops with a jerk.
        const float k = std::min(1.0f, stepDt * 4.0f);
        turnRateX += (std::clamp(yaw * gain, -maxRate, maxRate) - turnRateX) * k;
        turnRateY += (std::clamp(pitch * gain, -maxRate, maxRate) - turnRateY) * k;
    }

    // Bends a heading out of a star and its system aside, if it would take
    // the ship through the star or close by it, to pass just clear of it.
    void ClearOfStar(float* v) const
    {
        if (!sun.on) return;
        float c[3] = {sun.x, sun.y, sun.z}, reach = sun.radius * 2.0f + 6.0f;
        if (sun.kind == kNeutronStar) reach = 8.0f;
        if (sun.kind == kBinaryDwarf) reach = kBinaryGap * 0.5f + sun.radius * 2.0f + 6.0f;  // both of the pair
        if (sun.kind == kDoubleStar) {
            // Both stars, and the gas streaming between them.
            for (int i = 0; i < 3; i++) c[i] += sun.side[i] * kDoubleGap * 0.5f;
            reach = kDoubleGap * 0.5f + sun.radius * 1.5f + 8.0f;
        }
        const float sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        const float d = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (sp < 1e-6f || d < 1e-3f) return;
        float dir[3] = {v[0] / sp, v[1] / sp, v[2] / sp};
        const float ahead = c[0] * dir[0] + c[1] * dir[1] + c[2] * dir[2];
        if (ahead <= 0.0f || d * d - ahead * ahead >= reach * reach) return;  // heading away, or clear of it
        // Past the edge of the star's reach on the side the ship was heading.
        float side[3] = {dir[0] * ahead - c[0], dir[1] * ahead - c[1], dir[2] * ahead - c[2]};
        if (side[0] * side[0] + side[1] * side[1] + side[2] * side[2] < 1e-6f) Cross(c, sun.axis, side);
        Normalize(side);
        const float a = d > reach ? std::min(asinf(reach / d) * 1.15f, 1.5f) : 1.5707963f;
        for (int i = 0; i < 3; i++) v[i] = (c[i] / d * cosf(a) + side[i] * sinf(a)) * sp;
    }

    // Turns unit vector v towards unit vector to by the fraction k of the
    // angle between them, but no more than most (radians), about axis when
    // they point opposite ways.
    static void TurnToward(float* v, const float* to, const float* axis, float k, float most)
    {
        float c = std::clamp(v[0] * to[0] + v[1] * to[1] + v[2] * to[2], -1.0f, 1.0f);
        float a = std::min(acosf(c) * k, most);
        if (a < 1e-6f) return;
        float n[3];
        Cross(v, to, n);
        if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-10f)
            for (int i = 0; i < 3; i++) n[i] = axis[i];
        Normalize(n);
        float nv[3], d = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
        Cross(n, v, nv);
        const float ca = cosf(a), sa = sinf(a);
        for (int i = 0; i < 3; i++) v[i] = v[i] * ca + nv[i] * sa + n[i] * d * (1 - ca);
        Normalize(v);
    }

    // Which way the ship should point and its deck face, seen from the
    // camera. Flying free it points the way it flies, upright, tipped back
    // so the camera above and behind sees its back. In orbit, and slowing to
    // a planet, its deck faces away from the planet and its bow along its
    // path round it; heading straight in towards the planet, where that
    // would make no sense, it keeps flying upright.
    void ShipAim(float* fwd, float* up) const
    {
        const float free = 1.0f - shipOrbiting;
        const float tilt = kShipTilt * free * (1.0f - shipDocked);  // level while docked
        for (int i = 0; i < 3; i++) {
            fwd[i] = shipFwd[i] + (i == 1 ? tilt : 0.0f);
            up[i] = shipUp[i];
        }
        if (planet.on && shipOrbiting > 0.0f) {
            // Measured where the ship really is in its orbit, rather than at
            // the spot ahead of the camera it is drawn when the camera is
            // with it.
            float out[3] = {-planet.x, -planet.y, -planet.z};
            float vel[3] = {velX, velY, velZ};
            if (phase == Orbit || atElevator)
                for (int i = 0; i < 3; i++) {
                    out[i] = shipRadial[i];
                    vel[i] = shipTangent[i];
                }
            Normalize(out);
            float radial = vel[0] * out[0] + vel[1] * out[1] + vel[2] * out[2];
            float along[3] = {vel[0] - out[0] * radial, vel[1] - out[1] * radial, vel[2] - out[2] * radial};
            const float vv = vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2];
            const float aa = along[0] * along[0] + along[1] * along[1] + along[2] * along[2];
            // How far its path runs round the planet rather than straight in.
            float circling = vv > 1e-12f ? std::clamp((sqrtf(aa / vv) - 0.1f) / 0.3f, 0.0f, 1.0f) : 0.0f;
            circling = circling * circling * (3 - 2 * circling);
            if (circling > 0.0f) {
                Normalize(along);
                Normalize(fwd);
                const float w = shipOrbiting * circling;
                for (int i = 0; i < 3; i++) {
                    fwd[i] += (along[i] - fwd[i]) * w;
                    up[i] += (out[i] - up[i]) * w;
                }
            }
        }
        Normalize(fwd);
        // The deck at right angles to the bow; if they line up, it keeps
        // facing the way it did.
        float d = up[0] * fwd[0] + up[1] * fwd[1] + up[2] * fwd[2];
        float deck[3] = {up[0] - fwd[0] * d, up[1] - fwd[1] * d, up[2] - fwd[2] * d};
        if (deck[0] * deck[0] + deck[1] * deck[1] + deck[2] * deck[2] < 0.01f) {
            d = drawUp[0] * fwd[0] + drawUp[1] * fwd[1] + drawUp[2] * fwd[2];
            for (int i = 0; i < 3; i++) deck[i] = drawUp[i] - fwd[i] * d;
        }
        Normalize(deck);
        for (int i = 0; i < 3; i++) up[i] = deck[i];
    }

    // Where the ship is drawn while the camera rides with it, seen from the
    // camera (before the view's roll): a little ahead and below, drifting
    // gently about. See DrawShip.
    void ChaseSpot(float* out) const
    {
        const float tt = (float)t;
        const float x = (kShipAt[0] + 0.02f * sinf(tt * 0.43f)) * shipShrink;
        const float y = (kShipAt[1] + 0.012f * sinf(tt * 0.71f + 1.0f)) * shipShrink;
        const float viewRoll = roll * shipOrbiting, c = cosf(-viewRoll), s = sinf(-viewRoll);
        out[0] = x * c - y * s;
        out[1] = x * s + y * c;
        out[2] = kShipAt[2] * shipShrink;
    }

    // A space elevator's station; where the ship lies docked at it, the
    // bottom of its saucer on the docking pad, facing the station; the spot
    // just above that it creeps in to before settling; and where it waits,
    // off the pad and above it, before creeping in.
    void ElevatorPoints(float* station, float* berth, float* over, float* hold) const
    {
        const float* up = planet.elevatorUp;
        const float* port = planet.elevatorPort;
        const float c[3] = {planet.x, planet.y, planet.z};
        const float sc = kShipScale * shipShrink;
        for (int i = 0; i < 3; i++) {
            station[i] = c[i] + up[i] * planet.radius * (1.0f + kElevatorHeight);
            float pad = station[i] + (port[i] * kElevatorPort + up[i] * kElevatorPadTop) * kElevatorScale;
            berth[i] = pad - up[i] * kSaucerBottom[0] * sc + port[i] * kSaucerBottom[1] * sc;
            over[i] = berth[i] + up[i] * kElevatorSettle;
            hold[i] = over[i] + (port[i] + up[i] * 0.4f) * kElevatorHold;
        }
    }

    // Where the planet's moon i is, seen from the camera: round the planet's
    // equator, tipped a little.
    void MoonPos(int i, float* out) const
    {
        const auto& mn = planet.moon[i];
        const float c = cosf(mn.angle), s = sinf(mn.angle), ct = cosf(mn.tilt), st = sinf(mn.tilt);
        const float centre[3] = {planet.x, planet.y, planet.z};
        for (int k = 0; k < 3; k++)
            out[k] = centre[k] + mn.distance * (planet.axis[0][k] * c + (planet.axis[2][k] * ct + planet.axis[1][k] * st) * s);
    }

    float CruiseSpeed() const
    {
        float f = 0.78f + 0.12f * sinf((float)t * 0.13f) + 0.1f * sinf((float)t * 0.051f + 1.0f);
        return kMaxSpeed * f;
    }

    void Update(float dt)
    {
        stepDt = dt;
        t += dt;
        phaseTime += dt;
        sinceOrbit += dt;
        flash *= expf(-dt * 3.0f);
        auto blend = [](float from, float to, float e) { return expf(logf(from) + (logf(to) - logf(from)) * e); };
        switch (phase) {
        case Cruise:
            speed = CruiseSpeed();
            break;
        case Docked: {
            // Held fast at the station. The camera closes in from far off to
            // the side, swinging round to the docking side as it comes and
            // turning from the Earth and Moon to the hub, until the wheel
            // fills the screen.
            speed = 0;
            float u = std::clamp((phaseTime - 2.0f) / (phaseLength - 4.0f), 0.0f, 1.0f);
            float e = u * u * (3 - 2 * u);
            float d = kDockedFrom * powf(kDockedTo / kDockedFrom, e);
            float wideView[3] = {kDockedWide[0], kDockedWide[1], kDockedWide[2]};
            float nearView[3] = {kDockedView[0], kDockedView[1], kDockedView[2]};
            Normalize(wideView);
            Normalize(nearView);
            float view[3], cam[3], first[3], hub[3], look[3];
            for (int i = 0; i < 3; i++) view[i] = wideView[i] + (nearView[i] - wideView[i]) * e;
            Normalize(view);
            const float st[3] = {station.x, station.y, station.z};
            for (int i = 0; i < 3; i++)
                cam[i] = st[i] + (station.axis[0][i] * view[0] + station.axis[1][i] * view[1] + station.axis[2][i] * view[2]) * d;
            DockedLook(cam, first);
            for (int i = 0; i < 3; i++) hub[i] = st[i] * 0.75f + shipPos[i] * 0.25f - cam[i];
            Normalize(hub);
            for (int i = 0; i < 3; i++) look[i] = first[i] + (hub[i] - first[i]) * e;
            float yaw, pitch;
            Aim(look[0], look[1], look[2], yaw, pitch);
            Rotation(yaw, pitch).Apply(cam[0], cam[1], cam[2]);
            // Moved and turned there in a single step.
            float perStep = dt > 0 ? 1.0f / dt : 0.0f;
            velX = cam[0] * perStep;
            velY = cam[1] * perStep;
            velZ = cam[2] * perStep;
            turnRateX = yaw * perStep;
            turnRateY = pitch * perStep;
            if (phaseTime > phaseLength) {
                phase = Undock;
                phaseTime = 0;
                phaseLength = kBackingTime;
            }
            break;
        }
        case Berth: {
            // Slowing to a space elevator's station, then, facing it, creeping
            // in over its docking pad and settling down onto it. As it comes
            // the ship is drawn smaller and the camera brought closer, which
            // looks no different, until it is in scale with the station.
            shipShrink += (kElevatorShrink - shipShrink) * std::min(1.0f, dt);
            float st[3], berth[3], over[3], hold[3], spot[3];
            ElevatorPoints(st, berth, over, hold);
            ChaseSpot(spot);
            const float* to = berthStage == 0 ? hold : berthStage == 1 ? over : berth;
            float err[3] = {to[0] - spot[0], to[1] - spot[1], to[2] - spot[2]};
            const float gap = sqrtf(err[0] * err[0] + err[1] * err[1] + err[2] * err[2]);
            const float gain = berthStage == 0 ? 0.5f : 0.8f;
            const float fastest = berthStage == 0 ? 0.5f : berthStage == 1 ? 0.04f : 0.015f;
            velX = err[0] * gain;
            velY = err[1] * gain;
            velZ = err[2] * gain;
            speed = sqrtf(velX * velX + velY * velY + velZ * velZ);
            if (speed > fastest) {
                velX *= fastest / speed;
                velY *= fastest / speed;
                velZ *= fastest / speed;
                speed = fastest;
            }
            // The ship heads the way it goes, level; from the time it creeps
            // in, it turns to face the station.
            {
                const float* up = planet.elevatorUp;
                float want[3] = {-planet.elevatorPort[0], -planet.elevatorPort[1], -planet.elevatorPort[2]};
                if (berthStage == 0) {
                    float rise = velX * up[0] + velY * up[1] + velZ * up[2];
                    float level[3] = {velX - up[0] * rise, velY - up[1] * rise, velZ - up[2] * rise};
                    if (level[0] * level[0] + level[1] * level[1] + level[2] * level[2] > 1e-8f)
                        for (int i = 0; i < 3; i++) want[i] = level[i];
                }
                Normalize(want);
                const float k = std::min(1.0f, dt * 1.5f);
                for (int i = 0; i < 3; i++) shipTangent[i] += (want[i] - shipTangent[i]) * k;
                Normalize(shipTangent);
            }
            float yaw, pitch;
            Aim(st[0], st[1], st[2], yaw, pitch);
            Steer(yaw, pitch, 1.5f, 0.6f);
            if (berthStage == 0 && gap < 0.02f) {
                berthStage = 1;
                enginesOff = true;  // it creeps in to dock with its main engines shut down
            }
            else if (berthStage == 1 && gap < 0.003f) berthStage = 2;
            if ((berthStage == 2 && gap < 0.0015f) || phaseTime > 60.0f) {
                // Docked: the ship is held where it is, and the camera is
                // free to move about it.
                phase = Moored;
                phaseTime = 0;
                phaseLength = kMooredTime;
                for (int i = 0; i < 3; i++) {
                    shipPos[i] = spot[i];
                    shipFwd[i] = -planet.elevatorPort[i];
                    shipUp[i] = planet.elevatorUp[i];
                }
                wide = 1;
                // Where the camera is and looks now, and where it drifts to:
                // off to one side of the ship, a little above the station,
                // looking down the tether to the world below.
                const float* up = planet.elevatorUp;
                const float* port = planet.elevatorPort;
                float across[3];
                Cross(up, port, across);
                const float* axes[3] = {up, port, across};
                const float to[3] = {0.06f, 0.3f, 0.24f}, at[3] = {-0.12f, 0.04f, 0.0f};
                for (int k = 0; k < 3; k++) {
                    const float* a = axes[k];
                    moorFrom[k] = -(st[0] * a[0] + st[1] * a[1] + st[2] * a[2]);
                    moorFrom[3 + k] = a[2];
                    moorTo[k] = to[k];
                    moorTo[3 + k] = at[k] - to[k];
                }
            }
            break;
        }
        case Moored: {
            // Docked at the elevator, while the camera drifts round.
            speed = 0;
            float st[3], berth[3], over[3], hold[3];
            ElevatorPoints(st, berth, over, hold);
            const float* up = planet.elevatorUp;
            const float* port = planet.elevatorPort;
            float across[3];
            Cross(up, port, across);
            float u = std::clamp(phaseTime / (phaseLength * 0.8f), 0.0f, 1.0f);
            float e = u * u * (3 - 2 * u), rel[6];
            for (int k = 0; k < 6; k++) rel[k] = moorFrom[k] + (moorTo[k] - moorFrom[k]) * e;
            float cam[3], look[3];
            for (int i = 0; i < 3; i++) {
                cam[i] = st[i] + up[i] * rel[0] + port[i] * rel[1] + across[i] * rel[2];
                look[i] = up[i] * rel[3] + port[i] * rel[4] + across[i] * rel[5];
            }
            float yaw, pitch;
            Aim(look[0], look[1], look[2], yaw, pitch);
            Rotation(yaw, pitch).Apply(cam[0], cam[1], cam[2]);
            // Moved and turned there in a single step (as in Docked).
            float perStep = dt > 0 ? 1.0f / dt : 0.0f;
            velX = cam[0] * perStep;
            velY = cam[1] * perStep;
            velZ = cam[2] * perStep;
            turnRateX = yaw * perStep;
            turnRateY = pitch * perStep;
            if (phaseTime > phaseLength) {
                // Then it backs out and heads off, as from the station it set
                // out from.
                phase = Undock;
                phaseTime = 0;
                phaseLength = kBackingTime;
            }
            break;
        }
        case Undock:
        case Rejoin: {
            // The ship backs slowly away from the hub, still facing it; or
            // lifts straight up off an elevator's pad.
            float u = std::min((phase == Undock ? phaseTime : kBackingTime) / 3.0f, 1.0f);
            float back = 0.3f * u * u * (3 - 2 * u) * shipShrink;
            for (int i = 0; i < 3; i++) shipPos[i] += (atElevator ? shipUp[i] : -shipFwd[i]) * back * dt;
            speed = back;
            if (phase == Undock) {
                velX = velY = velZ = 0;
                turnRateX = turnRateY = 0;
                if (phaseTime > phaseLength) {
                    phase = Rejoin;
                    phaseTime = 0;
                    phaseLength = kRejoinTime;
                }
            } else {
                // The camera comes in behind the ship, looking past it at the
                // station.
                float target[3];
                for (int i = 0; i < 3; i++) target[i] = shipPos[i] - shipFwd[i] * kShipAt[2] - shipUp[i] * kShipAt[1];
                if (atElevator) {
                    // So that the ship comes to where it is drawn riding
                    // with the camera, at its smaller size.
                    float spot[3];
                    ChaseSpot(spot);
                    for (int i = 0; i < 3; i++) target[i] = shipPos[i] - spot[i];
                }
                const float gain = 1.2f, fastest = 8.0f * shipShrink;
                velX = target[0] * gain;
                velY = target[1] * gain;
                velZ = target[2] * gain;
                float sp = sqrtf(velX * velX + velY * velY + velZ * velZ);
                if (sp > fastest) {
                    velX *= fastest / sp;
                    velY *= fastest / sp;
                    velZ *= fastest / sp;
                }
                float yaw, pitch;
                Aim(shipFwd[0], shipFwd[1], shipFwd[2], yaw, pitch);
                Steer(yaw, pitch, 2.0f, 0.8f);
                wide += (0.0f - wide) * std::min(1.0f, dt * 0.7f);
                if (phaseTime > phaseLength) {
                    // Then swing round to head out, and jump.
                    phase = Depart;
                    phaseTime = 0;
                    wide = 0;
                    // Heading briskly away from the Moon, out to one side of
                    // the station; or from an elevator, out and up, away from
                    // its world.
                    float out[3];
                    for (int i = 0; i < 3; i++)
                        out[i] = atElevator ? -shipFwd[i] + planet.elevatorUp[i] * 0.6f : station.axis[0][i] + shipFwd[i] * 0.4f;
                    Normalize(out);
                    if (!atElevator) {
                        departAccel = 3.0f;
                        departTop = 6.0f;
                    }
                    if (atElevator) ClearOfStar(out);
                    const float away = 0.3f * shipShrink;
                    velX = out[0] * away;
                    velY = out[1] * away;
                    velZ = out[2] * away;
                    leavingStation = true;
                    if (atElevator) sinceOrbit = 0;
                    atElevator = false;
                }
            }
            break;
        }
        case Coast:
            speed = kCoastSpeed;
            if (phaseTime > phaseLength) {
                phase = Jump;
                phaseTime = 0;
                phaseLength = 2.0f;
                jumpFrom = speed;
            }
            break;
        case Jump: {
            // A slow build, then a sudden surge up to speed.
            float u = std::min(phaseTime / phaseLength, 1.0f);
            float e = powf(u, 3.0f);
            speed = blend(std::max(jumpFrom, 0.1f), CruiseSpeed(), e);
            if (phaseTime < 1.8f) {
                float k = std::clamp((phaseTime - 1.5f) / 0.3f, 0.0f, 1.0f);
                flash = std::max(flash, 0.3f * k * k);
            }
            if (phaseTime > phaseLength) {
                phase = Cruise;
                phaseTime = 0;
            }
            break;
        }
        case Approach: {
            // Drop out of warp and come gently round to face the planet,
            // slowing while it turns and only closing in once it faces it,
            // then come to a stop at orbiting distance; or, if it has an
            // elevator, a little short of the elevator's station.
            float to[3] = {planet.x, planet.y, planet.z};
            float arrive = planet.radius + kArriveAt;
            if (planet.elevator) {
                float berth[3], over[3], hold[3];
                ElevatorPoints(to, berth, over, hold);
                arrive = 1.0f;
            }
            float yaw, pitch, dist = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
            Aim(to[0], to[1], to[2], yaw, pitch);
            Steer(yaw, pitch, 0.5f, 0.25f);
            const float facing = std::clamp(to[2] / std::max(dist, 1e-3f), 0.0f, 1.0f);
            float want = std::max(0.0f, (dist - arrive) * 0.25f) * facing * facing;
            if (want < speed)
                speed += (want - speed) * std::min(1.0f, dt * 1.0f);
            else
                speed = std::min(want, speed + dt * 0.5f);
            velX = velY = 0;
            velZ = speed;
            const bool arrived = (phaseTime > 2.0f && fabsf(dist - arrive) < 0.4f && speed < 0.5f) || phaseTime > 60.0f;
            if (arrived && planet.elevator) {
                phase = Berth;  // dock at its elevator instead of circling it
                phaseTime = 0;
                atElevator = true;
                berthStage = 0;
                // From here it keeps its deck to the sky above the
                // elevator's world, heading level (see ShipAim).
                const float* up = planet.elevatorUp;
                for (int i = 0; i < 3; i++) {
                    shipRadial[i] = up[i];
                    shipTangent[i] = (i == 2) - up[i] * up[2];
                }
                Normalize(shipTangent);
            } else if (arrived) {
                phase = Orbit;
                enginesOff = true;  // it coasts round, its main engines shut down
                phaseTime = 0;
                const bool inhabited = planet.kind == 0 || planet.kind == 5 || planet.kind == 7;
                phaseLength = planet.lap * (inhabited ? kOrbits : 1);
                // Circle round in the plane that holds the star, so that
                // partway round the star passes behind the planet.
                float toPlanet[3] = {planet.x, planet.y, planet.z};
                float toSun[3] = {sun.x - planet.x, sun.y - planet.y, sun.z - planet.z};
                Cross(toPlanet, toSun, orbitAxis);
                if (orbitAxis[0] * orbitAxis[0] + orbitAxis[1] * orbitAxis[1] + orbitAxis[2] * orbitAxis[2] < 1e-6f) {
                    orbitAxis[0] = 0;
                    orbitAxis[1] = 1;
                    orbitAxis[2] = 0;
                }
                Normalize(orbitAxis);

                wide = 0;
                wideRate = 0;
            }
            break;
        }
        case Orbit: {
            // The ship descends into a low orbit and circles the planet
            // twice. The camera rides along high above it, looking straight
            // down on it, until near the end it comes back down behind it.
            float dist = sqrtf(planet.x * planet.x + planet.y * planet.y + planet.z * planet.z);
            float toPlanet[3] = {planet.x / dist, planet.y / dist, planet.z / dist};
            float along[3];
            Cross(orbitAxis, toPlanet, along);
            Normalize(along);
            // A wary world sends its buoys up once the ship has settled into
            // its orbit.
            if (planet.wary && !buoys.on && phaseTime > kBuoyDelay) {
                buoys.on = true;
                buoys.escorting = true;
                buoys.time = 0;
            }
            float u = std::min(phaseTime / 3.0f, 1.0f);
            float omega = 6.2831853f / planet.lap * u * u * (3 - 2 * u);
            const float height = std::min(planet.radius, kOrbitScale) * kOverhead;
            float in = std::clamp((dist - (planet.orbitRadius + height)) * 0.5f, -2.0f, 2.0f) * u * u * (3 - 2 * u);
            velX = along[0] * omega * dist + toPlanet[0] * in;
            velY = along[1] * omega * dist + toPlanet[1] * in;
            velZ = along[2] * omega * dist + toPlanet[2] * in;

            // The ship, on its orbit a little ahead of the camera, so the
            // camera, as high as ever, looks down on it from behind.
            const float ahead = kBehind * height / planet.orbitRadius;  // radians round the planet
            const float ca = cosf(ahead), sa = sinf(ahead);
            for (int i = 0; i < 3; i++) {
                shipRadial[i] = -toPlanet[i] * ca + along[i] * sa;
                shipTangent[i] = along[i] * ca + toPlanet[i] * sa;
                shipPos[i] = planet.x * (i == 0) + planet.y * (i == 1) + planet.z * (i == 2) + shipRadial[i] * planet.orbitRadius;
            }

            // The camera rises slowly above the ship, and near the end comes
            // back down, each time gathering speed gently and settling gently
            // (a critically damped spring), never starting with a jolt.
            bool overhead = phaseTime < phaseLength - kWideTime;
            const float spring = 0.8f;
            wideRate += (((overhead ? 1.0f : 0.0f) - wide) * spring * spring - 2.0f * spring * wideRate) * dt;
            wide = std::clamp(wide + wideRate * dt, 0.0f, 1.0f);
            float look[3];
            // At first it turns from the planet it has come to, round to the
            // way the ship goes; then it looks down at the ship as it rises
            // above it, and back along its way as it comes down: turned
            // between them gradually, never all at once.
            {
                float down[3] = {shipPos[0], shipPos[1], shipPos[2]};
                Normalize(down);
                const float a = std::clamp(phaseTime / 5.0f, 0.0f, 1.0f), round = a * a * (3 - 2 * a);
                for (int i = 0; i < 3; i++) {
                    const float ahead = toPlanet[i] + (along[i] - toPlanet[i]) * round;
                    look[i] = ahead + (down[i] - ahead) * wide;
                }
            }
            float yaw, pitch;
            Aim(look[0], look[1], look[2], yaw, pitch);
            Steer(yaw, pitch, 2.0f, 0.35f);
            speed = omega * dist;
            if (phaseTime > phaseLength) {
                // Fly on from where the orbit was going.
                phase = Depart;
                phaseTime = 0;
                sinceOrbit = 0;
                wide = 0;
                velX = along[0] * speed;
                velY = along[1] * speed;
                velZ = along[2] * speed;
                float v[3] = {velX, velY, velZ};
                ClearOfStar(v);
                velX = v[0];
                velY = v[1];
                velZ = v[2];
            }
            break;
        }
        case Depart: {
            // Fly straight on the way the orbit was heading, turning away
            // from the planet to face that way, then jump.
            float sp = std::max(1e-3f, sqrtf(velX * velX + velY * velY + velZ * velZ));
            float want = std::min(sp + dt * departAccel, std::max(departTop, sp));
            velX *= want / sp;
            velY *= want / sp;
            velZ *= want / sp;
            speed = want;
            float yaw, pitch;
            Aim(velX, velY, velZ, yaw, pitch);
            Steer(yaw, pitch, 0.8f, 0.35f);
            if (phaseTime > 1.0f && fabsf(yaw) < 0.02f && fabsf(pitch) < 0.02f) {
                phase = Jump;
                phaseTime = 0;
                phaseLength = 2.0f;
                jumpFrom = speed;
                enginesOff = false;  // the main engines light again for the jump
                departAccel = 1;
                departTop = kCoastSpeed;
                if (buoys.on && buoys.escorting) {
                    buoys.escorting = false;
                    buoys.released = buoys.time;
                }
            }
            break;
        }
        }
        const bool steered = phase == Approach || phase == Orbit || phase == Depart || phase == Docked || phase == Undock ||
                             phase == Rejoin || phase == Berth || phase == Moored;

        warp = std::clamp((speed - kCoastSpeed) / (kMaxSpeed * 0.7f - kCoastSpeed), 0.0f, 1.0f);
        rollRate = 0.05f * sinf((float)t * 0.031f) + 0.025f * sinf((float)t * 0.017f + 2.0f);
        rollRate *= (1.0f - shipOrbiting) * (1.0f - shipDocked);
        roll += rollRate * dt;
        if (planet.on && shipOrbiting > 0.0f) {
            // Straight out from the planet should point up the screen; looking
            // down from overhead, the ship's way forward should.
            float n = sqrtf(planet.x * planet.x + planet.y * planet.y + planet.z * planet.z) + 1e-6f;
            float pn[3] = {planet.x / n, planet.y / n, planet.z / n}, fw[3];
            Cross(orbitAxis, pn, fw);
            const float over = atElevator ? 0.0f : wide;  // at an elevator the camera is never overhead
            float ox = -pn[0] + (fw[0] + pn[0]) * over;
            float oy = -pn[1] + (fw[1] + pn[1]) * over;
            // When that way is nearly straight ahead or behind, it says little
            // about which way is up, so the view hardly turns for it.
            const float lean = ox * ox + oy * oy;
            if (lean > 1e-8f) {
                float want = 1.5707963f - atan2f(oy, ox);
                float diff = remainderf(want - roll, 6.2831853f);
                float sure = std::clamp((lean - 0.02f) / 0.13f, 0.0f, 1.0f);
                const float step = diff * std::min(1.0f, dt * 1.5f) * shipOrbiting * sure * sure * (3 - 2 * sure);
                roll += std::clamp(step, -dt * 0.35f, dt * 0.35f);  // and never faster than about 20 degrees a second
            }
        }
        zoom += dt * (0.002f + speed * kNebulaFlow);

        // A star's pull bends the ship's course; the turn builds and dies away
        // over kSwing seconds so a close pass swings the ship round smoothly.
        if (!steered) {
            velX = velY = 0;
            velZ = speed;
            float k = phase == Cruise ? dt / kSwing / std::max(speed, 30.0f) : 0.0f;
            float decay = expf(-dt / kSwing);
            turnRateX = (turnRateX + gravX * k) * decay;
            turnRateY = (turnRateY + gravY * k) * decay;
            float rate = hypotf(turnRateX, turnRateY);
            if (rate > kMaxTurn) {
                turnRateX *= kMaxTurn / rate;
                turnRateY *= kMaxTurn / rate;
            }
        }
        turnX = turnRateX * dt;
        turnY = turnRateY * dt;
        panX += turnX;
        panY += turnY;

        // The star and planet move just as the field of stars does (see
        // MoveStars).
        Rotation rot(turnX, turnY);
        if (phase == Depart) rot.Apply(velX, velY, velZ);  // keeps heading the same way as the ship turns
        if (sun.on) {
            rot.Apply(sun.x, sun.y, sun.z);
            sun.x -= velX * dt;
            sun.y -= velY * dt;
            sun.z -= velZ * dt;
            sun.age += dt;
            rot.Apply(sun.axis[0], sun.axis[1], sun.axis[2]);
            rot.Apply(sun.side[0], sun.side[1], sun.side[2]);
            sun.beamPhase += dt * 6.2831853f * 0.4f;
            float d2 = sun.x * sun.x + sun.y * sun.y + sun.z * sun.z;
            if (!steered && (sun.z < -60.0f || d2 > 1000.0f * 1000.0f)) sun.on = false;  // left behind
        }
        if (buoys.on) {
            buoys.time += dt;
            if (!buoys.escorting) {
                for (auto& b : buoys.pos) {
                    rot.Apply(b[0], b[1], b[2]);
                    b[0] -= velX * dt;
                    b[1] -= velY * dt;
                    b[2] -= velZ * dt;
                }
                if (buoys.time - buoys.released > 8.0f) buoys.on = false;
            }
        }
        if (phase == Docked || phase == Undock || phase == Rejoin || phase == Moored) {
            rot.Apply(shipPos[0], shipPos[1], shipPos[2]);
            shipPos[0] -= velX * dt;
            shipPos[1] -= velY * dt;
            shipPos[2] -= velZ * dt;
        }
        if (station.on) {
            station.spin += dt * 0.05f;
            rot.Apply(station.x, station.y, station.z);
            station.x -= velX * dt;
            station.y -= velY * dt;
            station.z -= velZ * dt;
            for (auto& a : station.axis) rot.Apply(a[0], a[1], a[2]);
            float d2 = station.x * station.x + station.y * station.y + station.z * station.z;
            if (!steered && (station.z < -60.0f || d2 > 400.0f * 400.0f)) station.on = false;
        }
        if (planet.on) {
            rot.Apply(planet.x, planet.y, planet.z);
            planet.x -= velX * dt;
            planet.y -= velY * dt;
            planet.z -= velZ * dt;
            for (auto& a : planet.axis) rot.Apply(a[0], a[1], a[2]);
            rot.Apply(orbitAxis[0], orbitAxis[1], orbitAxis[2]);
            rot.Apply(planet.elevatorUp[0], planet.elevatorUp[1], planet.elevatorUp[2]);
            rot.Apply(planet.elevatorPort[0], planet.elevatorPort[1], planet.elevatorPort[2]);
            if (atElevator) {
                rot.Apply(shipRadial[0], shipRadial[1], shipRadial[2]);
                rot.Apply(shipTangent[0], shipTangent[1], shipTangent[2]);
            }
            if (!planet.elevator) planet.spin += dt * 0.03f;
            for (int i = 0; i < planet.moons; i++) planet.moon[i].angle += planet.moon[i].speed * dt;
            planet.age += dt;
            // Gone once it and all its moons are wholly behind or far off.
            auto gone = [](const float* p, float r) {
                return p[2] < -60.0f - r || p[0] * p[0] + p[1] * p[1] + p[2] * p[2] > (1000.0f + r) * (1000.0f + r);
            };
            const float centre[3] = {planet.x, planet.y, planet.z};
            bool away = gone(centre, planet.radius);
            for (int i = 0; i < planet.moons && away; i++) {
                float m[3];
                MoonPos(i, m);
                away = gone(m, planet.moon[i].radius);
            }
            if (!steered && away) planet.on = false;
        }

        // The ship turns to point the way it is going, and leans into turns.
        rot.Apply(shipFwd[0], shipFwd[1], shipFwd[2]);
        float sp = sqrtf(velX * velX + velY * velY + velZ * velZ);
        float heading[3] = {velX / std::max(sp, 1e-6f), velY / std::max(sp, 1e-6f), velZ / std::max(sp, 1e-6f)};
        if (phase == Orbit) {
            for (int i = 0; i < 3; i++) heading[i] = shipTangent[i];
            sp = 1;
        }
        if (phase == Docked || phase == Undock || phase == Rejoin || phase == Moored) sp = 0;
        // At an elevator the ship keeps its deck to the sky above its world,
        // and docked, faces the collar (see DrawShip).
        if (atElevator) {
            const float k = std::min(1.0f, dt * 1.2f);
            for (int i = 0; i < 3; i++) {
                shipRadial[i] += (planet.elevatorUp[i] - shipRadial[i]) * k;
                if (phase != Berth) shipTangent[i] = -planet.elevatorPort[i];
            }
            Normalize(shipRadial);
        } else if (phase == Cruise) {
            // Well away at warp, back to its usual size.
            shipShrink += (1.0f - shipShrink) * std::min(1.0f, dt * 0.5f);
        }
        if (phase == Depart && leavingStation) {
            // Swinging round from the station, the ship turns with the view.
            heading[0] = heading[1] = 0;
            heading[2] = 1;
            sp = 1;
        }
        if (phase == Jump) leavingStation = false;
        // The main engines go dark while they are shut down.
        engines += ((enginesOff ? 0.0f : 1.0f) - engines) * std::min(1.0f, dt * 1.2f);
        shipDocked += ((phase == Docked || phase == Undock || phase == Rejoin || phase == Moored ? 1.0f : 0.0f) - shipDocked) *
                      std::min(1.0f, dt * 0.7f);
        if (sp > 0.05f) {
            float k = std::min(1.0f, dt * 1.5f);
            for (int i = 0; i < 3; i++) shipFwd[i] += (heading[i] - shipFwd[i]) * k;
            Normalize(shipFwd);
        }
        float lean = std::clamp(-turnRateX * 0.8f, -0.35f, 0.35f);
        shipBank += (lean - shipBank) * std::min(1.0f, dt * 2.0f);

        // In orbit it flies belly to the planet, its deck facing straight out
        // along the planet's radius; otherwise it keeps upright.
        bool orbiting = planet.on && (phase == Orbit || atElevator);  // level on the way in, so it never dives
        shipOrbiting += ((orbiting ? 1.0f : 0.0f) - shipOrbiting) * std::min(1.0f, dt * 0.4f);
        rot.Apply(shipUp[0], shipUp[1], shipUp[2]);
        float wantUp[3] = {0, 1, 0};
        if (planet.on) {
            float out[3] = {-planet.x, -planet.y, -planet.z};  // where the ship really is in its orbit
            if (phase == Orbit)
                for (int i = 0; i < 3; i++) out[i] = shipRadial[i];
            Normalize(out);
            for (int i = 0; i < 3; i++) wantUp[i] += (out[i] - wantUp[i]) * shipOrbiting;
            Normalize(wantUp);
        }
        if (!(phase == Docked || phase == Undock || phase == Rejoin || phase == Moored))  // docked, it keeps the station's up
            for (int i = 0; i < 3; i++) shipUp[i] += (wantUp[i] - shipUp[i]) * std::min(1.0f, dt * 2.0f);
        Normalize(shipUp);

        // The way the ship is drawn turns with the view, and smoothly towards
        // the way it should face, so it never jerks round or flips over.
        rot.Apply(drawFwd[0], drawFwd[1], drawFwd[2]);
        rot.Apply(drawUp[0], drawUp[1], drawUp[2]);
        {
            float aimFwd[3], aimUp[3];
            ShipAim(aimFwd, aimUp);
            if (!drawReady) {
                for (int i = 0; i < 3; i++) {
                    drawFwd[i] = aimFwd[i];
                    drawUp[i] = aimUp[i];
                }
                drawReady = true;
            } else {
                const float k = std::min(1.0f, dt * 1.2f), most = dt * 0.4f;  // never faster than about 23 degrees a second
                TurnToward(drawFwd, aimFwd, drawUp, k, most);
                // The deck, at right angles to the bow, turned about the bow.
                float d = drawUp[0] * drawFwd[0] + drawUp[1] * drawFwd[1] + drawUp[2] * drawFwd[2];
                for (int i = 0; i < 3; i++) drawUp[i] -= drawFwd[i] * d;
                Normalize(drawUp);
                d = aimUp[0] * drawFwd[0] + aimUp[1] * drawFwd[1] + aimUp[2] * drawFwd[2];
                float deck[3] = {aimUp[0] - drawFwd[0] * d, aimUp[1] - drawFwd[1] * d, aimUp[2] - drawFwd[2] * d};
                if (deck[0] * deck[0] + deck[1] * deck[1] + deck[2] * deck[2] > 1e-6f) {
                    Normalize(deck);
                    TurnToward(drawUp, deck, drawFwd, k, most);
                }
            }
        }

        // The light on the ship: a captured star's, fading as the ship flies
        // away from it, or the dim glow of deep space. It changes gradually.
        {
            static const float kDeep[3] = {0.38f, 0.4f, 0.45f};
            float key[3] = {0.35f, 0.85f, -0.4f}, light[3] = {kDeep[0], kDeep[1], kDeep[2]};
            Normalize(key);
            if (sun.on) {
                float toSun[3] = {sun.x - kShipAt[0], sun.y - kShipAt[1], sun.z - kShipAt[2]};
                float d = sqrtf(toSun[0] * toSun[0] + toSun[1] * toSun[1] + toSun[2] * toSun[2]);
                Normalize(toSun);
                // (The station's sun is far off, but lights it all the same.)
                float reach = std::max(std::clamp(1.0f - (d - 90.0f) / 150.0f, 0.0f, 1.0f), shipDocked);
                float w = std::min(sun.age / 3.0f, 1.0f) * reach;
                for (int i = 0; i < 3; i++) {
                    key[i] += (toSun[i] - key[i]) * w;
                    light[i] += (sun.light[i] * 1.2f - light[i]) * w;
                }
                Normalize(key);
            }
            float k = std::min(1.0f, dt * 0.6f);
            for (int i = 0; i < 3; i++) {
                shipLight[i] += (light[i] - shipLight[i]) * k;
                shipKey[i] += (key[i] - shipKey[i]) * k;
            }
            Normalize(shipKey);
        }

        // Stars stop streaking once the ship has settled into orbit.
        bool still = phase == Orbit || atElevator || (phase == Approach && speed < 2.0f);
        blur = std::clamp(blur + (still ? -dt : dt), 0.0f, 1.0f);

        // The view follows a change of course a little behind, so the point
        // the stars stream from swings towards a star that pulls the ship,
        // then settles back.
        float follow = expf(-dt / 1.5f);
        lagX = (lagX + (steered ? 0.0f : turnX)) * follow;
        lagY = (lagY + (steered ? 0.0f : turnY)) * follow;
        float cr = cosf(roll), sr = sinf(roll);
        // The view wanders gently, except when watching from far off in orbit.
        const float wander = 1.0f - wide;
        headingX = (0.06f * sinf((float)t * 0.043f) + 0.03f * sinf((float)t * 0.11f + 1.0f)) * wander +
                   kFocal * (lagX * cr - lagY * sr);
        headingY = (0.04f * sinf((float)t * 0.037f + 0.5f) + 0.02f * sinf((float)t * 0.09f)) * wander +
                   kFocal * (lagX * sr + lagY * cr);
    }
};

static Flight g_flight;

// ---------------------------------------------------------------------------
// Scene: one per window (one per monitor in full-screen mode)

struct Scene {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC rc = nullptr;
    int W = 0, H = 0;
    float S = 1;          // pixel scale relative to a 1080p screen

    GLuint progStreak = 0, progNebula = 0, progDown = 0, progBlur = 0, progComposite = 0, progSun = 0, progPlanet = 0;
    Target nebula, stars, blurA, blurB, shot;
    Target ship;          // the ship, drawn larger than the screen and shrunk to smooth its edges
    GLuint progShip = 0, progShipDown = 0;
    GLuint shipLetters = 0;  // the ship's registry, lettered on its saucer
    GLuint dockedLetters = 0;  // and the registry of the ship docked at a space elevator

    std::vector<Star> field;
    float feed = 0;       // stars owed to the edge the ship is turning towards
    std::vector<float> verts;
    std::mt19937 rng;

    float Rand() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
};

// Star colours, from hot blue-white to cool orange, and how common each is.
static const float kStarColors[][4] = {
    {0.62f, 0.73f, 1.00f, 0.15f},
    {0.80f, 0.86f, 1.00f, 0.25f},
    {1.00f, 1.00f, 1.00f, 0.25f},
    {1.00f, 0.93f, 0.78f, 0.18f},
    {1.00f, 0.80f, 0.55f, 0.12f},
    {1.00f, 0.62f, 0.40f, 0.05f},
};

// Puts a star at depth z where it will be seen at (u, v) screen heights from
// the middle of the screen (v up).
static void PlaceStar(Scene& sc, Star& s, float u, float v, float z)
{
    const float c = cosf(g_flight.roll), sn = sinf(g_flight.roll);
    s.x = (u * c + v * sn) * z / kFocal;
    s.y = (-u * sn + v * c) * z / kFocal;
    s.z = z;
    s.age = 0;
}

static void SpawnStar(Scene& sc, Star& s, float z)
{
    // Anywhere in view, and a little beyond its edges so that turning the
    // ship doesn't reveal empty space.
    PlaceStar(sc, s, (sc.Rand() - 0.5f) * 1.15f * sc.W / sc.H, (sc.Rand() - 0.5f) * 1.15f, z);
    // Many faint stars, a few bright ones.
    s.lum = 0.2f + 3.0f * powf(sc.Rand(), 6.0f);
    float pick = sc.Rand();
    int i = 0;
    while (i < 5 && pick > kStarColors[i][3]) pick -= kStarColors[i++][3];
    s.r = kStarColors[i][0];
    s.g = kStarColors[i][1];
    s.b = kStarColors[i][2];
}

// Where a star was kShutter seconds ago: the far end of its streak.
static void TailOf(const Star& s, float& x, float& y, float& z)
{
    const Flight& fl = g_flight;
    const float shutter = kShutter * fl.blur, move = fl.StarsStill() ? 0.0f : shutter;
    x = s.x + fl.velX * move;
    y = s.y + fl.velY * move;
    z = s.z + fl.velZ * move;
    Rotation(fl.turnRateX * shutter, fl.turnRateY * shutter).Undo(x, y, z);
    z = std::max(z, 0.05f);
}

static void MoveStars(Scene& sc, float dt)
{
    const Flight& fl = g_flight;
    const float f = kFocal * sc.H;
    const float cx = sc.W * 0.5f + fl.headingX * sc.H, cy = sc.H * 0.5f - fl.headingY * sc.H;
    const float margin = 8.0f * sc.S;
    const float c1 = cosf(fl.roll), s1 = sinf(fl.roll);
    const Rotation rot(fl.turnX, fl.turnY);
    auto offscreen = [&](float x, float y, float z) {
        float px = cx + f * (x * c1 - y * s1) / z, py = cy - f * (x * s1 + y * c1) / z;
        return px < -margin || px > sc.W + margin || py < -margin || py > sc.H + margin;
    };

    // Turning sweeps the stars across the screen, leaving a strip along the
    // edge the ship turns towards empty. Stars that leave the screen are
    // brought back in that strip.
    float wu = fl.turnX * c1 - fl.turnY * s1, wv = fl.turnX * s1 + fl.turnY * c1;  // screen frame
    float stripU = fabsf(wu) * kFocal, stripV = fabsf(wv) * kFocal;                 // screen heights
    float fracU = stripU * sc.H / sc.W, fracV = stripV;
    sc.feed = std::min(sc.feed + sc.field.size() * (fracU + fracV), sc.field.size() * 0.05f);
    auto respawn = [&](Star& s) {
        SpawnStar(sc, s, kFar);
        if (sc.feed < 1.0f) return;
        sc.feed -= 1.0f;
        float u0 = -cx / sc.H, u1 = (sc.W - cx) / sc.H;  // screen edges around the vanishing point
        float v0 = -(sc.H - cy) / sc.H, v1 = cy / sc.H;
        float u, v;
        if (sc.Rand() * (fracU + fracV) < fracU) {
            u = wu > 0 ? u1 - sc.Rand() * stripU : u0 + sc.Rand() * stripU;
            v = v0 + sc.Rand() * (v1 - v0);
        } else {
            u = u0 + sc.Rand() * (u1 - u0);
            v = wv > 0 ? v1 - sc.Rand() * stripV : v0 + sc.Rand() * stripV;
        }
        PlaceStar(sc, s, u, v, 5.0f + sc.Rand() * (kFar - 5.0f));
    };

    for (Star& s : sc.field) {
        // The ship turning swings every star the other way around it.
        rot.Apply(s.x, s.y, s.z);
        if (!fl.StarsStill()) {
            s.x -= fl.velX * dt;
            s.y -= fl.velY * dt;
            s.z -= fl.velZ * dt;
        }
        s.age += dt;
        if (s.z < kNear) {
            respawn(s);
            continue;
        }
        // Once both ends of its streak have left the screen the star has gone by.
        float tx, ty, tz;
        TailOf(s, tx, ty, tz);
        if (offscreen(s.x, s.y, s.z) && offscreen(tx, ty, tz)) respawn(s);
    }
}

// The sideways pull of the stars near the ship. Brighter stars are heavier.
// A star about to pass very close captures the ship.
static void Gravity(Scene& sc)
{
    float gx = 0, gy = 0;
    for (Star& s : sc.field) {
        if (s.z > 40.0f && s.z < 100.0f && hypotf(s.x, s.y) < kCapture && s.age > 1.0f && g_flight.Capture(s)) {
            SpawnStar(sc, s, kFar);  // it is drawn as the sun from now on
            continue;
        }
        // Only a close pass counts; further off, the pulls of all the
        // stars around cancel out.
        if (s.z > kReach) continue;
        float d2 = s.x * s.x + s.y * s.y + s.z * s.z + 1.0f;
        if (d2 > kReach * kReach) continue;
        float closeness = 1.0f - d2 / (kReach * kReach);
        float pull = kGravity * s.lum * s.lum * s.lum * closeness * closeness / (d2 * sqrtf(d2));
        gx += pull * s.x;
        gy += pull * s.y;
    }
    g_flight.gravX = gx;
    g_flight.gravY = gy;
}

static void DrawStars(Scene& sc)
{
    const Flight& fl = g_flight;
    const float f = kFocal * sc.H;
    const float cx = sc.W * 0.5f + fl.headingX * sc.H, cy = sc.H * 0.5f - fl.headingY * sc.H;
    const float c0 = cosf(fl.roll), s0 = sinf(fl.roll);
    const float rollThen = fl.roll - fl.rollRate * kShutter * fl.blur;
    const float c1 = cosf(rollThen), s1 = sinf(rollThen);

    std::vector<float>& v = sc.verts;
    v.clear();
    v.reserve(sc.field.size() * 54);
    auto push = [&v](float x, float y, float a, float c, float len, float w, float r, float g, float b) {
        v.insert(v.end(), {x, y, a, c, len, w, r, g, b});
    };
    for (const Star& s : sc.field) {
        float hx = cx + f * (s.x * c0 - s.y * s0) / s.z;
        float hy = cy - f * (s.x * s0 + s.y * c0) / s.z;
        float xt, yt, zt;
        TailOf(s, xt, yt, zt);
        float tx = cx + f * (xt * c1 - yt * s1) / zt;
        float ty = cy - f * (xt * s1 + yt * c1) / zt;

        float dx = hx - tx, dy = hy - ty;
        float len = sqrtf(dx * dx + dy * dy);
        float ux = 1, uy = 0;
        if (len > 1e-3f) {
            ux = dx / len;
            uy = dy / len;
        }
        float w = sc.S * (0.8f + std::min(1.5f, 15.0f / s.z));
        float fade = std::clamp((kFar - s.z) / (kFar * 0.3f), 0.0f, 1.0f) * std::min(1.0f, s.age / 0.3f);
        float bright = s.lum * fade * (0.3f + 40.0f / (s.z + 10.0f));
        // The longer the streak, the thinner its light is spread.
        bright *= powf(4.0f * w / (len + 4.0f * w), 0.25f);
        if (bright < 0.003f) continue;

        float m = 3.0f * w;
        float ax = tx - ux * m, ay = ty - uy * m;  // behind the tail
        float bx = hx + ux * m, by = hy + uy * m;  // ahead of the head
        float nx = -uy * m, ny = ux * m;
        float r = s.r * bright, g = s.g * bright, b = s.b * bright;
        push(ax + nx, ay + ny, -m, m, len, w, r, g, b);
        push(bx + nx, by + ny, len + m, m, len, w, r, g, b);
        push(bx - nx, by - ny, len + m, -m, len, w, r, g, b);
        push(ax + nx, ay + ny, -m, m, len, w, r, g, b);
        push(bx - nx, by - ny, len + m, -m, len, w, r, g, b);
        push(ax - nx, ay - ny, -m, -m, len, w, r, g, b);
    }
    if (v.empty()) return;

    glUseProgram(sc.progStreak);
    glUniform2f(glGetUniformLocation(sc.progStreak, "uRes"), (float)sc.W, (float)sc.H);
    glUniform1f(glGetUniformLocation(sc.progStreak, "uWarp"), fl.warp);
    for (int i = 0; i < 4; i++) glEnableVertexAttribArray(i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 36, v.data());
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, v.data() + 2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 36, v.data() + 4);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 36, v.data() + 6);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 9));
    for (int i = 1; i < 4; i++) glDisableVertexAttribArray(i);
}

// Where the captured star is on screen (pixels, y up) and how big it looks.
// It grows out of the point of light it was a moment ago.
// Where a point in space is on screen: pixels, y down (as the stars are
// drawn); false if it is behind the camera.
static bool ToScreen(const Scene& sc, const float* p, float& x, float& y)
{
    const Flight& fl = g_flight;
    if (p[2] < 0.5f) return false;
    const float f = kFocal * sc.H;
    const float cx = sc.W * 0.5f + fl.headingX * sc.H, cy = sc.H * 0.5f - fl.headingY * sc.H;
    const float c0 = cosf(fl.roll), s0 = sinf(fl.roll);
    x = cx + f * (p[0] * c0 - p[1] * s0) / p[2];
    y = cy - f * (p[0] * s0 + p[1] * c0) / p[2];
    return true;
}

// How far the captured star has grown out of the point of light it was a
// moment ago, 0..1.
static float SunGrown()
{
    float u = std::min(g_flight.sun.age / 3.0f, 1.0f);
    return u * u * (3 - 2 * u);
}

// The neutron star of a double star.
static void Companion(float* p)
{
    const Flight& fl = g_flight;
    p[0] = fl.sun.x + fl.sun.side[0] * kDoubleGap;
    p[1] = fl.sun.y + fl.sun.side[1] * kDoubleGap;
    p[2] = fl.sun.z + fl.sun.side[2] * kDoubleGap;
}

// The two brown dwarfs of a binary, slowly circling their middle.
static void BinaryPair(float* a, float* b)
{
    const Flight& fl = g_flight;
    float across[3];
    Flight::Cross(fl.sun.axis, fl.sun.side, across);
    const float turn = (float)fl.t * 0.06f, c = cosf(turn), s = sinf(turn);
    const float centre[3] = {fl.sun.x, fl.sun.y, fl.sun.z};
    for (int i = 0; i < 3; i++) {
        float d = (fl.sun.side[i] * c + across[i] * s) * kBinaryGap * 0.5f;
        a[i] = centre[i] + d;
        b[i] = centre[i] - d;
    }
}

// One star, seen up close, of the given kind (see kStarKinds).
static void DrawStar(Scene& sc, const float* pos, int kind)
{
    const Flight& fl = g_flight;
    float x, y;
    if (!fl.sun.on || !ToScreen(sc, pos, x, y)) return;
    const StarKind& sk = kStarKinds[kind];
    const float grown = SunGrown();
    const float radius = kFocal * sc.H * sk.radius / pos[2] * (0.15f + 0.85f * grown);

    // A neutron star's beams sweep round its spin axis.
    float beam[3], cr[3];
    Flight::Cross(fl.sun.axis, fl.sun.side, cr);
    for (int i = 0; i < 3; i++)
        beam[i] = 0.45f * fl.sun.axis[i] + 0.9f * (cosf(fl.sun.beamPhase) * fl.sun.side[i] + sinf(fl.sun.beamPhase) * cr[i]);
    Flight::Normalize(beam);
    const float c0 = cosf(fl.roll), s0 = sinf(fl.roll);

    GLuint p = sc.progSun;
    glUseProgram(p);
    glUniform2f(glGetUniformLocation(p, "uCenter"), x, sc.H - y);
    glUniform1f(glGetUniformLocation(p, "uRadius"), std::max(radius, kind == kNeutronStar ? 1.5f * sc.S : 1.0f));
    glUniform3f(glGetUniformLocation(p, "uColor"), sk.color[0], sk.color[1], sk.color[2]);
    glUniform1f(glGetUniformLocation(p, "uGlow"), 0.3f + 0.7f * grown);
    glUniform1i(glGetUniformLocation(p, "uKind"), kind);
    glUniform1f(glGetUniformLocation(p, "uTime"), (float)fl.t);
    glUniform3f(glGetUniformLocation(p, "uBeam"), beam[0] * c0 - beam[1] * s0, beam[0] * s0 + beam[1] * c0, beam[2]);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    DrawFullscreen();
    glBlendFunc(GL_ONE, GL_ONE);
}

// The gas a neutron star pulls off its red giant: a stream arcing across
// from the giant's near side, swept along by the pair's orbit, spiralling
// into a hot disc round the neutron star. Bright knots travel down the
// stream; the disc turns, fastest at its hot inner edge. Drawn as glowing
// segments, with the stars' streak shader.
static void DrawDoubleStarGas(Scene& sc)
{
    const Flight& fl = g_flight;
    const float f = kFocal * sc.H, t = (float)fl.t, fade = SunGrown();
    const float* side = fl.sun.side;
    const float* axis = fl.sun.axis;
    float along[3];
    Flight::Cross(axis, side, along);
    const float giant = kStarKinds[kRedGiant].radius, disc = 3.0f;
    float ns[3];
    Companion(ns);

    std::vector<float>& v = sc.verts;
    v.clear();
    // A glowing segment from a to b (points in space), width in space units.
    auto segment = [&](const float* a, const float* b, float width, float r, float g, float bl) {
        float ax, ay, bx, by;
        if (!ToScreen(sc, a, ax, ay) || !ToScreen(sc, b, bx, by)) return;
        float w = std::max(0.8f * sc.S, f * width / ((a[2] + b[2]) * 0.5f));
        float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        float ux = 1, uy = 0;
        if (len > 1e-3f) {
            ux = dx / len;
            uy = dy / len;
        }
        float m = 3.0f * w;
        float x0 = ax - ux * m, y0 = ay - uy * m, x1 = bx + ux * m, y1 = by + uy * m, nx = -uy * m, ny = ux * m;
        r *= fade;
        g *= fade;
        bl *= fade;
        v.insert(v.end(), {x0 + nx, y0 + ny, -m, m, len, w, r, g, bl, x1 + nx, y1 + ny, len + m, m, len, w, r, g, bl,
                           x1 - nx, y1 - ny, len + m, -m, len, w, r, g, bl, x0 + nx, y0 + ny, -m, m, len, w, r, g, bl,
                           x1 - nx, y1 - ny, len + m, -m, len, w, r, g, bl, x0 - nx, y0 - ny, -m, -m, len, w, r, g, bl});
    };
    auto point = [&](float* out, const float* c, float s1, const float* d1, float s2, const float* d2) {
        for (int i = 0; i < 3; i++) out[i] = c[i] + d1[i] * s1 + d2[i] * s2;
    };

    // The stream: a curve from the giant's surface to the edge of the disc.
    const float gc[3] = {fl.sun.x, fl.sun.y, fl.sun.z};
    float p0[3], p1[3], p2[3], p3[3];
    point(p0, gc, giant * 0.97f, side, 0.0f, along);
    point(p1, gc, giant + kDoubleGap * 0.3f, side, kDoubleGap * 0.18f, along);
    point(p2, ns, -disc * 1.6f, side, disc * 1.5f, along);
    point(p3, ns, -disc * 0.3f, side, disc * 0.95f, along);
    auto bezier = [&](float u, float* out) {
        float a = (1 - u) * (1 - u) * (1 - u), b = 3 * u * (1 - u) * (1 - u), c = 3 * u * u * (1 - u), d = u * u * u;
        for (int i = 0; i < 3; i++) out[i] = p0[i] * a + p1[i] * b + p2[i] * c + p3[i] * d;
    };
    const int steps = 90;
    float prev[3];
    bezier(0, prev);
    for (int k = 1; k <= steps; k++) {
        float u = (float)k / steps, cur[3];
        bezier(u, cur);
        float knot = powf(0.5f + 0.5f * sinf(6.2831853f * (u * 7.0f - t * 0.35f)), 3.0f);
        float bright = (0.5f + 0.5f * knot) * (0.7f + 0.8f * u) * 0.55f;
        float r = 1.0f, g = 0.45f + 0.4f * u, b = 0.2f + 0.4f * u;
        segment(prev, cur, 0.8f + (0.22f - 0.8f) * u, r * bright, g * bright, b * bright);
        for (int i = 0; i < 3; i++) prev[i] = cur[i];
    }

    // The disc: rings of glowing gas, turning, hottest and brightest within.
    const int rings = 14, around = 64;
    for (int ri = 0; ri < rings; ri++) {
        float rr = 0.35f + (disc - 0.35f) * ri / (rings - 1);
        float spin = t * 2.5f / powf(rr, 1.5f);
        float heat = 1.0f - (rr - 0.35f) / (disc - 0.35f);
        for (int k = 0; k < around; k++) {
            float a0 = 6.2831853f * k / around, a1 = 6.2831853f * (k + 1) / around;
            float a[3], b[3];
            point(a, ns, rr * cosf(a0), side, rr * sinf(a0), along);
            point(b, ns, rr * cosf(a1), side, rr * sinf(a1), along);
            float swirl = 0.6f + 0.4f * sinf(3.0f * a0 - spin + 6.0f * logf(rr));
            float bright = swirl * 0.5f / rr;
            float r = 1.0f, g = 0.55f + 0.35f * heat, bl = 0.25f + 0.75f * heat;
            segment(a, b, (disc - 0.35f) / rings * 0.9f, r * bright, g * bright, bl * bright);
        }
    }
    if (v.empty()) return;

    glUseProgram(sc.progStreak);
    glUniform2f(glGetUniformLocation(sc.progStreak, "uRes"), (float)sc.W, (float)sc.H);
    glUniform1f(glGetUniformLocation(sc.progStreak, "uWarp"), 0.0f);
    for (int i = 0; i < 4; i++) glEnableVertexAttribArray(i);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 36, v.data());
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, v.data() + 2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 36, v.data() + 4);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 36, v.data() + 6);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 9));
    for (int i = 1; i < 4; i++) glDisableVertexAttribArray(i);
}

// The star the ship has been captured by, seen up close. A double star's gas
// is drawn first, so the giant hides any of it that passes behind.
static void DrawSun(Scene& sc)
{
    const Flight& fl = g_flight;
    if (!fl.sun.on) return;
    const float pos[3] = {fl.sun.x, fl.sun.y, fl.sun.z};
    if (fl.sun.kind == kDoubleStar) {
        DrawDoubleStarGas(sc);
        DrawStar(sc, pos, kRedGiant);
        float ns[3];
        Companion(ns);
        DrawStar(sc, ns, kNeutronStar);
    } else if (fl.sun.kind == kBinaryDwarf) {
        // The farther first, so the nearer hides it as it passes in front.
        float a[3], b[3];
        BinaryPair(a, b);
        const bool aFar = a[0] * a[0] + a[1] * a[1] + a[2] * a[2] > b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
        DrawStar(sc, aFar ? a : b, kBrownDwarf);
        DrawStar(sc, aFar ? b : a, kBrownDwarf);
    } else {
        DrawStar(sc, pos, fl.sun.kind);
    }
}

// A world in front of the stars and its sun: the planet, or one of its moons.
struct Body {
    float pos[3];
    float radius;
    int kind;
    const float *colA, *colB, *colC, *atm, *seed;
    bool rings, civilized;
    bool round;           // always drawn round, however much of the view it fills
    float spin;
};

static void DrawBody(Scene& sc, const Body& w)
{
    const Flight& fl = g_flight;
    const auto& pl = fl.planet;
    const float cx = sc.W * 0.5f + fl.headingX * sc.H, cy = sc.H * 0.5f + fl.headingY * sc.H;  // y up
    float sun[3] = {fl.sun.x - w.pos[0], fl.sun.y - w.pos[1], fl.sun.z - w.pos[2]};
    Flight::Normalize(sun);
    float u = std::min(pl.age / 2.0f, 1.0f);

    GLuint p = sc.progPlanet;
    glUseProgram(p);
    glUniform2f(glGetUniformLocation(p, "uCenter"), cx, cy);
    glUniform1f(glGetUniformLocation(p, "uFocal"), kFocal * sc.H);
    glUniform1f(glGetUniformLocation(p, "uRoll"), fl.roll);
    glUniform3f(glGetUniformLocation(p, "uPos"), w.pos[0], w.pos[1], w.pos[2]);
    glUniform1f(glGetUniformLocation(p, "uRadius"), w.radius);
    glUniform3f(glGetUniformLocation(p, "uAxis0"), pl.axis[0][0], pl.axis[0][1], pl.axis[0][2]);
    glUniform3f(glGetUniformLocation(p, "uAxis1"), pl.axis[1][0], pl.axis[1][1], pl.axis[1][2]);
    glUniform3f(glGetUniformLocation(p, "uAxis2"), pl.axis[2][0], pl.axis[2][1], pl.axis[2][2]);
    glUniform1f(glGetUniformLocation(p, "uSpin"), w.spin);
    glUniform3f(glGetUniformLocation(p, "uSunDir"), sun[0], sun[1], sun[2]);
    glUniform3f(glGetUniformLocation(p, "uLight"), fl.sun.light[0], fl.sun.light[1], fl.sun.light[2]);
    glUniform1i(glGetUniformLocation(p, "uKind"), w.kind);
    glUniform3f(glGetUniformLocation(p, "uColA"), w.colA[0], w.colA[1], w.colA[2]);
    glUniform3f(glGetUniformLocation(p, "uColB"), w.colB[0], w.colB[1], w.colB[2]);
    glUniform3f(glGetUniformLocation(p, "uColC"), w.colC[0], w.colC[1], w.colC[2]);
    glUniform3f(glGetUniformLocation(p, "uAtm"), w.atm[0], w.atm[1], w.atm[2]);
    glUniform3f(glGetUniformLocation(p, "uSeed"), w.seed[0], w.seed[1], w.seed[2]);
    glUniform1i(glGetUniformLocation(p, "uRings"), w.rings ? 1 : 0);
    glUniform3f(glGetUniformLocation(p, "uRingCol"), pl.ringCol[0], pl.ringCol[1], pl.ringCol[2]);
    glUniform1f(glGetUniformLocation(p, "uFade"), u * u * (3 - 2 * u));
    glUniform1i(glGetUniformLocation(p, "uOctaves"), fl.wide > 0.2f ? 8 : 7);  // finer seen from close above
    glUniform1i(glGetUniformLocation(p, "uCivilized"), w.civilized ? 1 : 0);
    // Drawn round unless it fills much of the view, as a planet does when the
    // ship flies low over it.
    const float dist = sqrtf(w.pos[0] * w.pos[0] + w.pos[1] * w.pos[1] + w.pos[2] * w.pos[2]);
    const float size = asinf(std::min(1.0f, w.radius / std::max(dist, 1e-6f)));
    const float k = w.round ? 0.0f : std::clamp((size - 0.35f) / 0.25f, 0.0f, 1.0f);
    const float straighten = 1.0f - k * k * (3 - 2 * k);
    glUniform1f(glGetUniformLocation(p, "uStraighten"), straighten);
    // Only the pixels it, its air and its rings can cover need drawing: a
    // box round where it is on the screen, unless it is near or behind.
    const float reach = asinf(std::min(1.0f, w.radius * (w.rings ? 2.4f : 1.4f) / std::max(dist, 1e-6f)));
    const float off = acosf(std::clamp(w.pos[2] / std::max(dist, 1e-6f), -1.0f, 1.0f));  // from the line of sight
    const bool boxed = w.pos[2] > 0.0f && off + reach < 1.35f;
    if (boxed) {
        // Out from the middle of the view towards it, a wide view stretches
        // it from tan(off - reach) to tan(off + reach); drawn round, it
        // reaches tan(reach) / cos(off) either side of its centre. Across
        // that, it is no wider than tan(reach) / cos(off + reach).
        const float f = kFocal * sc.H, z = w.pos[2];
        const float vx = w.pos[0] / z, vy = w.pos[1] / z, len = sqrtf(vx * vx + vy * vy);
        const float ux = len > 1e-6f ? vx / len : 1.0f, uy = len > 1e-6f ? vy / len : 0.0f;
        const float round = tanf(reach) / cosf(off);
        const float in = std::min(tanf(off - reach), len - round), out = std::max(tanf(off + reach), len + round);
        const float half = tanf(reach) / cosf(off + reach);
        const float c = cosf(fl.roll), sn = sinf(fl.roll);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (float r : {in, out})
            for (float a : {-half, half}) {
                const float qx = ux * r - uy * a, qy = uy * r + ux * a;
                const float px = cx + (qx * c - qy * sn) * f, py = cy + (qx * sn + qy * c) * f;
                x0 = std::min(x0, px);
                x1 = std::max(x1, px);
                y0 = std::min(y0, py);
                y1 = std::max(y1, py);
            }
        const int bx0 = std::max(0, (int)(x0 - 4)), by0 = std::max(0, (int)(y0 - 4));
        const int bx1 = std::min(sc.W, (int)(x1 + 5)), by1 = std::min(sc.H, (int)(y1 + 5));
        if (bx1 <= bx0 || by1 <= by0) return;  // off the screen
        glEnable(GL_SCISSOR_TEST);
        glScissor(bx0, by0, bx1 - bx0, by1 - by0);
    }
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    DrawFullscreen();
    if (boxed) glDisable(GL_SCISSOR_TEST);
    glBlendFunc(GL_ONE, GL_ONE);
}

// The planet the ship orbits and its moons, the farthest first so nearer
// ones hide them.
static void DrawPlanet(Scene& sc)
{
    const Flight& fl = g_flight;
    if (!fl.planet.on) return;
    const auto& pl = fl.planet;
    static const float kNoAir[3] = {0, 0, 0};

    Body bodies[4];
    int n = 0;
    Body& planet = bodies[n++];
    planet.pos[0] = pl.x;
    planet.pos[1] = pl.y;
    planet.pos[2] = pl.z;
    planet.radius = pl.radius;
    planet.kind = pl.kind;
    planet.colA = pl.colA;
    planet.colB = pl.colB;
    planet.colC = pl.colC;
    planet.atm = pl.atm;
    planet.seed = pl.seed;
    planet.rings = pl.rings;
    planet.civilized = pl.kind == 0 || pl.kind == 5 || pl.kind == 7;  // temperate, ocean, jungle
    planet.round = pl.elevator;  // the ship docks high above it, never skimming it
    planet.spin = pl.spin;
    for (int i = 0; i < pl.moons; i++) {
        const auto& mn = pl.moon[i];
        Body& b = bodies[n++];
        // Round the planet's equator, tipped a little.
        float c = cosf(mn.angle), s = sinf(mn.angle), ct = cosf(mn.tilt), st = sinf(mn.tilt);
        for (int k = 0; k < 3; k++)
            b.pos[k] = planet.pos[k] + mn.distance * (pl.axis[0][k] * c + (pl.axis[2][k] * ct + pl.axis[1][k] * st) * s);
        b.radius = mn.radius;
        b.kind = mn.kind;
        b.colA = mn.colA;
        b.colB = mn.colB;
        b.colC = mn.colC;
        b.atm = kNoAir;
        b.seed = mn.seed;
        b.rings = false;
        b.civilized = false;
        b.round = false;
        b.spin = mn.angle;  // keeping one face to its planet
    }
    auto dist2 = [](const Body& b) { return b.pos[0] * b.pos[0] + b.pos[1] * b.pos[1] + b.pos[2] * b.pos[2]; };
    std::sort(bodies, bodies + n, [&](const Body& a, const Body& b) { return dist2(a) > dist2(b); });
    for (int i = 0; i < n; i++) DrawBody(sc, bodies[i]);
}

#include "ship.h"

static void Render(Scene& sc, const Target* out)
{
    const Flight& fl = g_flight;
    const float cx = sc.W * 0.5f + fl.headingX * sc.H, cy = sc.H * 0.5f + fl.headingY * sc.H;  // y up

    // Nebulae, at half resolution: they have no fine detail.
    BindTarget(sc.nebula);
    glUseProgram(sc.progNebula);
    glUniform2f(glGetUniformLocation(sc.progNebula, "uRes"), (float)sc.nebula.w, (float)sc.nebula.h);
    glUniform2f(glGetUniformLocation(sc.progNebula, "uCenter"), cx * sc.nebula.w / sc.W, cy * sc.nebula.h / sc.H);
    glUniform1f(glGetUniformLocation(sc.progNebula, "uZoom"), fl.zoom);
    glUniform1f(glGetUniformLocation(sc.progNebula, "uSmear"), fl.speed * kNebulaFlow * kShutter * fl.blur);
    glUniform1f(glGetUniformLocation(sc.progNebula, "uRoll"), fl.roll);
    glUniform2f(glGetUniformLocation(sc.progNebula, "uPan"), kFocal * fl.panX, kFocal * fl.panY);
    DrawFullscreen();

    // Stars.
    BindTarget(sc.stars);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    DrawStars(sc);
    DrawSun(sc);
    DrawPlanet(sc);
    DrawShip(sc);
    glDisable(GL_BLEND);
    BindTexture(0, sc.stars.tex);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Their glow.
    BindTarget(sc.blurA);
    glUseProgram(sc.progDown);
    glUniform1i(glGetUniformLocation(sc.progDown, "uTex"), 0);
    glUniform2f(glGetUniformLocation(sc.progDown, "uRes"), (float)sc.blurA.w, (float)sc.blurA.h);
    DrawFullscreen();
    glUseProgram(sc.progBlur);
    glUniform1i(glGetUniformLocation(sc.progBlur, "uTex"), 0);
    glUniform2f(glGetUniformLocation(sc.progBlur, "uRes"), (float)sc.blurA.w, (float)sc.blurA.h);
    for (int pass = 0; pass < 2; pass++) {
        BindTarget(sc.blurB);
        BindTexture(0, sc.blurA.tex);
        glUniform2f(glGetUniformLocation(sc.progBlur, "uDir"), 1.0f / sc.blurA.w, 0.0f);
        DrawFullscreen();
        BindTarget(sc.blurA);
        BindTexture(0, sc.blurB.tex);
        glUniform2f(glGetUniformLocation(sc.progBlur, "uDir"), 0.0f, 1.0f / sc.blurA.h);
        DrawFullscreen();
    }

    // Composite.
    if (out) {
        BindTarget(*out);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, sc.W, sc.H);
    }
    GLuint p = sc.progComposite;
    glUseProgram(p);
    BindTexture(0, sc.nebula.tex);
    BindTexture(1, sc.stars.tex);
    BindTexture(2, sc.blurA.tex);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(p, "uNebula"), 0);
    glUniform1i(glGetUniformLocation(p, "uStars"), 1);
    glUniform1i(glGetUniformLocation(p, "uBloom"), 2);
    glUniform2f(glGetUniformLocation(p, "uRes"), (float)sc.W, (float)sc.H);
    glUniform2f(glGetUniformLocation(p, "uCenter"), cx, cy);
    glUniform1f(glGetUniformLocation(p, "uWarp"), fl.warp);
    glUniform1f(glGetUniformLocation(p, "uFlash"), fl.flash);
    DrawFullscreen();
}

static void InitScene(Scene& sc, bool makeShotTarget)
{
    RECT rc;
    GetClientRect(sc.hwnd, &rc);
    sc.W = std::max(1, (int)(rc.right - rc.left));
    sc.H = std::max(1, (int)(rc.bottom - rc.top));
    sc.S = std::max(0.35f, sc.H / 1080.0f);
    sc.rng.seed((unsigned)GetTickCount() ^ (unsigned)(uintptr_t)sc.hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.iLayerType = PFD_MAIN_PLANE;
    sc.dc = GetDC(sc.hwnd);
    SetPixelFormat(sc.dc, ChoosePixelFormat(sc.dc, &pfd), &pfd);
    sc.rc = wglCreateContext(sc.dc);
    if (!sc.rc || !wglMakeCurrent(sc.dc, sc.rc)) Fail("Could not create an OpenGL context");
    if (!glCreateShader && !LoadGL()) Fail("OpenGL 3.0 or newer is required");

    sc.progStreak = LinkProgram(kStreakVS, kStreakFS);
    sc.progNebula = LinkProgram(kFullscreenVS, kNebulaFS);
    sc.progDown = LinkProgram(kFullscreenVS, kDownFS);
    sc.progBlur = LinkProgram(kFullscreenVS, kBlurFS);
    sc.progComposite = LinkProgram(kFullscreenVS, kCompositeFS);
    sc.progSun = LinkProgram(kFullscreenVS, kSunFS);
    sc.progPlanet = LinkProgram(kFullscreenVS, kPlanetFS);
    sc.progShip = LinkProgram(kShipVS, kShipFS);
    sc.progShipDown = LinkProgram(kFullscreenVS, kShipDownFS);

    sc.nebula = MakeTarget(std::max(1, sc.W / 2), std::max(1, sc.H / 2), GL_RGBA16F, false);
    sc.stars = MakeTarget(sc.W, sc.H, GL_RGBA16F, true);
    sc.blurA = MakeTarget(std::max(1, sc.W / 4), std::max(1, sc.H / 4), GL_RGBA16F, false);
    sc.blurB = MakeTarget(sc.blurA.w, sc.blurA.h, GL_RGBA16F, false);
    InitShip(sc);
    if (makeShotTarget) sc.shot = MakeTarget(sc.W, sc.H, GL_RGBA8, false);

    // Wide screens see more of the field, so need more stars to fill it.
    float aspect = std::max(1.0f, (float)sc.W / sc.H / (16.0f / 9.0f));
    sc.field.resize((size_t)(kStars * aspect));
    for (Star& s : sc.field) SpawnStar(sc, s, kNear + sc.Rand() * (kFar - kNear));
}

static bool SaveBmp(const char* path, int w, int h, const std::vector<uint8_t>& rgba)
{
    int stride = (w * 3 + 3) & ~3;
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + stride * h;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;  // bottom-up, same as glReadPixels
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    std::vector<uint8_t> row(stride, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* p = &rgba[(y * w + x) * 4];
            row[x * 3 + 0] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        fwrite(row.data(), stride, 1, f);
    }
    fclose(f);
    return true;
}

// Saves a picture as a JPEG. The pixels are RGBA, bottom row first (as from
// glReadPixels).
static bool SaveJpeg(const wchar_t* path, int w, int h, const std::vector<uint8_t>& rgba)
{
    std::vector<uint8_t> bgr((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t* p = &rgba[((size_t)(h - 1 - y) * w + x) * 4];
            uint8_t* q = &bgr[((size_t)y * w + x) * 3];
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
        }
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr)) {
        PROPBAG2 option = {};
        option.pstrName = (LPOLESTR)L"ImageQuality";
        VARIANT quality;
        VariantInit(&quality);
        quality.vt = VT_R4;
        quality.fltVal = 0.92f;
        props->Write(1, &option, &quality);
        hr = frame->Initialize(props);
    }
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 3, (UINT)bgr.size(), bgr.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (props) props->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) DeleteFileW(path);
    return SUCCEEDED(hr);
}

// Where a snapshot goes: the Downloads folder, named for the date and time,
// with a number added if a snapshot was already taken that second.
static std::wstring SnapshotPath()
{
    std::wstring dir;
    PWSTR downloads = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads))) dir = downloads;
    CoTaskMemFree(downloads);
    if (dir.empty()) return L"";
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf(name, 64, L"\\Starfield %04d-%02d-%02d %02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    std::wstring path = dir + name + L".jpg";
    for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; n++)
        path = dir + name + L" (" + std::to_wstring(n) + L").jpg";
    return path;
}

// ---------------------------------------------------------------------------
// Windows plumbing

enum class Mode { Config, Saver, Preview, Window, Shot };
static Mode g_mode = Mode::Config;
static bool g_quit = false;
static bool g_snapshot = false;  // save the next frame shown, as a JPEG (see SnapshotPath)
static bool g_haveCursor = false;
static POINT g_cursor;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    const bool saver = (g_mode == Mode::Saver);
    switch (msg) {
    case WM_SETCURSOR:
        if (saver) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (saver) {
            POINT p;
            GetCursorPos(&p);
            if (!g_haveCursor) {
                g_cursor = p;
                g_haveCursor = true;
            } else if (abs(p.x - g_cursor.x) + abs(p.y - g_cursor.y) > 8) {
                g_quit = true;
            }
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (saver || (g_mode == Mode::Window && wp == VK_ESCAPE)) g_quit = true;
        break;
    case WM_CHAR:
        if (g_mode == Mode::Window && (wp == 's' || wp == 'S')) g_snapshot = true;
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (saver) g_quit = true;
        break;
    case WM_ACTIVATEAPP:
        if (saver && !wp) g_quit = true;
        break;
    case WM_SYSCOMMAND:
        if (saver && ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_CLOSE)) return 0;
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        g_quit = true;
        return 0;
    case WM_DESTROY:
        g_quit = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL CALLBACK AddMonitor(HMONITOR, HDC, LPRECT r, LPARAM lp)
{
    ((std::vector<RECT>*)lp)->push_back(*r);
    return TRUE;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmdLine, int)
{
    // Parse the screensaver command line: "/s", "/p 1234", "/c:1234", ...
    SetProcessDPIAware();  // render at the real screen resolution, not a scaled-up one
    HWND parent = nullptr;
    std::string shotPath;
    const char* c = cmdLine;
    while (*c == ' ' || *c == '"') c++;

    // Started as Starfield.exe (e.g. from a desktop shortcut) with no
    // switches: open in a window instead of showing the settings box.
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    size_t len = strlen(exe);
    if (!*c && len > 4 && _stricmp(exe + len - 4, ".exe") == 0) g_mode = Mode::Window;

    if (*c == '/' || *c == '-') {
        char sw = (char)tolower(c[1]);
        const char* arg = c + 2;
        while (*arg == ' ' || *arg == ':') arg++;
        switch (sw) {
        case 's': g_mode = Mode::Saver; break;
        case 'p': g_mode = Mode::Preview; parent = (HWND)(uintptr_t)strtoull(arg, nullptr, 10); break;
        case 'w': g_mode = Mode::Window; break;
        case 'x': g_mode = Mode::Shot; shotPath = arg; break;
        default:  g_mode = Mode::Config; break;
        }
    }
    if (g_mode == Mode::Config) {
        MessageBoxW(nullptr,
                    L"Starfield\n\nFlying through deep space at extreme speed.\n\n"
                    L"This screensaver has no settings.",
                    L"Starfield", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (g_mode == Mode::Preview && !IsWindow(parent)) return 0;
    g_flight.StartDocked();

    WNDCLASSW wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    wc.lpszClassName = L"Starfield";
    RegisterClassW(&wc);

    std::vector<RECT> rects;
    DWORD style = WS_POPUP, exStyle = 0;
    if (g_mode == Mode::Saver) {
        EnumDisplayMonitors(nullptr, nullptr, AddMonitor, (LPARAM)&rects);
        style = WS_POPUP | WS_VISIBLE;
        exStyle = WS_EX_TOPMOST;
    } else if (g_mode == Mode::Preview) {
        RECT r;
        GetClientRect(parent, &r);
        rects.push_back(r);
        style = WS_CHILD | WS_VISIBLE;
    } else if (g_mode == Mode::Window) {
        RECT r = {0, 0, 1280, 720};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        rects.push_back({0, 0, r.right - r.left, r.bottom - r.top});
        style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    } else {
        rects.push_back({0, 0, 1920, 1080});  // hidden window, rendered offscreen
    }

    std::vector<Scene> scenes(rects.size());
    for (size_t i = 0; i < rects.size(); i++) {
        const RECT& r = rects[i];
        int x = r.left, y = r.top, w = r.right - r.left, h = r.bottom - r.top;
        if (g_mode == Mode::Window) {
            x = y = CW_USEDEFAULT;
        }
        scenes[i].hwnd = CreateWindowExW(exStyle, L"Starfield", L"Starfield", style, x, y, w, h,
                                         g_mode == Mode::Preview ? parent : nullptr, nullptr, inst, nullptr);
        if (!scenes[i].hwnd) return 1;
    }
    if (g_mode == Mode::Saver) SetForegroundWindow(scenes[0].hwnd);

    for (size_t i = 0; i < scenes.size(); i++) {
        InitScene(scenes[i], g_mode == Mode::Shot);
        if (wglSwapIntervalEXT) wglSwapIntervalEXT(i == 0 ? 1 : 0);
    }

    if (g_mode == Mode::Shot) {
        Scene& sc = scenes[0];
        // Fly for a while, then write three frames 0.1s apart.
        float seconds = 5.0f;
        size_t space = shotPath.find(' ');
        if (space != std::string::npos) {
            seconds = std::max(0.0f, (float)atof(shotPath.c_str() + space + 1));
            shotPath.resize(space);
        }
        const float dt = 1.0f / 60.0f;
        for (float t = 0; t < seconds; t += dt) {
            Gravity(sc);
            g_flight.Update(dt);
            MoveStars(sc, dt);
        }
        std::vector<uint8_t> px((size_t)sc.W * sc.H * 4);
        for (int frame = 0; frame < 3; frame++) {
            for (int i = 0; i < 6; i++) {
                Gravity(sc);
                g_flight.Update(dt);
                MoveStars(sc, dt);
            }
            Render(sc, &sc.shot);
            BindTarget(sc.shot);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, sc.W, sc.H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            std::string path = shotPath + "_" + std::to_string(frame) + ".bmp";
            if (!SaveBmp(path.c_str(), sc.W, sc.H, px)) return 1;
        }
        return 0;
    }

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    // Frames are shown at the screen's refresh rate, so the flight moves on
    // by whole refresh intervals: motion stays even when a frame takes a
    // little more or less time than usual. What is left over is carried to
    // the next frame.
    double refresh = 1.0 / 60.0, owed = 0.0;
    {
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 360)
            refresh = 1.0 / dm.dmDisplayFrequency;
    }
    Music music;
    if (g_mode != Mode::Preview) music.Start();  // the little preview in the settings dialog stays quiet
    MSG msg;
    double titleUntil = 0;  // when to put the window's title back after a snapshot
    while (!g_quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quit) break;
        QueryPerformanceCounter(&now);
        owed += (double)(now.QuadPart - last.QuadPart) / freq.QuadPart;
        const double steps = std::floor(owed / refresh + 0.5);
        float dt;
        if (steps < 1.0) {
            dt = (float)std::max(owed, 0.001);  // running faster than the screen refreshes: just the time taken
            owed = 0.0;
        } else {
            dt = (float)std::min(steps * refresh, 0.05);
            owed = std::clamp(owed - steps * refresh, -refresh, refresh);
        }
        last = now;
        Gravity(scenes[0]);  // every monitor shows the same ship, steered by the stars on the first
        g_flight.Update(dt);
        music.SetCruising(g_flight.phase == Flight::Cruise || g_flight.phase == Flight::Jump);
        for (Scene& sc : scenes) {
            RECT r;
            GetClientRect(sc.hwnd, &r);
            if (r.right <= 0 || r.bottom <= 0) continue;  // minimised
            wglMakeCurrent(sc.dc, sc.rc);
            if (r.right != sc.W || r.bottom != sc.H) {
                // The window was resized: start over at the new size.
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(sc.rc);
                ReleaseDC(sc.hwnd, sc.dc);
                HWND hwnd = sc.hwnd;
                sc = Scene();
                sc.hwnd = hwnd;
                InitScene(sc, false);
                if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);
            }
            MoveStars(sc, dt);
            Render(sc, nullptr);
            if (g_snapshot) {
                // Save the frame just drawn, and say so in the title bar.
                g_snapshot = false;
                std::vector<uint8_t> px((size_t)sc.W * sc.H * 4);
                glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, sc.W, sc.H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                std::wstring path = SnapshotPath();
                bool saved = !path.empty() && SaveJpeg(path.c_str(), sc.W, sc.H, px);
                std::wstring title = saved ? L"Starfield - saved " + path.substr(path.find_last_of(L'\\') + 1)
                                           : L"Starfield - could not save the picture";
                SetWindowTextW(sc.hwnd, title.c_str());
                titleUntil = g_flight.t + 4.0;
            }
            if (titleUntil > 0 && g_flight.t > titleUntil) {
                SetWindowTextW(sc.hwnd, L"Starfield");
                titleUntil = 0;
            }
            SwapBuffers(sc.dc);
        }
    }

    music.Stop();
    for (Scene& sc : scenes) {
        wglMakeCurrent(nullptr, nullptr);
        if (sc.rc) wglDeleteContext(sc.rc);
        if (IsWindow(sc.hwnd)) DestroyWindow(sc.hwnd);
    }
    return 0;
}
