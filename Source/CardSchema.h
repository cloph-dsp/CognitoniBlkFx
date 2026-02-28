#pragma once

#include <JuceHeader.h>
#include <array>

namespace CardSchema
{
    enum class CardId : int
    {
        autoHarm = 0,
        contrast = 1,
        saws = 2
    };

    struct ParamIds
    {
        const char* amount;
        const char* type;
        const char* freqA;
        const char* freqB;
        const char* bypass;
        const char* wetDry;
    };

    enum class Key : int
    {
        amount = 0,
        type,
        freqA,
        freqB,
        bypass,
        wetDry
    };

    constexpr std::array<ParamIds, 3> all
    {
        ParamIds {
            "autoHarmTargetIntensity",
            "autoHarmType",
            "autoHarmMinFreqHz",
            "autoHarmMaxFreqHz",
            "autoHarmBypass",
            "autoHarmWetDry"
        },
        ParamIds {
            "contrastAmount",
            "contrastType",
            "contrastMinFreqHz",
            "contrastMaxFreqHz",
            "contrastBypass",
            "contrastWetDry"
        },
        ParamIds {
            "sawsAmount",
            "sawsType",
            "sawsMinFreqHz",
            "sawsMaxFreqHz",
            "sawsBypass",
            "sawsWetDry"
        }
    };

    constexpr const ParamIds& paramsFor (CardId id)
    {
        return all[static_cast<int> (id)];
    }

    constexpr const char* cardName (CardId id)
    {
        switch (id)
        {
            case CardId::autoHarm: return "AutoHarm";
            case CardId::contrast: return "Contrast";
            case CardId::saws: return "Saws";
            default: return "Card";
        }
    }

    constexpr const char* keyName (Key key)
    {
        switch (key)
        {
            case Key::amount: return "amount";
            case Key::type: return "type";
            case Key::freqA: return "freqA";
            case Key::freqB: return "freqB";
            case Key::bypass: return "bypass";
            case Key::wetDry: return "wetDry";
            default: return "unknown";
        }
    }

    inline Key keyFromName (const juce::String& key)
    {
        if (key == "amount") return Key::amount;
        if (key == "type") return Key::type;
        if (key == "freqA") return Key::freqA;
        if (key == "freqB") return Key::freqB;
        if (key == "bypass") return Key::bypass;
        return Key::wetDry;
    }

    constexpr const char* paramIdFor (CardId id, Key key)
    {
        const auto& p = paramsFor (id);
        switch (key)
        {
            case Key::amount: return p.amount;
            case Key::type: return p.type;
            case Key::freqA: return p.freqA;
            case Key::freqB: return p.freqB;
            case Key::bypass: return p.bypass;
            case Key::wetDry: return p.wetDry;
            default: return p.amount;
        }
    }
}
