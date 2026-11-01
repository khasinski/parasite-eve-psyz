#version 450
#extension GL_GOOGLE_include_directive : require
// AMD FSR1 EASU, full precision. See third_party/fsr1/LICENSE.txt.
layout(set=2,binding=0) uniform sampler2D sourceImage;
layout(set=3,binding=0,std140) uniform Constants {
    uvec4 c0; uvec4 c1; uvec4 c2; uvec4 c3;
};
layout(location=0) out vec4 resultColor;
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return textureGather(sourceImage,p,0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(sourceImage,p,1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(sourceImage,p,2); }
#include "ffx_fsr1.h"
void main() {
    AF3 c;
    FsrEasuF(c, AU2(gl_FragCoord.xy), c0,c1,c2,c3);
    resultColor=vec4(c,1.0);
}
