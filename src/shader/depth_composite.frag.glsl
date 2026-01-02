#version 450

layout(set=0, binding=0) uniform sampler2D originalImage;
layout(set=0, binding=1) uniform sampler2D effectedImage;
layout(set=0, binding=2) uniform sampler2D depthImage;

layout(push_constant) uniform PushConstants {
    int enabled;           // 0 = passthrough, 1 = depth masking
    float depthThreshold;  // Threshold for UI detection (default 0.9999)
    int reversed;          // 0 = normal (far=1), 1 = reversed/DXVK (far=0)
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

    float depth = texture(depthImage, textureCoord).r;

    // Debug mode: threshold < 0.5 shows depth buffer visualization
    if (pushConstants.depthThreshold < 0.5)
    {
        // Show depth as grayscale (0=black, 1=white)
        // For reversed depth, invert so far is still white
        float displayDepth = (pushConstants.reversed != 0) ? (1.0 - depth) : depth;
        fragColor = vec4(displayDepth, displayDepth, displayDepth, 1.0);
        return;
    }

    vec4 original = texture(originalImage, textureCoord);

    // Determine if this pixel should show original (UI) or effected (3D world)
    bool isUI;
    if (pushConstants.reversed != 0)
    {
        // DXVK reversed depth: near=1, far=0
        // UI is at far plane = depth near 0
        isUI = (depth < (1.0 - pushConstants.depthThreshold));
    }
    else
    {
        // Normal depth: near=0, far=1
        // UI is at far plane = depth near 1
        isUI = (depth > pushConstants.depthThreshold);
    }

    fragColor = isUI ? original : effected;
}
