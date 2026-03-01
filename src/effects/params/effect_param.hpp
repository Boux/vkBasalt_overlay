#ifndef EFFECT_PARAM_HPP_INCLUDED
#define EFFECT_PARAM_HPP_INCLUDED

#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <cstdint>
#include <sstream>
#include <locale>

namespace vkBasalt
{
    // Locale-independent float-to-string (always uses '.' as decimal separator)
    inline std::string floatToString(float v)
    {
        std::ostringstream ss;
        ss.imbue(std::locale::classic());
        ss << v;
        return ss.str();
    }

    enum class ParamType
    {
        Float,
        FloatVec,   // float2, float3, float4 - uses componentCount
        Int,
        IntVec,     // int2, int3, int4 - uses componentCount
        Uint,       // scalar unsigned int
        UintVec,    // uint2, uint3, uint4 - uses componentCount
        Bool
    };

    // Base class for effect parameters
    class EffectParam
    {
    public:
        virtual ~EffectParam() = default;

        std::string effectName;  // Which effect this belongs to (e.g., "cas", "Clarity.fx")
        std::string name;        // Parameter name (e.g., "casSharpness")
        std::string label;       // Display label (from ui_label or name)
        std::string tooltip;     // ui_tooltip - hover description
        std::string uiType;      // ui_type - "slider", "drag", "combo", etc.

        virtual ParamType getType() const = 0;
        virtual const char* getTypeName() const = 0;
        virtual bool hasChanged() const = 0;
        virtual void resetToDefault() = 0;
        virtual std::vector<std::pair<std::string, std::string>> serialize() const = 0;
        virtual std::unique_ptr<EffectParam> clone() const = 0;
    };

    // Float parameter (scalar)
    class FloatParam : public EffectParam
    {
    public:
        float value = 0.0f;
        float defaultValue = 0.0f;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float step = 0.0f;

        ParamType getType() const override { return ParamType::Float; }
        const char* getTypeName() const override { return "FLOAT"; }

        bool hasChanged() const override { return value != defaultValue; }

        void resetToDefault() override { value = defaultValue; }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {{"", floatToString(value)}};
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<FloatParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value = value;
            p->defaultValue = defaultValue;
            p->minValue = minValue;
            p->maxValue = maxValue;
            p->step = step;
            return p;
        }
    };

    // Float vector parameter (float2, float3, float4)
    class FloatVecParam : public EffectParam
    {
    public:
        uint32_t componentCount = 2;  // 2, 3, or 4
        float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float minValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float maxValue[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float step = 0.0f;

        ParamType getType() const override { return ParamType::FloatVec; }

        const char* getTypeName() const override
        {
            static const char* names[] = {"FLOAT2", "FLOAT3", "FLOAT4"};
            if (componentCount >= 2 && componentCount <= 4)
                return names[componentCount - 2];
            return "FLOATVEC";
        }

        bool hasChanged() const override
        {
            for (uint32_t i = 0; i < componentCount; i++)
                if (value[i] != defaultValue[i])
                    return true;
            return false;
        }

        void resetToDefault() override
        {
            for (uint32_t i = 0; i < componentCount; i++)
                value[i] = defaultValue[i];
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            std::vector<std::pair<std::string, std::string>> result;
            for (uint32_t i = 0; i < componentCount; i++)
                result.push_back({name + "[" + std::to_string(i) + "]", floatToString(value[i])});
            return result;
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<FloatVecParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->componentCount = componentCount;
            for (uint32_t i = 0; i < 4; i++)
            {
                p->value[i] = value[i];
                p->defaultValue[i] = defaultValue[i];
                p->minValue[i] = minValue[i];
                p->maxValue[i] = maxValue[i];
            }
            p->step = step;
            return p;
        }
    };

    // Int parameter (scalar)
    class IntParam : public EffectParam
    {
    public:
        int value = 0;
        int defaultValue = 0;
        int minValue = 0;
        int maxValue = 100;
        float step = 0.0f;
        std::vector<std::string> items;  // ui_items - combo box options

        ParamType getType() const override { return ParamType::Int; }
        const char* getTypeName() const override { return "INT"; }

        bool hasChanged() const override { return value != defaultValue; }

        void resetToDefault() override { value = defaultValue; }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {{"", std::to_string(value)}};
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<IntParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value = value;
            p->defaultValue = defaultValue;
            p->minValue = minValue;
            p->maxValue = maxValue;
            p->step = step;
            p->items = items;
            return p;
        }
    };

    // Int vector parameter (int2, int3, int4)
    class IntVecParam : public EffectParam
    {
    public:
        uint32_t componentCount = 2;  // 2, 3, or 4
        int value[4] = {0, 0, 0, 0};
        int defaultValue[4] = {0, 0, 0, 0};
        int minValue[4] = {0, 0, 0, 0};
        int maxValue[4] = {100, 100, 100, 100};
        float step = 0.0f;

        ParamType getType() const override { return ParamType::IntVec; }

        const char* getTypeName() const override
        {
            static const char* names[] = {"INT2", "INT3", "INT4"};
            if (componentCount >= 2 && componentCount <= 4)
                return names[componentCount - 2];
            return "INTVEC";
        }

        bool hasChanged() const override
        {
            for (uint32_t i = 0; i < componentCount; i++)
                if (value[i] != defaultValue[i])
                    return true;
            return false;
        }

        void resetToDefault() override
        {
            for (uint32_t i = 0; i < componentCount; i++)
                value[i] = defaultValue[i];
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            std::vector<std::pair<std::string, std::string>> result;
            for (uint32_t i = 0; i < componentCount; i++)
                result.push_back({name + "[" + std::to_string(i) + "]", std::to_string(value[i])});
            return result;
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<IntVecParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->componentCount = componentCount;
            for (uint32_t i = 0; i < 4; i++)
            {
                p->value[i] = value[i];
                p->defaultValue[i] = defaultValue[i];
                p->minValue[i] = minValue[i];
                p->maxValue[i] = maxValue[i];
            }
            p->step = step;
            return p;
        }
    };

    // Uint parameter (scalar unsigned int)
    class UintParam : public EffectParam
    {
    public:
        uint32_t value = 0;
        uint32_t defaultValue = 0;
        uint32_t minValue = 0;
        uint32_t maxValue = 100;
        float step = 0.0f;

        ParamType getType() const override { return ParamType::Uint; }
        const char* getTypeName() const override { return "UINT"; }

        bool hasChanged() const override { return value != defaultValue; }

        void resetToDefault() override { value = defaultValue; }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {{"", std::to_string(value)}};
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<UintParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value = value;
            p->defaultValue = defaultValue;
            p->minValue = minValue;
            p->maxValue = maxValue;
            p->step = step;
            return p;
        }
    };

    // Uint vector parameter (uint2, uint3, uint4)
    class UintVecParam : public EffectParam
    {
    public:
        uint32_t componentCount = 2;  // 2, 3, or 4
        uint32_t value[4] = {0, 0, 0, 0};
        uint32_t defaultValue[4] = {0, 0, 0, 0};
        uint32_t minValue[4] = {0, 0, 0, 0};
        uint32_t maxValue[4] = {100, 100, 100, 100};
        float step = 0.0f;

        ParamType getType() const override { return ParamType::UintVec; }

        const char* getTypeName() const override
        {
            static const char* names[] = {"UINT2", "UINT3", "UINT4"};
            if (componentCount >= 2 && componentCount <= 4)
                return names[componentCount - 2];
            return "UINTVEC";
        }

        bool hasChanged() const override
        {
            for (uint32_t i = 0; i < componentCount; i++)
                if (value[i] != defaultValue[i])
                    return true;
            return false;
        }

        void resetToDefault() override
        {
            for (uint32_t i = 0; i < componentCount; i++)
                value[i] = defaultValue[i];
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            std::vector<std::pair<std::string, std::string>> result;
            for (uint32_t i = 0; i < componentCount; i++)
                result.push_back({name + "[" + std::to_string(i) + "]", std::to_string(value[i])});
            return result;
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<UintVecParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->componentCount = componentCount;
            for (uint32_t i = 0; i < 4; i++)
            {
                p->value[i] = value[i];
                p->defaultValue[i] = defaultValue[i];
                p->minValue[i] = minValue[i];
                p->maxValue[i] = maxValue[i];
            }
            p->step = step;
            return p;
        }
    };

    // Bool parameter
    class BoolParam : public EffectParam
    {
    public:
        bool value = false;
        bool defaultValue = false;

        ParamType getType() const override { return ParamType::Bool; }
        const char* getTypeName() const override { return "BOOL"; }

        bool hasChanged() const override { return value != defaultValue; }

        void resetToDefault() override { value = defaultValue; }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {{"", value ? "true" : "false"}};
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<BoolParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value = value;
            p->defaultValue = defaultValue;
            return p;
        }
    };

    // Helper to clone a vector of params
    inline std::vector<std::unique_ptr<EffectParam>> cloneParams(const std::vector<std::unique_ptr<EffectParam>>& params)
    {
        std::vector<std::unique_ptr<EffectParam>> result;
        result.reserve(params.size());
        for (const auto& p : params)
            result.push_back(p->clone());
        return result;
    }

} // namespace vkBasalt

#endif // EFFECT_PARAM_HPP_INCLUDED
