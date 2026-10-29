#version 450

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

layout(set = 2, binding = 0) uniform sampler2D texVram;
layout(set = 2, binding = 1) uniform sampler2D texBlend;
layout(set = 2, binding = 2) uniform sampler2D texHD;
layout(set = 3, binding = 0) uniform PassMode { uvec4 passMode; };

float attributeRoundingBias() {
    // Native 1:1 polygon attributes occupy a 1/4096 lattice. Half an LSB
    // repairs floating interpolation just below an exact integer without
    // rounding the preceding representable value up to it.
    return polygon != 0u && passMode.y == 1u && passMode.z != 0u
        ? 1.0 / 8192.0 : 0.0;
}

uvec2 resolveTexel() {
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
vec3 applyDither(vec3 c) {
    // Explicit Off is a display enhancement; Auto retains PS1 RGB555.
    if ((passMode.w & 2u) != 0u) return clamp(c, 0.0, 1.0);
    if (dither == 0u) return clamp(floor(c * 255.0 / 8.0), 0.0, 31.0) / 31.0;
    int dx = int(gl_FragCoord.x) & 3;
    int dy = int(gl_FragCoord.y) & 3;
    float off = ditherMatrix[dy][dx];
    vec3 c8 = c * 255.0 + off;
    vec3 c5 = clamp(floor(c8 / 8.0), 0.0, 31.0);
    return c5 / 31.0;
}

vec4 fetchModelTexel(ivec2 position) {
    uvec2 wrapped=(uvec2(position)&uvec2(255u));
    ivec2 texel=ivec2((wrapped&texWindow.xy)|texWindow.zw);
    if (textureMode==1u) return texelFetch(texVram,pageBase+texel,0);
    uint subPixel=uint(texel.x)&subPixelMask;
    ivec2 texelPos=ivec2(texel.x>>texelShift,texel.y);
    uint word16=rgb5551ToU16(texelFetch(texVram,pageBase+texelPos,0));
    uint index=(word16>>(subPixel*indexShift))&indexMask;
    ivec2 base=ivec2((clut%64u)*16u,clut/64u);
    return texelFetch(texVram,base+ivec2(index,0),0);
}

void main() {
    // Outpainted room margins are presentation-only. They must never reach
    // the native target, even if camera motion moves a margin into its bounds.
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
    if ((passMode.w & 1u)!=0u && model!=0u && textureMode!=0u) {
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
    bool interfacePixel=hdUV.z>3.5 && passMode.y>1u;
    vec4 interfaceColor=vec4(0);
    if(interfacePixel){
        vec2 uv=hdUV.xy;
        interfaceColor=texture(texHD,uv);
        if(interfaceColor.a<0.5){if(passMode.x!=0u)discard;FragColor=vec4(0);return;}
        texColor.rgb=interfaceColor.rgb;
    }
    // check for full transparency
    if (!interfacePixel && texColor == vec4(0, 0, 0, 0)) {
        if (passMode.x != 0u) discard;
        FragColor = vec4(0);
        return;
    }
    if (passMode.x == 1u) {
        FragColor = vec4(0,0,0,textureMode == 0u ? 0.0 : texColor.a);
        return;
    }
    // check for setSemiTrans(p, 1)
    bool isSemiTrans = vertexColor.a < 0.75;
    // when a color has the 0x8000 bit left then it has the semitrans flag on
    bool colorSemiTrans = texColor.a > 0;
    // PS1-accurate texture-color modulation: (tex5 * col8) >> 7, clamp to 31
    vec3 modColor;
    if (textureMode == 0u) {
        // untextured: PS1 uses color directly, no modulation
        modColor = texColor.rgb * (vertexColor.rgb + attributeRoundingBias() / 255.0);
    } else {
        vec3 tex5 = floor(texColor.rgb * 31.0 + 0.5);
        vec3 col8 = min(floor(vertexColor.rgb * 127.5 +
                             (polygon != 0u ? attributeRoundingBias() : 0.5)), vec3(255.0));
        vec3 prod8 = min(tex5 * col8 / 16.0, vec3(255.0));
        modColor = dither != 0u ? prod8 / 255.0
                                : floor(prod8 / 8.0) / 31.0;
    }
    if ((passMode.w & 2u) != 0u && textureMode != 0u)
        modColor = texColor.rgb * vertexColor.rgb;
    modColor = applyDither(modColor);
    bool enhancedPixel=interfacePixel;
    if(interfacePixel){
        vec3 tint=min(floor(vertexColor.rgb*127.5+0.5),vec3(255.0))/128.0;
        modColor=clamp(interfaceColor.rgb*tint,0.0,1.0);
    }
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
    if (passMode.x == 2u && (enhancedPixel || (passMode.w & 2u) != 0u)) {
        if (colorSemiTrans && isSemiTrans) {
            vec3 back = texelFetch(texBlend, ivec2(gl_FragCoord.xy), 0).rgb;
            uint abr = (tpage & 0x60u) >> 5u;
            if (abr == 0u) modColor = (back + modColor) * 0.5;
            else if (abr == 1u) modColor = back + modColor;
            else if (abr == 2u) modColor = back - modColor;
            else modColor = back + modColor * 0.25;
        }
        FragColor = vec4(clamp(modColor, 0.0, 1.0), textureMode == 0u ? 0.0 : texColor.a);
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
        FragColor = vec4(vec3(clamp(color,ivec3(0),ivec3(31))) / 31.0,
                         textureMode == 0u ? 0.0 : texColor.a);
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
