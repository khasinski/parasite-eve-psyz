#version 450
// SPDX-License-Identifier: MIT
// Independently authored bounded spatial filter. Operates on encoded UNORM
// movie RGB, before existing FSR/upscale; not HDR, AI or detail reconstruction.
layout(set=2,binding=0) uniform sampler2D movie_source;
layout(set=3,binding=0,std140) uniform Params {
    vec4 source_rect;
    vec4 logical_strength; // authored width,height,edge-threshold,preset strength
} params;
layout(location=0) out vec4 output_color;
vec3 sample_logical(ivec2 at) {
    ivec2 logical=ivec2(params.logical_strength.xy);
    at=clamp(at,ivec2(0),logical-1);
    vec2 relative=(vec2(at)+0.5)/vec2(logical);
    ivec2 tex_size=textureSize(movie_source,0);
    ivec2 lo=clamp(ivec2(params.source_rect.xy),ivec2(0),tex_size-1);
    ivec2 hi=clamp(ivec2(params.source_rect.xy+params.source_rect.zw)-1,lo,tex_size-1);
    ivec2 pixel=ivec2(floor(params.source_rect.xy+relative*params.source_rect.zw));
    return texelFetch(movie_source,clamp(pixel,lo,hi),0).rgb;
}
// Optional mode3 only: a bounded nominal8-pixel seam heuristic. No codec
// metadata, motion history or reconstructed detail is claimed. True contrast
// and textured slopes reject smoothing; all results are convex/clamped RGB.
float channel_max(vec3 value) { return max(value.r,max(value.g,value.b)); }
vec3 smooth_block_axis(ivec2 pixel,ivec2 axis,vec3 center) {
    int position=axis.x!=0?pixel.x:pixel.y;
    int residue=position&7;
    if(residue!=0&&residue!=7)return center;
    int boundary=position+(residue==7?1:0);
    int extent=axis.x!=0?int(params.logical_strength.x):int(params.logical_strength.y);
    if(boundary<2||boundary+1>=extent)return center;
    ivec2 first=pixel+axis*(boundary-position);
    vec3 p1=sample_logical(first-axis*2),p0=sample_logical(first-axis);
    vec3 q0=sample_logical(first),q1=sample_logical(first+axis);
    float slope=max(channel_max(abs(p1-p0)),channel_max(abs(q1-q0)));
    float contrast=channel_max(abs(p0-q0));
    float flatness=max(0.0,1.0-slope/(params.logical_strength.z*0.35));
    float edge_keep=max(0.0,1.0-contrast/(params.logical_strength.z*1.5));
    vec3 average=(p1+2.0*p0+2.0*q0+q1)/6.0;
    return mix(center,average,0.45*flatness*edge_keep);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec3 center=sample_logical(pixel);
    if(params.logical_strength.w>0.65) {
        center=smooth_block_axis(pixel,ivec2(1,0),center);
        center=smooth_block_axis(pixel,ivec2(0,1),center);
    }
    vec3 sum=center*4.0;
    float weight_sum=4.0;
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++) {
        if(x==0&&y==0)continue;
        vec3 value=sample_logical(pixel+ivec2(x,y));
        vec3 difference=abs(value-center);
        float edge=max(difference.r,max(difference.g,difference.b));
        float w=max(0.0,1.0-edge/params.logical_strength.z);
        w=w*w*((x==0||y==0)?1.0:0.70710678);
        sum+=value*w;weight_sum+=w;
    }
    // More conservative away from nominal8-pixel compression boundaries.
    // No codec metadata is invented: this is only a spatial strength heuristic.
    bool boundary=((pixel.x&7)==0)||((pixel.x&7)==7)||((pixel.y&7)==0)||((pixel.y&7)==7);
    float amount=params.logical_strength.w*(boundary?1.0:0.35);
    output_color=vec4(clamp(mix(center,sum/weight_sum,amount),0.0,1.0),1.0);
}
