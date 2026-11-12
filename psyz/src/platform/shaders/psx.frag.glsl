#version 450
// PSYZ_SHADER_REVISION 1
// The interface sdl3_gpu.c drives (its PSX_SHADER_REVISION): bump both when
// it changes, so stale generated headers fall back to the old behaviour.

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 rawUV;
layout(location = 2) flat in uint tpage;
layout(location = 3) flat in uint clut;
layout(location = 4) flat in uint textureMode;
layout(location = 5) flat in uint subPixelMask;
layout(location = 6) flat in uint texelShift;
layout(location = 7) flat in uint indexShift;
layout(location = 8) flat in uint indexMask;
layout(location = 9) flat in uint dither;
layout(location = 10) flat in ivec2 pageBase;
layout(location = 11) flat in uvec4 texWindow;
layout(location = 12) flat in uint polygon;
layout(location = 13) flat in uint model;
layout(location = 14) flat in vec4 uvBounds;
layout(location = 15) in vec3 hdUV;
layout(location = 16) flat in uvec4 exTri;
layout(location = 17) flat in uvec4 exBase;
layout(location = 18) flat in uvec4 exPerX;
layout(location = 19) flat in uvec4 exPerY;
layout(location = 20) flat in uvec4 exBlue;

layout(set = 2, binding = 0) uniform sampler2D texVram;
layout(set = 2, binding = 1) uniform sampler2D texBlend;
layout(set = 2, binding = 2) uniform sampler2D texHD;
// The GPU's CLUT cache: one palette per row; exBlue.w is this primitive's.
layout(set = 2, binding = 3) uniform sampler2D texClut;
// passMode: x = pass (0 colour, 1 mask bit, 2 direct blend, 3 colour and
// mask bit under check mask), y = target scale, z = 1 without a horizontal
// draw grid, w = bit0 model filtering, bit1 high colour (no dithering or
// RGB555 shading), bit2 sprite filtering, bit3 set mask, bit4 check mask.
layout(set = 3, binding = 0) uniform PassMode { uvec4 passMode; };

float attributeRoundingBias() {
    // Native 1:1 polygon attributes occupy a 1/4096 lattice. Half an LSB
    // repairs floating interpolation just below an exact integer without
    // rounding the preceding representable value up to it.
    return polygon != 0u && passMode.z != 0u ? 1.0 / 8192.0 : 0.0;
}

// Exact PS1 polygon (exTri.w != 0, see sdl3_draw.h): U, V, R, G, B of this
// fragment's sample from the GPU's integer attribute planes.
bool exactPolygon = false;
uvec2 exactUV;
vec3 exactColor8;

ivec2 exactVertex(uint packed) {
    return ivec2(int(packed << 16) >> 16, int(packed) >> 16);
}

// Edge a -> b of a triangle with orientation sg (the sign of its area): the
// sample s is inside, or on a top or left edge (the PS1 fills those).
bool exactEdge(ivec2 a, ivec2 b, ivec2 s, int sg) {
    ivec2 d = (b - a) * sg;
    int e = d.x * (s.y - a.y) - d.y * (s.x - a.x);
    return e > 0 || (e == 0 && (d.y < 0 || (d.y == 0 && d.x > 0)));
}

// floor(a / n) for n > 0
int floorDiv(int a, int n) { return a >= 0 ? a / n : -((n - 1 - a) / n); }

// The target pixel (i, j) samples the PS1 polygon at (i / N, j / N): returns
// false when the PS1 does not cover it, else sets the attributes there.
bool exactSample() {
    int n = int(max(passMode.y, 1u));
    // A piece drawn 2048 pixels further (exTri.w bits 1, 2) samples the
    // triangle where the GPU's 11-bit wrap of the line start came from.
    ivec2 s = ivec2(gl_FragCoord.xy) -
        ivec2((exTri.w & 2u) != 0u ? 2048 * n : 0, (exTri.w & 4u) != 0u ? 2048 * n : 0);
    ivec2 a = exactVertex(exTri.x) * n, b = exactVertex(exTri.y) * n,
          c = exactVertex(exTri.z) * n;
    int area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area == 0) return false;
    int sg = area > 0 ? 1 : -1;
    if (!exactEdge(a, b, s, sg) || !exactEdge(b, c, s, sg) ||
        !exactEdge(c, a, s, sg))
        return false;
    // VRAM pixel q plus r / N: the plane at q in 32-bit wrapping 12.12 like
    // the GPU's registers, then the fraction r / N of a pixel's step.
    ivec2 q = ivec2(floorDiv(s.x, n), floorDiv(s.y, n)), r = s - q * n;
    uvec4 at = exBase + exPerX * uint(q.x) + exPerY * uint(q.y);
    uint atB = exBlue.x + exBlue.y * uint(q.x) + exBlue.z * uint(q.y);
    ivec4 frac4 = ivec4(exPerX) * r.x + ivec4(exPerY) * r.y;
    int fracB = int(exBlue.y) * r.x + int(exBlue.z) * r.y;
    if (n > 1) {
        at += uvec4(floorDiv(frac4.x, n), floorDiv(frac4.y, n),
                    floorDiv(frac4.z, n), floorDiv(frac4.w, n));
        atB += uint(floorDiv(fracB, n));
    }
    // 8-bit registers: bits 12..19
    exactUV = (at.xy >> 12) & 255u;
    exactColor8 = vec3((uvec3(at.zw, atB) >> 12) & 255u);
    return true;
}

uvec2 resolveTexel() {
    if (exactPolygon) return (exactUV & texWindow.xy) | texWindow.zw;
    // PS1 interpolates at integer pixel coordinates, the GPU at pixel centres.
    vec2 uv = polygon != 0u ? rawUV + attributeRoundingBias() :
        rawUV - 0.5 * (dFdx(rawUV) + dFdy(rawUV)) + 1.0 / 512.0;
    ivec2 position = ivec2(floor(uv));
    // A sprite reads only its own texels: a squeezed sprite's edge can cut
    // a pixel whose left side lies outside it.
    if (polygon == 0u) position = clamp(position, ivec2(uvBounds.xy), ivec2(uvBounds.zw));
    uvec2 texel = uvec2(position) & uvec2(255u);
    return (texel & texWindow.xy) | texWindow.zw;
}

layout(location = 0) out vec4 FragColor;

uint rgb5551ToU16(vec4 c) {
    uint r = uint(c.r * 31.0 + 0.5);
    uint g = uint(c.g * 31.0 + 0.5);
    uint b = uint(c.b * 31.0 + 0.5);
    uint a = uint(c.a + 0.5);
    return r | (g << 5u) | (b << 10u) | (a << 15u);
}

const mat4 ditherMatrix = mat4(
    -4.0, +0.0, -3.0, +1.0,
    +2.0, -2.0, +3.0, -1.0,
    -3.0, +1.0, -4.0, +0.0,
    +3.0, -1.0, +2.0, -2.0);
// The VRAM pixel this fragment belongs to: at Nx every target pixel of a
// VRAM pixel takes that pixel's dither offset.
ivec2 vramPixel() { return ivec2(gl_FragCoord.xy) / int(max(passMode.y, 1u)); }
// PS1 RGB555 output of an 8-bit colour (integers 0..255): (c + d) >> 3 with
// the 4x4 dither offset d when the primitive dithers, clamped to 0..31.
vec3 quantize(vec3 c8) {
    float off = 0.0;
    if (dither != 0u) {
        ivec2 p = vramPixel() & 3;
        off = ditherMatrix[p.y][p.x];
    }
    return clamp(floor((c8 + off) / 8.0), 0.0, 31.0) / 31.0;
}
// A polygon's 8-bit colour: the PS1 keeps the interpolated value in an
// 8-bit register, so it wraps instead of saturating.
vec3 polygonColor8(vec3 c) {
    return mod(floor(c + attributeRoundingBias()), 256.0);
}

// Texture pages and CLUTs running past VRAM x 1023 wrap to x 0.
ivec2 wrapVram(ivec2 p) { return ivec2(p.x & 1023, p.y & 511); }
vec4 fetchModelTexel(ivec2 position) {
    uvec2 wrapped=(uvec2(position)&uvec2(255u));
    ivec2 texel=ivec2((wrapped&texWindow.xy)|texWindow.zw);
    if (textureMode==1u) return texelFetch(texVram,wrapVram(pageBase+texel),0);
    uint subPixel=uint(texel.x)&subPixelMask;
    ivec2 texelPos=ivec2(texel.x>>texelShift,texel.y);
    uint word16=rgb5551ToU16(texelFetch(texVram,wrapVram(pageBase+texelPos),0));
    uint index=(word16>>(subPixel*indexShift))&indexMask;
    return texelFetch(texClut,ivec2(index,exBlue.w),0);
}

void main() {
    exactPolygon = exTri.w != 0u;
    if (exactPolygon && !exactSample()) discard;
    // Outpainted room margins are presentation-only. They must never reach
    // the native target, even if camera motion moves a margin into its bounds.
    // Explicit queued dialogue glyph: new alpha is presentation-only.
    // BLEND_ADD consumes premultiplied color + source coverage. Raw VRAM
    // retains exactly the original sprite/palette/mask behavior below.
    if(hdUV.z>3.5 && hdUV.z<4.5 && passMode.y>1u){
        if(passMode.x==1u)discard;
        vec4 glyph=texture(texHD,hdUV.xy);
        vec3 tint=min(floor(vertexColor.rgb*127.5+0.5),vec3(255.0))/128.0;
        FragColor=vec4(clamp(glyph.rgb*tint,0.0,1.0)*glyph.a,glyph.a);return;
    }
    if (hdUV.z>1.5 && hdUV.z<3.5) {
        if (hdUV.z>2.5 || passMode.y<=1u) discard;
        vec3 tint=min(floor(vertexColor.rgb*127.5+0.5),vec3(255.0))/128.0;
        vec4 material=texture(texHD,hdUV.xy);
        if (material.a<0.5) discard;
        FragColor=vec4(clamp(material.rgb*tint,0.0,1.0),passMode.x==1u?0.0:1.0);
        return;
    }
    vec4 texColor=textureMode==0u ? vec4(1,1,1,2) :
        fetchModelTexel(ivec2(resolveTexel()));
    if (textureMode!=0u && (passMode.w & 1u)!=0u && model!=0u && polygon!=0u) {
        // Model-only filtering. SPRT also carries field/background tiles: changing
        // their native sample breaks exact HD reference matching and reveals
        // tile rectangles. Bit4 intentionally does not enable that unsafe path.
        vec2 pos=rawUV-vec2(0.5);
        ivec2 lo=ivec2(uvBounds.xy),hi=ivec2(uvBounds.zw);
        ivec2 origin=ivec2(floor(pos));vec2 weight=fract(pos);
        vec4 a=fetchModelTexel(clamp(origin,lo,hi));
        vec4 b=fetchModelTexel(clamp(origin+ivec2(1,0),lo,hi));
        vec4 c=fetchModelTexel(clamp(origin+ivec2(0,1),lo,hi));
        vec4 d=fetchModelTexel(clamp(origin+ivec2(1,1),lo,hi));
        // Never mix transparency or different PS1 blend/mask classes. Keep
        // authored cutouts and neighbouring atlas regions from bleeding.
        if (a!=vec4(0) && b!=vec4(0) && c!=vec4(0) && d!=vec4(0) &&
            a.a==b.a && a.a==c.a && a.a==d.a)
            texColor=mix(mix(a,b,weight.x),mix(c,d,weight.x),weight.y);
    }
    // check for full transparency
    if (texColor == vec4(0, 0, 0, 0)) {
        if (passMode.x != 0u) discard;
        FragColor = vec4(0);
        return;
    }
    // Mask bit of the written pixel: the texel's STP bit (0 untextured),
    // forced by "set mask".
    float maskBit = (passMode.w & 8u) != 0u ? 1.0 :
        (textureMode == 0u ? 0.0 : texColor.a);
    if (passMode.x == 1u) {
        FragColor = vec4(0,0,0,maskBit);
        return;
    }
    // "Check mask" keeps pixels whose mask bit is set. The opaque pass 3
    // blends with the destination's alpha instead, as its triangles overlap.
    if (passMode.x == 2u && (passMode.w & 16u) != 0u &&
        texelFetch(texBlend, ivec2(gl_FragCoord.xy), 0).a >= 0.5)
        discard;
    // check for setSemiTrans(p, 1)
    bool isSemiTrans = vertexColor.a < 0.75;
    // when a color has the 0x8000 bit left then it has the semitrans flag on
    bool colorSemiTrans = texColor.a > 0;
    // PS1-accurate texture-color modulation: (tex5 * col8) >> 7, clamp to 31
    vec3 modColor;
    if ((passMode.w & 2u) != 0u) {
        // Explicit Off is a display enhancement; Auto retains PS1 RGB555.
        modColor = clamp(textureMode == 0u ? vertexColor.rgb :
                         texColor.rgb * vertexColor.rgb, 0.0, 1.0);
    } else if (textureMode == 0u) {
        // untextured: PS1 uses color directly, no modulation
        vec3 col8 = exactPolygon ? exactColor8 :
            polygon != 0u ? polygonColor8(vertexColor.rgb * 255.0)
                          : floor(vertexColor.rgb * 255.0 + 0.5);
        modColor = quantize(col8);
    } else {
        // (texel5 * colour8) >> 4, saturated to 8 bits, then dithered to
        // RGB555; without dithering this is (texel5 * colour8) >> 7.
        vec3 tex5 = floor(texColor.rgb * 31.0 + 0.5);
        vec3 col8 = exactPolygon ? exactColor8 :
            polygon != 0u ? polygonColor8(vertexColor.rgb * 127.5)
                          : min(floor(vertexColor.rgb * 127.5 + 0.5), vec3(255.0));
        modColor = quantize(min(floor(tex5 * col8 / 16.0), vec3(255.0)));
    }
    bool enhancedPixel=false;
    // Presentation only. The original texel still owns coverage, STP and
    // palette changes. Unmatched reference colours fall back pixel by pixel.
    if (passMode.y>1u && hdUV.z>0.5 && hdUV.z<1.5 && textureMode!=0u) {
        ivec2 size=textureSize(texHD,0);
        ivec2 reference=ivec2(hdUV.xy*vec2(size))+ivec2(0,size.y/2);
        vec3 basis=texelFetch(texHD,reference,0).rgb;
        if (all(equal(floor(basis*31.0+0.5),floor(texColor.rgb*31.0+0.5)))) {
            vec3 enhanced=texture(texHD,hdUV.xy).rgb;
            vec3 tint=min(floor(vertexColor.rgb*127.5+0.5),vec3(255.0))/128.0;
            modColor=clamp(enhanced*tint,0.0,1.0);
            enhancedPixel=true;
        }
    }
    // Exact source-page/CLUT matching owns identity; native texel still owns
    // transparent cutouts, STP, mask writes and blend classification.
    if (passMode.y>1u && hdUV.z>4.5 && polygon!=0u && textureMode!=0u) {
        vec3 enhanced=texture(texHD,hdUV.xy).rgb;
        vec3 tint=min(floor(vertexColor.rgb*127.5+0.5),vec3(255.0))/128.0;
        modColor=clamp(enhanced*tint,0.0,1.0);enhancedPixel=true;
    }
    if (passMode.x == 2u && (enhancedPixel || (passMode.w & 2u) != 0u)) {
        if (colorSemiTrans && isSemiTrans) {
            vec3 back = texelFetch(texBlend, ivec2(gl_FragCoord.xy), 0).rgb;
            uint abr = (tpage & 0x60u) >> 5u;
            if (abr == 0u) modColor = (back + modColor) * 0.5;
            else if (abr == 1u) modColor = back + modColor;
            else if (abr == 2u) modColor = back - modColor;
            else modColor = back + modColor * 0.25;
        }
        FragColor = vec4(clamp(modColor, 0.0, 1.0), maskBit);
        return;
    }
    if (passMode.x == 2u) {
        ivec3 color = ivec3(floor(modColor * 31.0 + 0.5));
        if (colorSemiTrans && isSemiTrans) {
            ivec3 back = ivec3(floor(texelFetch(texBlend,
                ivec2(gl_FragCoord.xy), 0).rgb * 31.0 + 0.5));
            uint abr = (tpage & 0x60u) >> 5u;
            if (abr == 0u) color = (back + color) >> 1;
            else if (abr == 1u) color = back + color;
            else if (abr == 2u) color = back - color;
            else color = back + (color >> 2);
        }
        FragColor = vec4(vec3(clamp(color,ivec3(0),ivec3(31))) / 31.0, maskBit);
        return;
    }
    if (passMode.x == 3u) {
        FragColor = vec4(modColor, maskBit);
        return;
    }
    // pre-multiplied alpha output for ONE, ONE_MINUS_SRC_ALPHA blending
    if (colorSemiTrans && isSemiTrans) {
        uint abr = (tpage & 0x60u) >> 5u;
        if (abr == 0u) {
            FragColor = vec4(modColor * 0.5, 0.5); // 50% blend
        } else if (abr == 1u) {
            FragColor = vec4(modColor, 0.0); // additive
        } else if (abr == 2u) {
            FragColor = vec4(modColor, 0.0); // subtractive
        } else {                                  // abr == 3u
            FragColor = vec4(modColor * 0.25, 0.0); // B + F/4
        }
    } else {
        FragColor = vec4(modColor, 1.0); // full opacity
    }
}
