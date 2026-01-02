#version 450

layout(set=0, binding=0) uniform sampler2D originalImage;
layout(set=0, binding=1) uniform sampler2D effectedImage;
layout(set=0, binding=2) uniform sampler2D depthImage;

layout(constant_id = 0) const float depthThreshold = 0.9999;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    float depth = texture(depthImage, textureCoord).r;
    vec4 original = texture(originalImage, textureCoord);
    vec4 effected = texture(effectedImage, textureCoord);

    // UI pixels typically have depth = 1.0 (far plane)
    // 3D world pixels have depth < 1.0
    // For DXVK (reversed depth): UI = 0.0, world > 0.0
    // Auto-detect: if depth > threshold, assume it's UI
    // With reversed depth, we invert the comparison

    // Simple approach: sample center of screen to detect reversed depth
    // If center depth > 0.5, likely normal depth; if < 0.5, likely reversed
    // For now, assume normal depth buffer (UI at far plane = 1.0)

    // Keep original (no effects) where depth >= threshold (UI)
    // Apply effects where depth < threshold (3D world)
    fragColor = (depth >= depthThreshold) ? original : effected;
}
