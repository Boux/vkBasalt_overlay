#include "imgui_overlay.hpp"
#include "logger.hpp"

#include <fstream>
#include <filesystem>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>

#include "imgui/imgui.h"

namespace vkBasalt
{
    // Build version - increment this each build
    static constexpr int BUILD_NUMBER = 9;
    static constexpr const char* BUILD_DATE = "2026-01-01";
    namespace
    {
        // Ring buffer for storing history
        template<typename T, size_t N>
        class RingBuffer
        {
        public:
            void push(T value)
            {
                data[writeIndex] = value;
                writeIndex = (writeIndex + 1) % N;
                if (count < N)
                    count++;
            }

            T get(size_t i) const
            {
                if (i >= count)
                    return T{};
                size_t idx = (writeIndex + N - count + i) % N;
                return data[idx];
            }

            size_t size() const { return count; }
            static constexpr size_t capacity() { return N; }

            T min() const
            {
                if (count == 0) return T{};
                T m = get(0);
                for (size_t i = 1; i < count; i++)
                    m = std::min(m, get(i));
                return m;
            }

            T max() const
            {
                if (count == 0) return T{};
                T m = get(0);
                for (size_t i = 1; i < count; i++)
                    m = std::max(m, get(i));
                return m;
            }

            T avg() const
            {
                if (count == 0) return T{};
                T sum = T{};
                for (size_t i = 0; i < count; i++)
                    sum += get(i);
                return sum / static_cast<T>(count);
            }

            // Get data as contiguous array for ImGui plotting
            void copyTo(float* out) const
            {
                for (size_t i = 0; i < count; i++)
                    out[i] = static_cast<float>(get(i));
            }

        private:
            T data[N] = {};
            size_t writeIndex = 0;
            size_t count = 0;
        };

        // Static state for diagnostics
        static RingBuffer<float, 300> frameTimeHistory;   // 300 samples (~5 seconds at 60fps)
        static RingBuffer<float, 300> gpuUsageHistory;
        static RingBuffer<float, 300> vramUsageHistory;
        static RingBuffer<float, 300> gttUsageHistory;    // Shared memory for iGPUs
        static std::chrono::steady_clock::time_point lastFrameTime;
        static std::string drmCardPath;

        // Find the DRM card path for GPU stats
        std::string findDrmCard()
        {
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm"))
                {
                    std::string name = entry.path().filename().string();
                    if (name.find("card") == 0 && name.find("-") == std::string::npos)
                    {
                        std::string devicePath = entry.path().string() + "/device";
                        // Check if this card has GPU busy percentage
                        if (std::filesystem::exists(devicePath + "/gpu_busy_percent"))
                            return entry.path().string();
                    }
                }
            }
            catch (...) {}
            return "";
        }

        // Read a single value from sysfs
        template<typename T>
        bool readSysfs(const std::string& path, T& value)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;
            file >> value;
            return !file.fail();
        }

        // Get GPU usage percentage (0-100)
        float getGpuUsage()
        {
            if (drmCardPath.empty())
                return -1.0f;

            int usage = 0;
            if (readSysfs(drmCardPath + "/device/gpu_busy_percent", usage))
                return static_cast<float>(usage);
            return -1.0f;
        }

        // Get VRAM usage in MB and total (dedicated VRAM)
        bool getVramUsage(float& usedMB, float& totalMB)
        {
            if (drmCardPath.empty())
                return false;

            uint64_t used = 0, total = 0;

            // AMD format
            if (readSysfs(drmCardPath + "/device/mem_info_vram_used", used) &&
                readSysfs(drmCardPath + "/device/mem_info_vram_total", total))
            {
                usedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                totalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
                return true;
            }

            return false;
        }

        // Get GTT (shared system memory) usage in MB - for iGPUs
        bool getGttUsage(float& usedMB, float& totalMB)
        {
            if (drmCardPath.empty())
                return false;

            uint64_t used = 0, total = 0;

            if (readSysfs(drmCardPath + "/device/mem_info_gtt_used", used) &&
                readSysfs(drmCardPath + "/device/mem_info_gtt_total", total))
            {
                usedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                totalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
                return true;
            }

            return false;
        }

        // Helper to draw a graph with label
        void drawGraph(const char* label, const char* id, RingBuffer<float, 300>& history, float minVal, float maxVal,
                       const char* overlayFmt, ImVec4 color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f))
        {
            ImGui::Text("%s", label);

            // Get data for plotting
            float data[300];
            history.copyTo(data);

            ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

            char overlay[64];
            snprintf(overlay, sizeof(overlay), overlayFmt, history.size() > 0 ? history.get(history.size() - 1) : 0.0f);

            ImGui::PlotLines(id, data, static_cast<int>(history.size()), 0, overlay,
                            minVal, maxVal, ImVec2(-1, 60));

            ImGui::PopStyleColor(2);

            // Stats below graph
            if (history.size() > 0)
            {
                ImGui::TextDisabled("Min: %.1f  Avg: %.1f  Max: %.1f",
                    history.min(), history.avg(), history.max());
            }
        }
    }

    void ImGuiOverlay::renderDiagnosticsView()
    {
        // Initialize on first call
        static bool initialized = false;
        if (!initialized)
        {
            drmCardPath = findDrmCard();
            lastFrameTime = std::chrono::steady_clock::now();
            initialized = true;
            if (!drmCardPath.empty())
                Logger::info("Diagnostics: Found GPU at " + drmCardPath);
            else
                Logger::info("Diagnostics: No GPU sysfs interface found");
        }

        // Calculate frame time
        auto now = std::chrono::steady_clock::now();
        float frameTimeMs = std::chrono::duration<float, std::milli>(now - lastFrameTime).count();
        lastFrameTime = now;

        // Only record if reasonable (avoid spikes from tab switching)
        if (frameTimeMs > 0.1f && frameTimeMs < 500.0f)
            frameTimeHistory.push(frameTimeMs);

        // Sample GPU stats (less frequently to reduce overhead)
        static int sampleCounter = 0;
        if (++sampleCounter >= 10)  // Every 10 frames
        {
            sampleCounter = 0;

            float gpuUsage = getGpuUsage();
            if (gpuUsage >= 0)
                gpuUsageHistory.push(gpuUsage);

            float vramUsed, vramTotal;
            if (getVramUsage(vramUsed, vramTotal))
                vramUsageHistory.push((vramUsed / vramTotal) * 100.0f);

            float gttUsed, gttTotal;
            if (getGttUsage(gttUsed, gttTotal))
                gttUsageHistory.push((gttUsed / gttTotal) * 100.0f);
        }

        ImGui::BeginChild("DiagnosticsContent", ImVec2(0, 0), false);

        // Frame rate and timing
        float avgFrameTime = frameTimeHistory.avg();
        float fps = avgFrameTime > 0 ? 1000.0f / avgFrameTime : 0;
        float fps1Low = frameTimeHistory.max() > 0 ? 1000.0f / frameTimeHistory.max() : 0;

        ImGui::Text("Performance");
        ImGui::Separator();

        // Big FPS display
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);  // Default font, could be bigger
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%.0f FPS", fps);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("(1%% low: %.0f)", fps1Low);

        ImGui::Spacing();

        // Frame time graph
        drawGraph("Frame Time", "##frametime", frameTimeHistory, 0.0f, 50.0f, "%.1f ms",
                  ImVec4(0.4f, 0.8f, 0.4f, 1.0f));

        ImGui::Spacing();
        ImGui::Spacing();

        // GPU stats (if available)
        if (!drmCardPath.empty())
        {
            ImGui::Text("GPU");
            ImGui::Separator();

            float currentGpuUsage = getGpuUsage();
            if (currentGpuUsage >= 0)
            {
                // GPU Usage graph
                drawGraph("GPU Usage", "##gpuusage", gpuUsageHistory, 0.0f, 100.0f, "%.0f%%",
                          ImVec4(0.8f, 0.6f, 0.2f, 1.0f));

                ImGui::Spacing();
            }

            float vramUsed, vramTotal;
            if (getVramUsage(vramUsed, vramTotal))
            {
                // VRAM bar (dedicated)
                ImGui::Text("VRAM (dedicated): %.0f / %.0f MB",
                    vramUsed, vramTotal);
                ImGui::ProgressBar(vramUsed / vramTotal, ImVec2(-1, 0));
            }

            float gttUsed, gttTotal;
            if (getGttUsage(gttUsed, gttTotal))
            {
                // GTT bar (shared system memory - more relevant for iGPUs)
                ImGui::Text("GTT (shared): %.0f / %.0f MB",
                    gttUsed, gttTotal);
                ImGui::ProgressBar(gttUsed / gttTotal, ImVec2(-1, 0));

                ImGui::Spacing();

                // GTT usage graph (more useful for iGPUs)
                drawGraph("Memory Usage", "##gttusage", gttUsageHistory, 0.0f, 100.0f, "%.0f%%",
                          ImVec4(0.6f, 0.4f, 0.8f, 1.0f));
            }
            else if (getVramUsage(vramUsed, vramTotal))
            {
                ImGui::Spacing();

                // VRAM usage graph (for dGPUs without GTT)
                drawGraph("VRAM Usage", "##vramusage", vramUsageHistory, 0.0f, 100.0f, "%.0f%%",
                          ImVec4(0.6f, 0.4f, 0.8f, 1.0f));
            }
        }
        else
        {
            ImGui::Spacing();
            ImGui::TextDisabled("GPU stats not available");
            ImGui::TextDisabled("(No sysfs interface found)");
        }

        // Build info at bottom
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextDisabled("Build #%d (%s)", BUILD_NUMBER, BUILD_DATE);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
        ImGui::TextWrapped("This is an early beta build. Please report any issues or bugs to:");
        ImGui::PopStyleColor();
        ImGui::TextLinkOpenURL("github.com/Boux/vkBasalt_overlay/issues", "https://github.com/Boux/vkBasalt_overlay/issues");

        ImGui::EndChild();
    }

} // namespace vkBasalt
