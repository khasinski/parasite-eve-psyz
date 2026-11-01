#version 450
#extension GL_GOOGLE_include_directive : require
// AMD FSR1 RCAS, full precision. See third_party/fsr1/LICENSE.txt.
layout(set=2,binding=0) uniform sampler2D sourceImage;
layout(set=3,binding=0,std140) uniform Constants { uvec4 c0; };
layout(location=0) out vec4 resultColor;
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) {
    return texelFetch(sourceImage,clamp(p,ivec2(0),textureSize(sourceImage,0)-ivec2(1)),0);
}
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b) {}
#include "ffx_fsr1.h"
void main() {
    AF3 c;
    FsrRcasF(c.r,c.g,c.b,AU2(gl_FragCoord.xy),c0);
    resultColor=vec4(c,1.0);
}
