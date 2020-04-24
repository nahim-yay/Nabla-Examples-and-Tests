// vertex shader is provided by the fullScreenTriangle extension

#version 430 core
layout(set = 3, binding = 0) uniform sampler2DArray tex0;

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 pixelColor;

void main()
{
    pixelColor = texture(tex0, vec3(TexCoord.x, TexCoord.y, 1.0));
}