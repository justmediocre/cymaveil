#include "sleeve3d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "rlgl.h"

namespace {

constexpr int kRtSize = 768;
constexpr float kHalf = 0.5f;       // half the cover's side, world units
constexpr float kThick = 0.03f;     // sleeve thickness (edge reads even in a quick turn)
constexpr float kGloss = 0.95f;     // gloss highlight strength
constexpr float kFovy = 20.0f;      // camera field of view (mild perspective)
constexpr float kPop = 0.24f;       // forward lift toward the viewer mid-turn

// Light from the upper-left front; flat per-face diffuse is baked into vertex
// colour, so only the cover's gloss sweep needs the shader.
const Vector3 kLight = {-0.35f, 0.42f, 0.84f};

// Desktop GL. The cover gloss band slides with uTurn; uFacing gates it to the
// face actually pointing at the viewer (cosθ front, -cosθ back, 0 on edges).
const char* kVs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

const char* kFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float uTurn;
uniform float uFacing;
uniform float uGloss;
out vec4 finalColor;

void main() {
    vec4 tex = texture(texture0, fragTexCoord);
    vec3 col = tex.rgb*fragColor.rgb;
    vec2 p = fragTexCoord;

    float facing = clamp(uFacing, 0.0, 1.0);
    // A bright diagonal light streak sweeping across the cover as it turns,
    // plus a faint overall glaze so the facing side reads as a glossy laminate.
    // A hot squared core inside a wider soft halo sells the reflection.
    float band = p.x*0.6 + (1.0 - p.y)*0.4;
    float d = abs(band - uTurn);
    float streak = smoothstep(0.18, 0.0, d);
    float g = (streak*streak*uGloss + smoothstep(0.4, 0.0, d)*0.18 + 0.06)*facing;
    col += g;
    finalColor = vec4(col, tex.a*fragColor.a)*colDiffuse;
}
)";

float Dot(Vector3 a, Vector3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

Vector3 Normalize(Vector3 v) {
    const float m = std::sqrt(Dot(v, v));
    return m > 0 ? Vector3{v.x/m, v.y/m, v.z/m} : v;
}

// Worn cardboard grain: value noise + faint horizontal layer lines, baked once.
Texture2D GenPatina() {
    constexpr int N = 128;
    const auto hash = [](int x, int y) {
        uint32_t h = static_cast<uint32_t>(x)*374761393u + static_cast<uint32_t>(y)*668265263u;
        h = (h ^ (h >> 13))*1274126177u;
        return ((h ^ (h >> 16)) & 0xffff)/65535.0f;
    };
    Image img = GenImageColor(N, N, BLANK);
    auto* px = static_cast<Color*>(img.data);
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            const float grain = (hash(x, y) - 0.5f)*0.30f + (hash(x/3, y/3) - 0.5f)*0.18f;
            const float layer = 0.07f*std::sin(y*2.7f);
            const float v = std::clamp(0.80f + grain + layer, 0.0f, 1.0f);
            const auto g = static_cast<unsigned char>(v*255);
            px[y*N + x] = Color{g, g, g, 255};
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    return tex;
}

Color Shade(Color base, Vector3 normal, float ambient) {
    const float diff = std::max(0.0f, Dot(normal, kLight));
    const float s = ambient + (1.0f - ambient)*diff;
    return Color{static_cast<unsigned char>(base.r*s), static_cast<unsigned char>(base.g*s),
                 static_cast<unsigned char>(base.b*s), 255};
}

Color Lerp(Color a, Color b, float t) {
    return Color{static_cast<unsigned char>(a.r + (b.r - a.r)*t),
                 static_cast<unsigned char>(a.g + (b.g - a.g)*t),
                 static_cast<unsigned char>(a.b + (b.b - a.b)*t), 255};
}

// One flat face: 4 corners (local, pre-rotation), its UVs, and a baked diffuse
// colour. Culling is off, so winding doesn't matter. The caller sets the
// per-face uniforms (facing, wear) before this; we flush the batch after so
// those uniforms actually apply to this face rather than the last face drawn.
void Face(Texture2D tex, Color col, const Vector3 c[4], const Vector2 uv[4]) {
    rlSetTexture(tex.id);
    rlColor4ub(col.r, col.g, col.b, 255);
    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; i++) {
        rlTexCoord2f(uv[i].x, uv[i].y);
        rlVertex3f(c[i].x, c[i].y, c[i].z);
    }
    rlEnd();
    rlDrawRenderBatchActive();  // draw now so this face keeps its own uniforms
}

}  // namespace

void Sleeve3D::Ensure() {
    if (rt_.id == 0) rt_ = LoadRenderTexture(kRtSize, kRtSize);
    if (shader_.id == 0) {
        shader_ = LoadShaderFromMemory(kVs, kFs);
        locTurn_ = GetShaderLocation(shader_, "uTurn");
        locFacing_ = GetShaderLocation(shader_, "uFacing");
        locGloss_ = GetShaderLocation(shader_, "uGloss");
    }
    if (patina_.id == 0) patina_ = GenPatina();
}

void Sleeve3D::Render(const Texture2D* front, const Texture2D* back, Color edgeFrom, Color edgeTo,
                      float flip) {
    Ensure();

    const float theta = flip*PI;
    const float ct = std::cos(theta), st = std::sin(theta);
    // Face normals after the Y rotation, for baked diffuse + gloss facing.
    const Vector3 nFront = {st, 0, ct};
    const Vector3 nBack = {-st, 0, -ct};
    const Vector3 nRight = {ct, 0, -st};
    const Vector3 nLeft = {-ct, 0, st};
    const Vector3 nTop = {0, 1, 0};
    const Vector3 nBot = {0, -1, 0};

    // Bias the edge toward a worn-cardboard tan so the spine reads as a real
    // sleeve edge instead of sinking into the (already dark) album dominant.
    const Color kCardboard{128, 109, 84, 255};
    const Color edge = Lerp(Lerp(edgeFrom, edgeTo, flip), kCardboard, 0.5f);
    const float h = kThick*0.5f;

    // Pull the camera back so the face-on cover fills only kCoverFrac of the
    // target — that headroom lets the corners swing out under perspective (and
    // the forward pop) without the texture clipping them. The caller scales the
    // target back up so the face-on cover still lands on the art rect.
    Camera3D cam{};
    cam.position = {0, 0, kHalf / (Sleeve3D::kCoverFrac * std::tan(kFovy * 0.5f * DEG2RAD))};
    cam.target = {0, 0, 0};
    cam.up = {0, 1, 0};
    cam.fovy = kFovy;
    cam.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode(rt_);
    ClearBackground(BLANK);
    BeginMode3D(cam);
    rlDisableBackfaceCulling();
    BeginShaderMode(shader_);
    const float gloss = kGloss;
    SetShaderValue(shader_, locTurn_, &flip, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locGloss_, &gloss, SHADER_UNIFORM_FLOAT);

    rlPushMatrix();
    rlTranslatef(0, 0, kPop * st);  // lift toward the viewer, peaking edge-on
    rlRotatef(flip*180.0f, 0, 1, 0);

    // Square-crop the covers (matches the flat DrawAlbumArt), in normalised UVs.
    const auto crop = [](const Texture2D* t, float* u0, float* u1, float* v0, float* v1) {
        const float side = static_cast<float>(std::min(t->width, t->height));
        *u0 = (t->width - side)/2/t->width;
        *u1 = *u0 + side/t->width;
        *v0 = (t->height - side)/2/t->height;
        *v1 = *v0 + side/t->height;
    };

    if (front != nullptr) {
        float u0, u1, v0, v1;
        crop(front, &u0, &u1, &v0, &v1);
        const Vector3 c[4] = {
            {-kHalf, -kHalf, h}, {kHalf, -kHalf, h}, {kHalf, kHalf, h}, {-kHalf, kHalf, h}};
        const Vector2 uv[4] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
        SetShaderValue(shader_, locFacing_, &ct, SHADER_UNIFORM_FLOAT);
        Face(*front, Shade(WHITE, nFront, 0.62f), c, uv);
    }
    if (back != nullptr) {
        float u0, u1, v0, v1;
        crop(back, &u0, &u1, &v0, &v1);
        // Mirror U so the incoming cover reads upright once we turn past edge-on.
        const Vector3 c[4] = {
            {kHalf, -kHalf, -h}, {-kHalf, -kHalf, -h}, {-kHalf, kHalf, -h}, {kHalf, kHalf, -h}};
        const Vector2 uv[4] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
        const float facing = -ct;
        SetShaderValue(shader_, locFacing_, &facing, SHADER_UNIFORM_FLOAT);
        Face(*back, Shade(WHITE, nBack, 0.62f), c, uv);
    }

    // Patina edges: no gloss, darker ambient, cardboard grain.
    const float zero = 0.0f;
    SetShaderValue(shader_, locFacing_, &zero, SHADER_UNIFORM_FLOAT);
    const Vector2 euv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const Vector3 right[4] = {
        {kHalf, -kHalf, h}, {kHalf, -kHalf, -h}, {kHalf, kHalf, -h}, {kHalf, kHalf, h}};
    const Vector3 left[4] = {
        {-kHalf, -kHalf, -h}, {-kHalf, -kHalf, h}, {-kHalf, kHalf, h}, {-kHalf, kHalf, -h}};
    const Vector3 top[4] = {
        {-kHalf, kHalf, h}, {kHalf, kHalf, h}, {kHalf, kHalf, -h}, {-kHalf, kHalf, -h}};
    const Vector3 bot[4] = {
        {-kHalf, -kHalf, -h}, {kHalf, -kHalf, -h}, {kHalf, -kHalf, h}, {-kHalf, -kHalf, h}};
    Face(patina_, Shade(edge, nRight, 0.58f), right, euv);
    Face(patina_, Shade(edge, nLeft, 0.58f), left, euv);
    Face(patina_, Shade(edge, nTop, 0.58f), top, euv);
    Face(patina_, Shade(edge, nBot, 0.58f), bot, euv);

    rlPopMatrix();
    EndShaderMode();
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();
}

void Sleeve3D::Unload() {
    if (rt_.id != 0) UnloadRenderTexture(rt_);
    if (shader_.id != 0) UnloadShader(shader_);
    if (patina_.id != 0) UnloadTexture(patina_);
    rt_ = RenderTexture2D{};
    shader_ = Shader{};
    patina_ = Texture2D{};
}
