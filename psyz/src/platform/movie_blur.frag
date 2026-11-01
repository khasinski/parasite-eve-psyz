#version 450
// Project original separable nine-tap Gaussian background blur.
layout(set=2,binding=0) uniform sampler2D image;
layout(set=3,binding=0,std140) uniform Parameters {vec4 step_dim;};
layout(location=0) out vec4 colour;
void main(){
    vec2 uv=gl_FragCoord.xy/vec2(textureSize(image,0));
    vec2 d=step_dim.xy;
    vec3 c=texture(image,uv).rgb*0.2270270270;
    c+=(texture(image,uv+d).rgb+texture(image,uv-d).rgb)*0.1945945946;
    c+=(texture(image,uv+d*2).rgb+texture(image,uv-d*2).rgb)*0.1216216216;
    c+=(texture(image,uv+d*3).rgb+texture(image,uv-d*3).rgb)*0.0540540541;
    c+=(texture(image,uv+d*4).rgb+texture(image,uv-d*4).rgb)*0.0162162162;
    colour=vec4(c*step_dim.z,1);
}
