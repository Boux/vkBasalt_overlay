#version 450

layout(set=0, binding=0) uniform sampler2D originalImage;
layout(set=0, binding=1) uniform sampler2D effectedImage;
layout(set=0, binding=2) uniform sampler2D depthImage;

layout(constant_id = 0) const float depthThreshold = 0.9999;

layout(push_constant) uniform PushConstants {
    int enabled;  // 0 = passthrough, 1 = depth masking
} pushConstants;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 effected = texture(effectedImage, textureCoord);

    // Passthrough mode - just output effected image
    if (pushConstants.enabled == 0)
    {
        fragColor = effected;
        return;
    }

    // Depth masking mode
    float depth = texture(depthImage, textureCoord).r;
    vec4 original = texture(originalImage, textureCoord);

    // UI pixels typically have depth = 1.0 (far plane)
    // 3D world pixels have depth < 1.0
    // Keep original (no effects) where depth >= threshold (UI)
    // Apply effects where depth < threshold (3D world)
    fragColor = (depth >= depthThreshold) ? original : effected;
}
