// ship.h - the ship, seen from a chase camera above and behind it.
//
// An original design: a broad silver-white saucer for the crew, lettered
// with the ship's registry and ringed with lit windows, a raised dome on top
// ringed with windows and a red beacon blinking on it, a tapered delta spine
// running aft to two faint, steady ion streams, and broad, smoothly
// streamlined swept wings, rising like a manta's, with a row of red tail
// lights flashing outwards along their trailing edges, carrying two long
// cylindrical engine pods high at their tips. The pods have pointed nose cones, glowing exhausts
// trailing luminous ion streams, and rings of light flashing one after
// another from front to back, all brighter at warp.
//
// The camera looks where it always has, riding along with the ship, which
// flies a little ahead of it and below its line of sight. It is drawn into
// its own target at up to twice the screen's resolution, with a depth
// buffer, then shrunk into the stars layer so its edges are smooth.
//
// Part of main.cpp.

// Model space: x to starboard, y up, z towards the bow. Materials:
enum ShipMaterial { kHull = 0, kExhaust = 1, kWindows = 3, kDark = 4, kTrail = 5, kBeacon = 6,
                    kPodRing = 10,    // pod ring i (0 at the front) is kPodRing + i
                    kStrutLight = 20,    // strut tail light i (0 innermost) is kStrutLight + i
                    kBuoyLight = 30, kBuoyGrid = 31, kBuoyHull = 32,
                    kStationWindows = 33, kNavGreen = 34, kDockLight = 35, kSolarPanel = 36, kStationHull = 37 };
static const int kStrutLights = 6;
static const int kPodRings = 5;

static const wchar_t* kRegistry = L"FSS-908";   // lettered across the top of the saucer
static const float kShipScale = 0.62f;                    // model units to space units
static const float kShipOverhead = 0.16f;                 // seen from overhead, its size for each unit away
static const float kPodX = 0.32f, kPodY = 0.17f;          // where the engine pods are, model units
static const float kPodR = 0.045f;                        // their radius
static const float kPodBack = -0.85f;                     // where their exhausts are
static const float kShipTilt = 0.3f;                      // tipped back towards the camera, so it looks down on it

static const char* kShipVS = R"(#version 130
in vec3 aPos;
in vec3 aNormal;
in float aMat;
uniform vec3 uRight, uUp, uFwd;   // the ship's axes, seen from the camera
uniform vec3 uAt;                 // where it is, seen from the camera
uniform float uScale;
uniform float uRoll;              // roll of the view, followed in orbit
uniform vec2 uFocal;              // focal length over half the screen, x and y
uniform vec2 uShift;              // where the line of sight meets the screen, -1..1
out vec3 vPos;
out vec3 vNormal;
out vec3 vModel;
out float vMat;
void main() {
    vec3 p = uAt + (uRight * aPos.x + uUp * aPos.y + uFwd * aPos.z) * uScale;
    vPos = p;
    vNormal = uRight * aNormal.x + uUp * aNormal.y + uFwd * aNormal.z;
    vModel = aPos;
    vMat = aMat;
    float c = cos(uRoll), s = sin(uRoll);
    vec2 q = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    const float n = 0.05, f = 1000.0;
    gl_Position = vec4(q.x * uFocal.x + uShift.x * p.z, q.y * uFocal.y + uShift.y * p.z,
                       p.z * (f + n) / (f - n) - 2.0 * f * n / (f - n), p.z);
}
)";

static const char* kShipFS = R"(#version 130
in vec3 vPos;
in vec3 vNormal;
in vec3 vModel;
in float vMat;
uniform vec3 uLightDir;   // towards the light
uniform vec3 uLight;      // its colour and strength
uniform float uEngine;    // how hard the engines are working
uniform float uTime;
uniform sampler2D uLetters; // the registry, white on black

// Thin dark lines between hull plates.
float hash1(vec3 p) {
    p = fract(p * 0.3183099 + 0.1) * 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float panels(vec3 m) {
    float r = length(m.xz - vec2(0.0, 0.2));
    if (r < 0.37 && abs(m.y) < 0.155) {  // the saucer: rings and spokes
        float ang = atan(m.x, m.z - 0.2);
        float ring = abs(fract(r / 0.055) - 0.5);
        float spoke = abs(fract(ang * 16.0 / 6.2831853) - 0.5) * r * 6.0;
        return smoothstep(0.0, 0.06, min(ring, spoke));
    }
    vec3 g = abs(fract(m * vec3(14.0, 20.0, 9.0)) - 0.5);
    return smoothstep(0.0, 0.05, min(g.x, g.z));
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 v = normalize(-vPos);
    if (dot(n, v) < 0.0) n = -n;
    int mat = int(vMat + 0.5);
    if (mat == 5) {
        // An ion trail: vNormal carries how far along it (0 at the exhaust)
        // and across it (-1..1) this is, and whether it is the steady, faint
        // one from the spine. It adds light; it covers nothing.
        float along = vNormal.x, across = vNormal.y;
        float power = vNormal.z > 0.5 ? 0.22 : 0.15 + 0.75 * uEngine;
        float flicker = 0.75 + 0.25 * sin(uTime * 37.0 - along * 40.0) * sin(uTime * 23.0 + along * 17.0);
        float core = exp(-across * across * 6.0), halo = exp(-across * across * 1.5) * 0.35;
        vec3 c = mix(vec3(0.75, 0.9, 1.0), vec3(0.3, 0.45, 1.0), along);
        float fade = clamp(1.0 - along, 0.0, 1.0);
        gl_FragColor = vec4(c * (core + halo) * fade * fade * flicker * power, 0.0);
        return;
    }
    vec3 glow = vec3(0.0);
    vec3 base = vec3(0.86, 0.87, 0.9);
    float shine = 1.0;
    if (mat == 1) {
        glow = vec3(0.55, 0.78, 1.0) * (1.0 + 3.0 * uEngine);
    } else if (mat == 30) {
        // A buoy's red warning light: a sharp flash twice a second.
        glow = vec3(0.9, 0.02, 0.0) * (0.4 + 10.0 * exp(-fract(uTime * 2.0) * 9.0));
        base = vec3(0.3, 0.0, 0.0);
    } else if (mat == 31) {
        // A buoy's side: a grid of small white lights, flickering at random.
        float ang = atan(vModel.x, vModel.z) / 6.2831853;
        vec2 g = vec2(ang * 48.0, (vModel.y + 0.07) / 0.14 * 7.0);
        vec2 f = abs(fract(g) - 0.5);
        float cellLit = step(max(f.x, f.y), 0.22);
        float on = step(0.45, hash1(vec3(floor(g), floor(uTime * 7.0))));
        glow = vec3(1.0, 0.95, 0.85) * cellLit * on * 1.6;
        base = vec3(0.1, 0.1, 0.12);
        shine = 0.5;
    } else if (mat == 32) {
        // A buoy's hull: dark, gunmetal.
        base = vec3(0.16, 0.16, 0.19);
        shine = 0.8;
    } else if (mat == 33) {
        // Rows of station windows, most of them lit, round the station's axis.
        float ang = atan(vModel.y, vModel.x) / 6.2831853;
        vec2 g = vec2(ang * 600.0, vModel.z * 9.0);
        vec2 f = abs(fract(g) - 0.5);
        float pane = step(f.x, 0.3) * step(f.y, 0.2);
        float lit = step(0.3, hash1(vec3(floor(g), 3.0)));
        glow = vec3(1.0, 0.86, 0.6) * pane * lit * 1.3;
        base = vec3(0.3, 0.3, 0.32);
    } else if (mat == 34) {
        // A green navigation light, steady.
        glow = vec3(0.05, 0.9, 0.25) * 2.0;
        base = vec3(0.0, 0.3, 0.1);
    } else if (mat == 35) {
        // The docking collar's glow.
        glow = vec3(0.55, 0.8, 1.0) * 0.9;
        base = vec3(0.4, 0.45, 0.5);
    } else if (mat == 37) {
        // The station's hull: a darker grey than the ship's, so the ship shows
        // against it.
        base = vec3(0.42, 0.43, 0.46);
        shine = 0.7;
    } else if (mat == 36) {
        // Solar panels: deep blue cells in a fine silver grid.
        vec2 g = abs(fract(vec2(vModel.x, vModel.z) * 4.0) - 0.5);
        float frame = step(0.46, max(g.x, g.y));
        base = mix(vec3(0.04, 0.07, 0.2), vec3(0.6, 0.62, 0.66), frame);
        shine = 1.2;
    } else if (mat >= 20) {
        // The tail lights along each strut flash one after another from the
        // hull outwards, a sixteenth of a second each, once every 2 seconds.
        float light = float(mat - 20);
        float since = fract(uTime * 0.5) * 32.0 - light;
        float flash = since >= 0.0 ? exp(-since * 2.5) : 0.0;
        glow = vec3(0.75, 0.0, 0.0) * (0.3 + 9.0 * flash);
        base = vec3(0.3, 0.0, 0.0);
    } else if (mat >= 10) {
        // The pod rings flash one after another from front to back, each
        // fading as the next lights, then a pause before the next run.
        float ring = float(mat - 10);
        float since = fract(uTime * 0.5) * 32.0 - ring;  // a run every 2 seconds, a sixteenth of a second per ring
        float flash = since >= 0.0 ? exp(-since * 2.5) : 0.0;
        glow = vec3(0.35, 0.7, 1.0) * (0.12 + flash * (3.5 + 3.5 * uEngine));
        base *= 0.3;
    } else if (mat == 6) {
        // The beacon on top of the saucer and the tail lights: a short bright
        // red flash each second.
        float since = fract(uTime * 0.95);
        glow = vec3(0.75, 0.0, 0.0) * (0.3 + 9.0 * exp(-since * 7.0));
        base = vec3(0.3, 0.0, 0.0);
    } else if (mat == 3) {
        float ang = atan(vModel.x, vModel.z - 0.2);
        float w = step(0.7, fract(ang * 24.0 / 6.2831853)) * step(abs(vModel.y + 0.002), 0.008);
        glow = vec3(1.0, 0.88, 0.65) * w * 1.4;
        base *= 0.55;
    } else if (mat == 4) {
        base = vec3(0.4, 0.42, 0.46);
        shine = 0.6;
    }
    base *= mix(0.82, 1.0, panels(vModel));
    // The registry, in big dark letters arcing round the forward part of
    // the saucer, facing outwards: their feet towards the rim, reading left
    // to right from beyond the bow, looking in.
    if (mat == 0 && vModel.y > 0.03) {
        float r = length(vModel.xz - vec2(0.0, 0.2));
        float ang = atan(vModel.x, vModel.z - 0.2);
        const float r0 = 0.195, r1 = 0.29, span = 1.6;
        if (r > r0 && r < r1 && abs(ang) < span * 0.5) {
            vec2 uv = vec2(0.5 - ang / span, (r - r0) / (r1 - r0));
            float ink = texture(uLetters, uv).r;
            base = mix(base, vec3(0.14, 0.14, 0.16), ink * 0.9);
        }
    }
    // A ring of windows round the side of the raised dome on top.
    if (mat == 0 && vModel.y > 0.03) {
        float r = length(vModel.xz - vec2(0.0, 0.2));
        if (r < 0.12 && abs(vModel.y - 0.113) < 0.006) {
            float ang = atan(vModel.x, vModel.z - 0.2);
            float w = step(0.6, fract(ang * 24.0 / 6.2831853));
            glow += vec3(1.0, 0.88, 0.65) * w * 1.3;
            base *= 1.0 - 0.45 * w;
        }
    }

    float diff = max(dot(n, uLightDir), 0.0);
    vec3 h = normalize(uLightDir + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.5 * shine;
    float fres = pow(clamp(1.0 - dot(n, v), 0.0, 1.0), 4.0);
    vec3 r = reflect(-v, n);
    vec3 env = mix(vec3(0.02, 0.025, 0.04), vec3(0.22, 0.24, 0.3), smoothstep(-0.3, 0.8, r.y));
    vec3 col = base * (uLight * diff * 0.5 + vec3(0.035, 0.04, 0.05))
             + uLight * spec + env * (0.15 + 0.5 * fres) * shine + glow;
    gl_FragColor = vec4(col, 1.0);
}
)";

// Shrinks the ship's target into the stars layer: one linear sample sits
// between four of its pixels, so averages them.
static const char* kShipDownFS = R"(#version 130
uniform sampler2D uTex;
uniform vec2 uRes;
void main() { gl_FragColor = texture(uTex, gl_FragCoord.xy / uRes); }
)";

// ---------------------------------------------------------------------------
// The model

struct ShipVertex {
    float p[3], n[3], m;
};

static std::vector<ShipVertex> g_shipMesh, g_buoyMesh;
static std::vector<ShipVertex>* g_meshOut = &g_shipMesh;  // where ShipTri adds triangles

static void ShipTri(const float* a, const float* b, const float* c, const float* na, const float* nb, const float* nc, float m)
{
    g_meshOut->push_back({{a[0], a[1], a[2]}, {na[0], na[1], na[2]}, m});
    g_meshOut->push_back({{b[0], b[1], b[2]}, {nb[0], nb[1], nb[2]}, m});
    g_meshOut->push_back({{c[0], c[1], c[2]}, {nc[0], nc[1], nc[2]}, m});
}

// A flat-shaded quad, corners in order round its edge.
static void ShipQuad(const float* a, const float* b, const float* c, const float* d, float m)
{
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {d[0] - a[0], d[1] - a[1], d[2] - a[2]}, n[3];
    Flight::Cross(e1, e2, n);
    Flight::Normalize(n);
    ShipTri(a, b, c, n, n, n, m);
    ShipTri(a, c, d, n, n, n, m);
}

// A six-sided block from its eight corners: the four at its back, then the
// four at its front, each set going round the same way.
static void ShipBlock(const float (*c)[3], float m)
{
    ShipQuad(c[0], c[1], c[2], c[3], m);  // back
    ShipQuad(c[7], c[6], c[5], c[4], m);  // front
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        ShipQuad(c[j], c[i], c[i + 4], c[j + 4], m);
    }
}

// A surface turned about an axis parallel to y through (xc, zc): a profile of
// (radius, height) points, each span with its own material.
static void ShipLathe(const float (*prof)[2], const int* mats, int count, float zc, int sides, float xc = 0.0f)
{
    for (int i = 0; i + 1 < count; i++) {
        float dr = prof[i + 1][0] - prof[i][0], dy = prof[i + 1][1] - prof[i][1];
        float len = sqrtf(dr * dr + dy * dy);
        float nr = -dy / len, ny = dr / len;  // outward normal of the span
        for (int k = 0; k < sides; k++) {
            float a0 = 6.2831853f * k / sides, a1 = 6.2831853f * (k + 1) / sides;
            auto at = [&](int pi, float a, float* out) {
                out[0] = xc + prof[pi][0] * sinf(a);
                out[1] = prof[pi][1];
                out[2] = zc + prof[pi][0] * cosf(a);
            };
            auto nrm = [&](float a, float* out) {
                out[0] = nr * sinf(a);
                out[1] = ny;
                out[2] = nr * cosf(a);
            };
            float p00[3], p01[3], p10[3], p11[3], n0[3], n1[3];
            at(i, a0, p00);
            at(i, a1, p01);
            at(i + 1, a0, p10);
            at(i + 1, a1, p11);
            nrm(a0, n0);
            nrm(a1, n1);
            ShipTri(p00, p10, p11, n0, n0, n1, (float)mats[i]);
            ShipTri(p00, p11, p01, n0, n1, n1, (float)mats[i]);
        }
    }
}

// An engine pod along z, centred on (x, y), from its back at z0 to the tip
// of its nose cone.
static void ShipPod(float x, float y, float z0, float z1, float r)
{
    const int sides = 28;
    auto ring = [&](float za, float zb, float ra, float rb, float m) {
        for (int k = 0; k < sides; k++) {
            float a0 = 6.2831853f * k / sides, a1 = 6.2831853f * (k + 1) / sides;
            float slope = (ra - rb) / (zb - za);
            float nl = sqrtf(1 + slope * slope);
            float pa0[3] = {x + ra * cosf(a0), y + ra * sinf(a0), za}, pa1[3] = {x + ra * cosf(a1), y + ra * sinf(a1), za};
            float pb0[3] = {x + rb * cosf(a0), y + rb * sinf(a0), zb}, pb1[3] = {x + rb * cosf(a1), y + rb * sinf(a1), zb};
            float n0[3] = {cosf(a0) / nl, sinf(a0) / nl, slope / nl}, n1[3] = {cosf(a1) / nl, sinf(a1) / nl, slope / nl};
            ShipTri(pa0, pb0, pb1, n0, n0, n1, m);
            ShipTri(pa0, pb1, pa1, n0, n1, n1, m);
        }
    };
    const float len = z1 - z0, body = z0 + len * 0.82f;
    // The body, with five glowing rings round it.
    float z = z0;
    const float rings[kPodRings] = {0.15f, 0.29f, 0.43f, 0.57f, 0.71f};  // back to front
    for (int i = 0; i < kPodRings; i++) {
        float zr = z0 + len * rings[i];
        ring(z, zr, r, r, kHull);
        ring(zr, zr + 0.026f, r * 1.07f, r * 1.07f, (float)(kPodRing + kPodRings - 1 - i));
        ring(zr, zr, r, r * 1.07f, kHull);
        ring(zr + 0.026f, zr + 0.026f, r * 1.07f, r, kHull);
        z = zr + 0.026f;
    }
    ring(z, body, r, r, kHull);
    ring(body, z1, r, 0.0f, kDark);            // nose cone
    ring(z0 - 0.015f, z0, r * 0.8f, r, kDark);  // exhaust collar
    // The glowing exhaust across the back.
    float c[3] = {x, y, z0 - 0.015f}, back[3] = {0, 0, -1};
    for (int k = 0; k < sides; k++) {
        float a0 = 6.2831853f * k / sides, a1 = 6.2831853f * (k + 1) / sides;
        float p0[3] = {x + r * 0.8f * cosf(a0), y + r * 0.8f * sinf(a0), z0 - 0.015f};
        float p1[3] = {x + r * 0.8f * cosf(a1), y + r * 0.8f * sinf(a1), z0 - 0.015f};
        ShipTri(c, p1, p0, back, back, back, kExhaust);
    }
}

// A streamlined strut between two airfoil sections (x, y, front z, back z),
// thicker at the root than the tip, with smooth normals.
static void ShipStrut(const float* root, const float* tip, float rootThick, float tipThick)
{
    const int spans = 10, around = 20;
    // Thickness runs at right angles to the span, in the x-y plane.
    float sx = tip[0] - root[0], sy = tip[1] - root[1], sl = sqrtf(sx * sx + sy * sy);
    float tx = -sy / sl, ty = sx / sl;
    auto at = [&](int i, int j, float* p, float* n) {
        float f = (float)i / spans, a = 6.2831853f * j / around;
        float cx = root[0] + (tip[0] - root[0]) * f, cy = root[1] + (tip[1] - root[1]) * f;
        float zf = root[2] + (tip[2] - root[2]) * f, zb = root[3] + (tip[3] - root[3]) * f;
        float cz = (zf + zb) * 0.5f, half = (zf - zb) * 0.5f;
        float thick = rootThick + (tipThick - rootThick) * f;
        // An airfoil: blunt at the front, fine at the back.
        float c = cosf(a), sn = sinf(a);
        float t = thick * sn * (c > 0 ? 1.0f : 0.55f + 0.45f * (1 + c));
        p[0] = cx + tx * t;
        p[1] = cy + ty * t;
        p[2] = cz + half * c;
        float nx = c / half, nt = sn / thick;
        n[0] = tx * nt;
        n[1] = ty * nt;
        n[2] = nx;
        Flight::Normalize(n);
    };
    for (int i = 0; i < spans; i++)
        for (int j = 0; j < around; j++) {
            float p00[3], p01[3], p10[3], p11[3], n00[3], n01[3], n10[3], n11[3];
            at(i, j, p00, n00);
            at(i, j + 1, p01, n01);
            at(i + 1, j, p10, n10);
            at(i + 1, j + 1, p11, n11);
            ShipTri(p00, p10, p11, n00, n10, n11, kHull);
            ShipTri(p00, p11, p01, n00, n11, n01, kHull);
        }
}

static void BuildShip()
{
    if (!g_shipMesh.empty()) return;

    // The saucer, centred a little forward: a raised dome, a broad shallow
    // top, a rim band of windows and a gently curved underside.
    const float saucer[][2] = {
        {0.0f, 0.15f}, {0.05f, 0.147f}, {0.09f, 0.128f}, {0.115f, 0.098f}, {0.2f, 0.074f},
        {0.3f, 0.046f}, {0.355f, 0.024f}, {0.37f, 0.014f}, {0.37f, -0.018f}, {0.35f, -0.034f},
        {0.25f, -0.06f}, {0.12f, -0.08f}, {0.05f, -0.087f}, {0.0f, -0.089f},
    };
    const int saucerMats[] = {kHull, kHull, kHull, kHull, kHull, kHull, kHull, kWindows,
                              kHull, kHull, kHull, kDark, kDark};
    ShipLathe(saucer, saucerMats, sizeof(saucer) / sizeof(saucer[0]), 0.2f, 72);

    // A small red beacon on top of the dome.
    const float beacon[][2] = {{0.0f, 0.157f}, {0.0035f, 0.1555f}, {0.0055f, 0.152f}, {0.006f, 0.146f}};
    const int beaconMats[] = {kBeacon, kBeacon, kBeacon};
    ShipLathe(beacon, beaconMats, 4, 0.2f, 16);


    // The delta spine, tapering aft from beneath the saucer.
    const float spine[8][3] = {
        {-0.04f, -0.03f, -0.5f}, {0.04f, -0.03f, -0.5f}, {0.04f, 0.0f, -0.5f}, {-0.04f, 0.0f, -0.5f},
        {-0.13f, -0.045f, 0.12f}, {0.13f, -0.045f, 0.12f}, {0.13f, 0.02f, 0.12f}, {-0.13f, 0.02f, 0.12f},
    };
    ShipBlock(spine, kHull);
    // A low dorsal ridge along it.
    const float ridge[8][3] = {
        {-0.012f, 0.0f, -0.45f}, {0.012f, 0.0f, -0.45f}, {0.008f, 0.03f, -0.4f}, {-0.008f, 0.03f, -0.4f},
        {-0.03f, 0.02f, 0.02f}, {0.03f, 0.02f, 0.02f}, {0.02f, 0.045f, -0.02f}, {-0.02f, 0.045f, -0.02f},
    };
    ShipBlock(ridge, kHull);

    // Broad swept wings rising from the spine to the pods, and the pods.
    const float podX = kPodX, podY = kPodY, podR = kPodR;
    for (int side = -1; side <= 1; side += 2) {
        float s = (float)side;
        // Root: along the spine, from just behind the saucer to its tail.
        // Tip: buried in the pod. Each is an airfoil, rounded all round.
        const float root[4] = {s * 0.03f, -0.012f, 0.05f, -0.5f};    // x, y, front z, back z: inside the spine
        const float tip[4] = {s * (podX - 0.015f), podY - 0.004f, -0.3f, -0.72f};
        ShipStrut(root, tip, 0.022f, 0.009f);
        // Small red tail lights along the strut's trailing edge, from the
        // hull out to the pod.
        for (int i = 0; i < kStrutLights; i++) {
            float f = 0.14f + 0.72f * i / (kStrutLights - 1);
            float x = root[0] + (tip[0] - root[0]) * f;
            float y = root[1] + (tip[1] - root[1]) * f;
            float z = root[3] + (tip[3] - root[3]) * f - 0.003f;
            const float r = 0.0045f;
            const float light[][2] = {{0.0f, y + r}, {r * 0.7f, y + r * 0.7f}, {r, y}, {r * 0.7f, y - r * 0.7f}, {0.0f, y - r}};
            const int mat = kStrutLight + i;
            const int mats[] = {mat, mat, mat, mat};
            ShipLathe(light, mats, 5, z, 12, x);
        }
        ShipPod(s * podX, podY, kPodBack, -0.12f, podR);
    }
}

// ---------------------------------------------------------------------------
// Drawing

// A guide buoy: a dark spindle with a six-sided waist covered in a grid of
// small lights, tapering to a red warning light at each end, with a thin
// collar flaring round its middle. It spins about its long (y) axis.
static void BuildBuoy()
{
    if (!g_buoyMesh.empty()) return;
    g_meshOut = &g_buoyMesh;
    const float r = 0.08f, h = 0.07f, tip = 0.18f, collar = 0.13f, lip = 0.008f;
    float ring[6][3];
    for (int i = 0; i < 6; i++) {
        float a = 6.2831853f * (i + 0.5f) / 6;
        ring[i][0] = r * sinf(a);
        ring[i][2] = r * cosf(a);
    }
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        float b0[3] = {ring[i][0], -h, ring[i][2]}, b1[3] = {ring[j][0], -h, ring[j][2]};
        float t0[3] = {ring[i][0], h, ring[i][2]}, t1[3] = {ring[j][0], h, ring[j][2]};
        ShipQuad(b0, b1, t1, t0, kBuoyGrid);  // a lit side panel
        float top[3] = {0, tip, 0}, bottom[3] = {0, -tip, 0};
        float e1[3] = {t1[0] - t0[0], t1[1] - t0[1], t1[2] - t0[2]}, e2[3] = {top[0] - t0[0], top[1] - t0[1], top[2] - t0[2]}, n[3];
        Flight::Cross(e1, e2, n);
        Flight::Normalize(n);
        ShipTri(t0, t1, top, n, n, n, kBuoyHull);
        float f1[3] = {b0[0] - b1[0], b0[1] - b1[1], b0[2] - b1[2]}, f2[3] = {bottom[0] - b1[0], bottom[1] - b1[1], bottom[2] - b1[2]};
        Flight::Cross(f1, f2, n);
        Flight::Normalize(n);
        ShipTri(b1, b0, bottom, n, n, n, kBuoyHull);
        // The collar round the waist: top, rim and underside.
        float ci0[3] = {ring[i][0], lip, ring[i][2]}, ci1[3] = {ring[j][0], lip, ring[j][2]};
        float co0[3] = {ring[i][0] * collar / r, lip, ring[i][2] * collar / r}, co1[3] = {ring[j][0] * collar / r, lip, ring[j][2] * collar / r};
        ShipQuad(ci0, co0, co1, ci1, kBuoyHull);
        float cu0[3] = {co0[0], -lip, co0[2]}, cu1[3] = {co1[0], -lip, co1[2]};
        ShipQuad(co0, cu0, cu1, co1, kBuoyHull);
        float di0[3] = {ring[i][0], -lip, ring[i][2]}, di1[3] = {ring[j][0], -lip, ring[j][2]};
        ShipQuad(di1, cu1, cu0, di0, kBuoyHull);
    }
    // A red warning light at each end.
    const float light[][2] = {{0.0f, 0.016f}, {0.011f, 0.011f}, {0.016f, 0.0f}, {0.011f, -0.011f}, {0.0f, -0.016f}};
    float lp[5][2];
    const int mats[] = {kBuoyLight, kBuoyLight, kBuoyLight, kBuoyLight};
    for (float end : {tip + 0.006f, -tip - 0.006f}) {
        for (int i = 0; i < 5; i++) {
            lp[i][0] = light[i][0];
            lp[i][1] = light[i][1] + end;
        }
        ShipLathe(lp, mats, 5, 0.0f, 12);
    }
    g_meshOut = &g_shipMesh;
}

// Draws the registry into a texture, white on black, filling it edge to edge.
static GLuint MakeLetters()
{
    const int w = 1024, hgt = 160;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hgt;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT all = {0, 0, w, hgt};
    FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
    HFONT font = CreateFontW(-(int)(hgt * 1.25f), 0, 0, 0, FW_BLACK, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial Black");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    // Squeeze the text to fit the width; centre it.
    SIZE size;
    GetTextExtentPoint32W(dc, kRegistry, (int)wcslen(kRegistry), &size);
    XFORM squeeze = {std::min(1.0f, w * 0.96f / size.cx), 0, 0, 1, 0, 0};
    SetGraphicsMode(dc, GM_ADVANCED);
    SetWorldTransform(dc, &squeeze);
    RECT box = {0, -(int)(hgt * 0.22f), (LONG)(w / squeeze.eM11), hgt};
    DrawTextW(dc, kRegistry, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    GdiFlush();

    std::vector<uint8_t> rgba((size_t)w * hgt * 4);
    const uint8_t* px = (const uint8_t*)bits;
    for (int i = 0; i < w * hgt; i++) {
        uint8_t v = px[i * 4 + 1];
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = 255;
    }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, hgt, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

// A tube along z, centred on (x, y), from z0 to z1.
static void StationTube(float x, float y, float z0, float z1, float r, int mat)
{
    const int sides = 20;
    for (int k = 0; k < sides; k++) {
        float a0 = 6.2831853f * k / sides, a1 = 6.2831853f * (k + 1) / sides;
        float p00[3] = {x + r * cosf(a0), y + r * sinf(a0), z0}, p01[3] = {x + r * cosf(a1), y + r * sinf(a1), z0};
        float p10[3] = {x + r * cosf(a0), y + r * sinf(a0), z1}, p11[3] = {x + r * cosf(a1), y + r * sinf(a1), z1};
        float n0[3] = {cosf(a0), sinf(a0), 0}, n1[3] = {cosf(a1), sinf(a1), 0};
        ShipTri(p00, p10, p11, n0, n0, n1, (float)mat);
        ShipTri(p00, p11, p01, n0, n1, n1, (float)mat);
    }
}

// A tube between two points, with caps at its ends.
static void StationRod(const float* p0, const float* p1, float r, int mat)
{
    float ax[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, u[3], v[3], any[3] = {0.31f, 0.93f, 0.17f};
    Flight::Normalize(ax);
    Flight::Cross(ax, any, u);
    Flight::Normalize(u);
    Flight::Cross(ax, u, v);
    const int sides = 12;
    for (int k = 0; k < sides; k++) {
        float a0 = 6.2831853f * k / sides, a1 = 6.2831853f * (k + 1) / sides;
        float n0[3], n1[3], q00[3], q01[3], q10[3], q11[3];
        for (int i = 0; i < 3; i++) {
            n0[i] = u[i] * cosf(a0) + v[i] * sinf(a0);
            n1[i] = u[i] * cosf(a1) + v[i] * sinf(a1);
            q00[i] = p0[i] + n0[i] * r;
            q01[i] = p0[i] + n1[i] * r;
            q10[i] = p1[i] + n0[i] * r;
            q11[i] = p1[i] + n1[i] * r;
        }
        ShipTri(q00, q10, q11, n0, n0, n1, (float)mat);
        ShipTri(q00, q11, q01, n0, n1, n1, (float)mat);
    }
}

// The space station: an original design. A single great wheel, some twenty
// times the ship's length across, turns slowly on six spokes about a hub
// that stays still, so ships can dock at it. The wheel's outer face is
// banded with lit windows, with red and green navigation lights round it;
// the hub has a glowing docking collar on its front face, and an antenna
// and red beacon behind. In its own units (space units) the wheel lies in
// the x-y plane, its axis along z, the docking side towards +z.
static const float kWheelRadius = 9.0f, kWheelTube = 0.85f;
static std::vector<ShipVertex> g_hubMesh, g_wheelMesh;

static void BuildStation()
{
    if (!g_hubMesh.empty()) return;

    // The hub: a drum along the axis, with a collar on its docking face.
    g_meshOut = &g_hubMesh;
    {
        const float drum[][2] = {{0.0f, 1.9f}, {1.2f, 1.85f}, {1.45f, 1.6f}, {1.45f, 0.6f}, {1.45f, -0.6f},
                                 {1.45f, -1.6f}, {1.2f, -1.85f}, {0.0f, -1.9f}};
        const int mats[] = {kStationHull, kStationHull, kStationHull, kStationWindows, kStationHull, kStationHull, kStationHull};
        // Lathed about y; turned to lie along z below.
        size_t start = g_meshOut->size();
        ShipLathe(drum, mats, 8, 0.0f, 40);
        for (size_t i = start; i < g_meshOut->size(); i++) {
            ShipVertex& vx = (*g_meshOut)[i];
            float y = vx.p[1], z = vx.p[2], ny = vx.n[1], nz = vx.n[2];
            vx.p[1] = -z;
            vx.p[2] = y;
            vx.n[1] = -nz;
            vx.n[2] = ny;
        }
    }
    StationTube(0.0f, 0.0f, 1.9f, kHubPort - 0.05f, 0.42f, kStationHull);
    StationTube(0.0f, 0.0f, kHubPort - 0.05f, kHubPort, 0.5f, kDockLight);
    {
        const float m0[3] = {0, 0, -1.9f}, m1[3] = {0, 0, -4.2f};
        StationRod(m0, m1, 0.05f, kStationHull);
        const float beacon[][2] = {{0.0f, 0.12f}, {0.09f, 0.08f}, {0.12f, 0.0f}, {0.09f, -0.08f}, {0.0f, -0.12f}};
        const int bm[] = {kBeacon, kBeacon, kBeacon, kBeacon};
        float lp[5][2];
        for (int i = 0; i < 5; i++) {
            lp[i][0] = beacon[i][0];
            lp[i][1] = beacon[i][1];
        }
        ShipLathe(lp, bm, 5, -4.3f, 12);
    }

    // The wheel: its rim, a tube bent round the axis, and six spokes.
    g_meshOut = &g_wheelMesh;
    {
        const int around = 120, round = 16;
        for (int i = 0; i < around; i++)
            for (int j = 0; j < round; j++) {
                auto at = [&](int ii, int jj, float* p, float* n) {
                    float a = 6.2831853f * ii / around, b = 6.2831853f * jj / round;
                    float cx = cosf(a), cy = sinf(a);
                    n[0] = cx * cosf(b);
                    n[1] = cy * cosf(b);
                    n[2] = sinf(b);
                    p[0] = cx * kWheelRadius + n[0] * kWheelTube;
                    p[1] = cy * kWheelRadius + n[1] * kWheelTube;
                    p[2] = n[2] * kWheelTube;
                };
                float p00[3], p01[3], p10[3], p11[3], n00[3], n01[3], n10[3], n11[3];
                at(i, j, p00, n00);
                at(i + 1, j, p10, n10);
                at(i, j + 1, p01, n01);
                at(i + 1, j + 1, p11, n11);
                // Windows round the outer face; plain hull elsewhere.
                float b = 6.2831853f * (j + 0.5f) / round;
                int mat = cosf(b) > 0.55f ? kStationWindows : kStationHull;
                ShipTri(p00, p10, p11, n00, n10, n11, (float)mat);
                ShipTri(p00, p11, p01, n00, n11, n01, (float)mat);
            }
    }
    for (int k = 0; k < 6; k++) {
        float a = 6.2831853f * k / 6;
        const float p0[3] = {cosf(a) * 1.4f, sinf(a) * 1.4f, 0}, p1[3] = {cosf(a) * (kWheelRadius - 0.6f), sinf(a) * (kWheelRadius - 0.6f), 0};
        StationRod(p0, p1, 0.22f, kStationHull);
    }
    // Navigation lights round the rim: red on one side of the wheel, green on the other.
    const float nav[][2] = {{0.0f, 0.12f}, {0.09f, 0.08f}, {0.12f, 0.0f}, {0.09f, -0.08f}, {0.0f, -0.12f}};
    for (int k = 0; k < 8; k++) {
        float a = 6.2831853f * (k + 0.5f) / 8;
        float lp[5][2];
        for (int i = 0; i < 5; i++) {
            lp[i][0] = nav[i][0];
            lp[i][1] = nav[i][1] + sinf(a) * (kWheelRadius + kWheelTube);
        }
        int m = k < 4 ? kBeacon : kNavGreen;
        const int mats[] = {m, m, m, m};
        ShipLathe(lp, mats, 5, 0.0f, 10, cosf(a) * (kWheelRadius + kWheelTube));
    }
    g_meshOut = &g_shipMesh;
}

static void InitShip(Scene& sc)
{
    sc.shipLetters = MakeLetters();
    BuildShip();
    BuildBuoy();
    BuildStation();
    int k = sc.W * 2 <= 4096 && sc.H * 2 <= 4096 ? 2 : 1;
    sc.ship = MakeTarget(sc.W * k, sc.H * k, GL_RGBA16F, false);
    GLuint rb = 0;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, sc.ship.w, sc.ship.h);
    glBindFramebuffer(GL_FRAMEBUFFER, sc.ship.fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Fail("Framebuffer incomplete");
}

// Draws the ship over everything in the stars layer (which must be bound,
// with additive blending on).
static void DrawShip(Scene& sc)
{
    const Flight& fl = g_flight;

    // Its axes. Flying free it points the way it flies, upright, tipped back
    // so the camera above and behind sees its back, and leans into turns.
    const float free = 1.0f - fl.shipOrbiting;
    const float tilt = kShipTilt * free * (1.0f - fl.shipDocked);  // level while docked
    float fwd[3] = {fl.shipFwd[0], fl.shipFwd[1] + tilt, fl.shipFwd[2]};
    float up[3] = {fl.shipUp[0], fl.shipUp[1], fl.shipUp[2]}, right[3];
    // In orbit its top faces straight away from the planet's centre and its
    // bow points along its path round the planet.
    if (fl.planet.on && fl.shipOrbiting > 0.0f) {
        // Measured where the ship really is in its orbit, rather than at the
        // spot ahead of the camera it is drawn when the camera is with it.
        float out[3] = {-fl.planet.x, -fl.planet.y, -fl.planet.z};
        float vel[3] = {fl.velX, fl.velY, fl.velZ};
        if (fl.phase == Flight::Orbit)
            for (int i = 0; i < 3; i++) {
                out[i] = fl.shipRadial[i];
                vel[i] = fl.shipTangent[i];
            }
        Flight::Normalize(out);
        float radial = vel[0] * out[0] + vel[1] * out[1] + vel[2] * out[2];
        float along[3] = {vel[0] - out[0] * radial, vel[1] - out[1] * radial, vel[2] - out[2] * radial};
        if (along[0] * along[0] + along[1] * along[1] + along[2] * along[2] > 1e-8f) {
            Flight::Normalize(along);
            Flight::Normalize(fwd);
            const float w = fl.shipOrbiting;
            for (int i = 0; i < 3; i++) {
                fwd[i] += (along[i] - fwd[i]) * w;
                up[i] += (out[i] - up[i]) * w;
            }
        }
    }
    Flight::Normalize(fwd);
    Flight::Cross(up, fwd, right);
    Flight::Normalize(right);
    Flight::Cross(fwd, right, up);
    const float bank = fl.shipBank * free * (1.0f - fl.shipDocked);
    float cb = cosf(bank), sb = sinf(bank);
    float r2[3], u2[3];
    for (int i = 0; i < 3; i++) {
        r2[i] = right[i] * cb + up[i] * sb;
        u2[i] = up[i] * cb - right[i] * sb;
    }
    // A gentle drift about its place.
    float t = (float)fl.t;
    float at[3] = {kShipAt[0] + 0.02f * sinf(t * 0.43f), kShipAt[1] + 0.012f * sinf(t * 0.71f + 1.0f), kShipAt[2]};
    // In orbit the ship turns with the view (see uRoll); turn its place the
    // other way first, so it stays at the same spot on the screen.
    const float viewRoll = fl.roll * fl.shipOrbiting, vc = cosf(-viewRoll), vs = sinf(-viewRoll);
    const float ax = at[0], ay = at[1];
    at[0] = ax * vc - ay * vs;
    at[1] = ax * vs + ay * vc;
    // With the camera overhead in orbit, the ship is where it really is,
    // straight below the camera.
    for (int i = 0; i < 3; i++) at[i] += (fl.shipPos[i] - at[i]) * fl.wide;
    const float below = sqrtf(fl.shipPos[0] * fl.shipPos[0] + fl.shipPos[1] * fl.shipPos[1] + fl.shipPos[2] * fl.shipPos[2]);
    const float distantScale = fl.phase == Flight::Orbit ? kShipOverhead * below : kShipScale;
    const float scale = kShipScale + (distantScale - kShipScale) * fl.wide;

    // Lit by the star it has been captured by, or else dimly, by deep space
    // (worked out in Flight::Update).
    const float* key = fl.shipKey;
    const float* light = fl.shipLight;

    const float cx = sc.W * 0.5f + fl.headingX * sc.H, cy = sc.H * 0.5f + fl.headingY * sc.H;  // y up
    const float f = kFocal * sc.H;

    BindTarget(sc.ship);
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    GLuint p = sc.progShip;
    glUseProgram(p);
    glUniform3f(glGetUniformLocation(p, "uRight"), r2[0], r2[1], r2[2]);
    glUniform3f(glGetUniformLocation(p, "uUp"), u2[0], u2[1], u2[2]);
    glUniform3f(glGetUniformLocation(p, "uFwd"), fwd[0], fwd[1], fwd[2]);
    glUniform3f(glGetUniformLocation(p, "uAt"), at[0], at[1], at[2]);
    glUniform1f(glGetUniformLocation(p, "uScale"), scale);
    glUniform1f(glGetUniformLocation(p, "uRoll"), viewRoll);
    glUniform2f(glGetUniformLocation(p, "uFocal"), 2.0f * f / sc.W, 2.0f * f / sc.H);
    glUniform2f(glGetUniformLocation(p, "uShift"), 2.0f * cx / sc.W - 1.0f, 2.0f * cy / sc.H - 1.0f);
    glUniform3f(glGetUniformLocation(p, "uLightDir"), key[0], key[1], key[2]);
    glUniform3f(glGetUniformLocation(p, "uLight"), light[0], light[1], light[2]);
    glUniform1f(glGetUniformLocation(p, "uEngine"), std::min(1.0f, 0.15f + 0.85f * fl.warp + fl.flash * 3.0f));
    glUniform1f(glGetUniformLocation(p, "uTime"), t);
    BindTexture(1, sc.shipLetters);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(p, "uLetters"), 1);
    const ShipVertex* v = g_shipMesh.data();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), v->p);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), v->n);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), &v->m);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_shipMesh.size());

    // The space station, if it is still about: its hub still, its wheel
    // turning. It is not tied to the ship, so it turns with the view.
    if (fl.station.on) {
        const auto& st = fl.station;
        glUniform3f(glGetUniformLocation(p, "uAt"), st.x, st.y, st.z);
        glUniform1f(glGetUniformLocation(p, "uScale"), 1.0f);
        glUniform1f(glGetUniformLocation(p, "uRoll"), fl.roll);
        glUniform3f(glGetUniformLocation(p, "uFwd"), st.axis[2][0], st.axis[2][1], st.axis[2][2]);
        for (int part = 0; part < 2; part++) {
            const std::vector<ShipVertex>& mesh = part == 0 ? g_hubMesh : g_wheelMesh;
            float spin = part == 0 ? 0.0f : st.spin, c = cosf(spin), sn = sinf(spin);
            float rx[3], ry[3];
            for (int i = 0; i < 3; i++) {
                rx[i] = st.axis[0][i] * c + st.axis[1][i] * sn;
                ry[i] = st.axis[1][i] * c - st.axis[0][i] * sn;
            }
            glUniform3f(glGetUniformLocation(p, "uRight"), rx[0], rx[1], rx[2]);
            glUniform3f(glGetUniformLocation(p, "uUp"), ry[0], ry[1], ry[2]);
            const ShipVertex* sv = mesh.data();
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), sv->p);
            glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), sv->n);
            glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), &sv->m);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh.size());
        }
        glUniform1f(glGetUniformLocation(p, "uRoll"), viewRoll);
        glUniform1f(glGetUniformLocation(p, "uScale"), scale);
    }

    // Guide buoys: flying in from ahead to flank the ship, spinning, then
    // left behind where they were when it jumps.
    if (fl.buoys.on) {
        auto& buoys = g_flight.buoys;
        float in = std::min(buoys.time / 5.0f, 1.0f);
        in = in * in * (3 - 2 * in);
        const ShipVertex* bv = g_buoyMesh.data();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), bv->p);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), bv->n);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), &bv->m);
        for (int k = 0; k < 2; k++) {
            float side = k == 0 ? -1.0f : 1.0f;
            if (buoys.escorting) {
                // Off each side of the ship, a little above; streaking in
                // from far ahead at first.
                float sx = side * (0.9f + 2.6f * (1 - in)), sy = 0.06f + 0.9f * (1 - in), sz = 0.05f + 5.0f * (1 - in);
                for (int i = 0; i < 3; i++) buoys.pos[k][i] = at[i] + (r2[i] * sx + u2[i] * sy + fwd[i] * sz) * scale;
            }
            float spin = buoys.time * 1.8f * side, cs = cosf(spin), sn = sinf(spin);
            float br[3], bf[3];
            for (int i = 0; i < 3; i++) {
                br[i] = r2[i] * cs + fwd[i] * sn;
                bf[i] = fwd[i] * cs - r2[i] * sn;
            }
            glUniform3f(glGetUniformLocation(p, "uRight"), br[0], br[1], br[2]);
            glUniform3f(glGetUniformLocation(p, "uUp"), u2[0], u2[1], u2[2]);
            glUniform3f(glGetUniformLocation(p, "uFwd"), bf[0], bf[1], bf[2]);
            glUniform3f(glGetUniformLocation(p, "uAt"), buoys.pos[k][0], buoys.pos[k][1], buoys.pos[k][2]);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_buoyMesh.size());
        }
    }

    // Ion trails streaming back from the exhausts: ribbons turned to face the
    // camera, longer and brighter the harder the engines work. Their
    // positions are worked out here, seen from the camera, so the ship's
    // own placement is switched off while they are drawn.
    const float engine = std::min(1.0f, 0.15f + 0.85f * fl.warp + fl.flash * 3.0f);
    std::vector<ShipVertex> trail;
    // A trail streaming back from m (model units): length and widths in space
    // units; steady trails keep the same faint glow whatever the engines do.
    auto addTrail = [&](const float* m, float length, float widthStart, float widthEnd, bool steady) {
        float a[3], b[3];
        for (int i = 0; i < 3; i++) {
            a[i] = at[i] + (r2[i] * m[0] + u2[i] * m[1] + fwd[i] * m[2]) * scale;
            b[i] = a[i] - fwd[i] * length;
        }
        // Across the ribbon: at right angles to it and to the line of sight.
        auto across = [&](const float* p, float halfWidth, float* out) {
            float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {-p[0], -p[1], -p[2]};
            Flight::Cross(d, v, out);
            Flight::Normalize(out);
            for (int i = 0; i < 3; i++) out[i] *= halfWidth;
        };
        float wa[3], wb[3];
        across(a, widthStart, wa);
        across(b, widthEnd, wb);
        const float s = steady ? 1.0f : 0.0f;
        ShipVertex v0 = {{a[0] - wa[0], a[1] - wa[1], a[2] - wa[2]}, {0, -1, s}, kTrail};
        ShipVertex v1 = {{a[0] + wa[0], a[1] + wa[1], a[2] + wa[2]}, {0, 1, s}, kTrail};
        ShipVertex v2 = {{b[0] + wb[0], b[1] + wb[1], b[2] + wb[2]}, {1, 1, s}, kTrail};
        ShipVertex v3 = {{b[0] - wb[0], b[1] - wb[1], b[2] - wb[2]}, {1, -1, s}, kTrail};
        trail.insert(trail.end(), {v0, v1, v2, v0, v2, v3});
    };
    // From the engine exhausts: longer and brighter the harder they work.
    const float length = (0.5f + 2.5f * engine) * scale;
    for (int side = -1; side <= 1; side += 2) {
        const float m[3] = {side * kPodX, kPodY, kPodBack - 0.015f};
        addTrail(m, length, kPodR * scale * 0.7f, kPodR * scale * 1.4f, false);
    }
    // From the tip of the spine: faint and steady, warp or no warp.
    for (int side = -1; side <= 1; side += 2) {
        const float spineTip[3] = {side * 0.014f, -0.015f, -0.505f};
        addTrail(spineTip, 1.2f * scale, kPodR * scale * 0.32f, kPodR * scale * 0.65f, true);
    }
    glUniform3f(glGetUniformLocation(p, "uRight"), 1, 0, 0);
    glUniform3f(glGetUniformLocation(p, "uUp"), 0, 1, 0);
    glUniform3f(glGetUniformLocation(p, "uFwd"), 0, 0, 1);
    glUniform3f(glGetUniformLocation(p, "uAt"), 0, 0, 0);
    glUniform1f(glGetUniformLocation(p, "uScale"), 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    const ShipVertex* tv = trail.data();
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), tv->p);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), tv->n);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ShipVertex), &tv->m);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)trail.size());
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glDisable(GL_DEPTH_TEST);

    // Into the stars layer, over everything already there.
    BindTarget(sc.stars);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(sc.progShipDown);
    BindTexture(0, sc.ship.tex);
    glUniform1i(glGetUniformLocation(sc.progShipDown, "uTex"), 0);
    glUniform2f(glGetUniformLocation(sc.progShipDown, "uRes"), (float)sc.W, (float)sc.H);
    DrawFullscreen();
    glBlendFunc(GL_ONE, GL_ONE);
}
