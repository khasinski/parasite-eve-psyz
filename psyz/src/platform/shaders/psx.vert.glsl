#version 450
// PSYZ_SHADER_REVISION 1
// The interface sdl3_gpu.c drives (its PSX_SHADER_REVISION): bump both when
// it changes, so stale generated headers fall back to the old behaviour.

layout(location = 0) in ivec2 pos;  // SDL_GPU_VERTEXELEMENTFORMAT_SHORT2
layout(location = 1) in uvec4 tex;  // SDL_GPU_VERTEXELEMENTFORMAT_USHORT4
layout(location = 2) in vec4 color; // SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM
layout(location = 3) in uvec4 twin; // SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4
layout(location = 4) in vec2 polygonUV;
layout(location = 5) in vec3 polygonRGB;
layout(location = 6) in vec3 pgxpPos;
layout(location = 7) in vec4 atlasBounds;
layout(location = 8) in vec3 hdCoordinates;
// exact PS1 polygon: triangle and integer attribute planes (sdl3_draw.h)
layout(location = 9) in uvec4 exactTriangle;
layout(location = 10) in uvec4 exactBase;
layout(location = 11) in uvec4 exactPerX;
layout(location = 12) in uvec4 exactPerY;
layout(location = 13) in uvec4 exactBlue;

// widescreen: {x squeeze, centre x before the draw-grid viewport,
//              sub-pixel shift x, y (game pixels)}
layout(set = 1, binding = 0) uniform UBO { vec4 drawOffset; vec4 widescreen; };

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec2 rawUV;
layout(location = 2) flat out uint tpage;
layout(location = 3) flat out uint clut;
// Pre-computed pixel shader parameters
layout(location = 4) flat out uint textureMode;  // 0=untextured, 1=16-bit, 2=indexed
layout(location = 5) flat out uint subPixelMask; // Sub-pixel mask (8-bit:1, 4-bit:3)
layout(location = 6) flat out uint texelShift;   // Right shift for texel X
layout(location = 7) flat out uint indexShift;   // Shift for index extraction
layout(location = 8) flat out uint indexMask;    // Mask for color index
layout(location = 9) flat out uint dither;      // 1 when this primitive dithers
layout(location = 10) flat out ivec2 pageBase;   // texture page origin, in VRAM pixels
layout(location = 11) flat out uvec4 texWindow;  // GP0(E2h) as {and.xy, or.zw}
layout(location = 12) flat out uint polygon;
layout(location = 13) flat out uint model;
layout(location = 14) flat out vec4 uvBounds;

layout(location = 15) out vec3 hdUV;
layout(location = 16) flat out uvec4 exTri;
layout(location = 17) flat out uvec4 exBase;
layout(location = 18) flat out uvec4 exPerX;
layout(location = 19) flat out uvec4 exPerY;
layout(location = 20) flat out uvec4 exBlue;

void main() {
    hdUV=hdCoordinates;
    exTri = exactTriangle;
    exBase = exactBase;
    exPerX = exactPerX;
    exPerY = exactPerY;
    exBlue = exactBlue;
    model = (tex.w & 0x1000u) != 0u ? 1u : 0u;
    uvBounds = atlasBounds;
    polygon = (tex.w & 0x2000u) != 0u ? 1u : 0u;
    // drawOffset.z is a bitfield: bit0=ordinary PGXP, bit1=midpoint position.
    bool usePgxp = polygon != 0u && (uint(drawOffset.z)&1u)!=0u && pgxpPos.z > 0.0;
    bool midpoint = (uint(drawOffset.z)&2u)!=0u && pgxpPos.z>0.0;
    // drawOffset.w: the target's scale (VRAM pixel -> target pixels).
    // The PS1 samples a polygon at integer coordinates, the GPU at pixel
    // centres: half a target pixel puts the first target pixel of every VRAM
    // pixel on the PS1 sample, so the NxN block's top-left pixel matches 1x.
    float scale = max(drawOffset.w, 1.0);
    // An exact polygon's bounding box lies on VRAM pixel edges.
    bool exact = exactTriangle.w != 0u;
    float bias = polygon != 0u && !exact ? 0.5 / scale : 0.0;
    float vx = (usePgxp || midpoint) ? pgxpPos.x : (float(pos.x) + bias);
    float vy = (usePgxp || midpoint) ? pgxpPos.y : (float(pos.y) + bias);
    float w = usePgxp ? pgxpPos.z : 1.0;
    float ax = vx + widescreen.z + drawOffset.x;
    // Squeezed before clipping, so drawing left of a buffer at VRAM x 0
    // still lands inside VRAM.
    if (widescreen.x != 1.0) {
        ax = widescreen.y + (ax - widescreen.y) * widescreen.x;
    }
    float x = (ax / (1024.0 / 2.0)) - 1.0;
    float y = ((vy + widescreen.w + drawOffset.y) / (512.0 / 2.0)) - 1.0;
    // SDL_GPU NDC y=-1 is the bottom while texture row 0 is the top; negate Y
    // so VRAM row 0 lands on texture row 0, like the GL FBO convention.
    gl_Position = vec4(x * w, -y * w, 0.0, w);
    // gouraud colors
    vertexColor = color;
    if (polygon != 0u) {
        vertexColor.rgb =
            (usePgxp || midpoint) ? (color.rgb + (0.5 / 255.0)) : (polygonRGB / 255.0);
    }
    // select the right texture coords based on the tpage
    clut = tex.z;
    uint texWord = tex.w;
    tpage = texWord & 0x1FFu;
    dither = (texWord & 0x4000u) != 0u ? 1u : 0u;
    rawUV = polygon != 0u
                ? ((usePgxp || midpoint) ? vec2(tex.xy) : polygonUV)
                : vec2(tex.xy);
    // Determine texture mode and pre-compute parameters
    subPixelMask = 0u;
    texelShift = 0u;
    indexShift = 0u;
    indexMask = 0u;
    if ((texWord & 0x8000u) != 0u) {
        textureMode = 0u; // untextured
    } else if ((tpage & 0x180u) >= 0x100u) {
        textureMode = 1u; // 16-bit direct
        vertexColor.rgb *= 2.0;
    } else {
        textureMode = 2u; // indexed
        vertexColor.rgb *= 2.0;
        if ((tpage & 0x80u) != 0u) { // 8-bit indexed
            subPixelMask = 1u;
            texelShift = 1u;
            indexShift = 8u;
            indexMask = 0xFFu;
        } else { // 4-bit indexed
            subPixelMask = 3u;
            texelShift = 2u;
            indexShift = 4u;
            indexMask = 0xFu;
        }
    }
    pageBase = ivec2(int((tpage % 32u) % 16u) * 64, int((tpage % 32u) / 16u) * 256);
    texWindow = twin;
}
