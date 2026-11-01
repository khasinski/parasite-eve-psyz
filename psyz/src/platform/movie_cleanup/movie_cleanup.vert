#version 450
// SPDX-License-Identifier: MIT
// Project-original fullscreen triangle, no vertex buffer or game geometry.
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
