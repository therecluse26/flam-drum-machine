#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/PerChannelMixer.h"

namespace flam {

/**
 * @brief EQ Editor with 10-band graphic EQ controls
 *
 * Displays 10 vertical sliders for ISO standard frequencies:
 * 31Hz, 62Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz
 *
 * Used in CallOutBox popovers from FX buttons.
 */
class EQEditorComponent : public juce::Component
{
public:
    EQEditorComponent(PerChannelMixer& mixer, int channelIdx, bool isMaster = false)
        : mixerRef(mixer)
        , channelIndex(channelIdx)
        , isMasterEQ(isMaster)
    {
        // Create 10 band sliders
        for (int i = 0; i < 10; ++i)
        {
            auto& slider = bandSliders[i];
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
            slider.setRange(-12.0, 12.0, 0.1);
            slider.setValue(0.0);
            slider.setTextValueSuffix(" dB");
            slider.onValueChange = [this, i] { onBandChanged(i); };

            // Labels for each band
            auto& label = bandLabels[i];
            addAndMakeVisible(label);
            label.setText(getBandFrequencyLabel(i), juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId, juce::Colours::white);
        }

        // Update UI from mixer state
        updateFromMixer();

        setSize(600, 300);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff2a2a2a));

        // Title
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText("10-Band Graphic EQ", getLocalBounds().removeFromTop(30), juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);

        // Title takes top space (handled in paint())
        bounds.removeFromTop(30);
        bounds.removeFromTop(10);

        // 10 sliders side by side
        const int sliderWidth = bounds.getWidth() / 10;
        for (int i = 0; i < 10; ++i)
        {
            auto sliderBounds = bounds.removeFromLeft(sliderWidth).reduced(5);

            // Label at top
            bandLabels[i].setBounds(sliderBounds.removeFromTop(20));

            // Slider fills remaining space
            bandSliders[i].setBounds(sliderBounds);
        }
    }

private:
    void onBandChanged(int bandIndex)
    {
        float gainDb = static_cast<float>(bandSliders[bandIndex].getValue());

        if (isMasterEQ)
            mixerRef.setMasterEQBandGain(bandIndex, gainDb);
        else
            mixerRef.setChannelEQBandGain(channelIndex, bandIndex, gainDb);
    }

    void updateFromMixer()
    {
        for (int i = 0; i < 10; ++i)
        {
            float gain = isMasterEQ ? mixerRef.getMasterEQBandGain(i)
                                     : mixerRef.getChannelEQBandGain(channelIndex, i);
            bandSliders[i].setValue(gain, juce::dontSendNotification);
        }
    }

    juce::String getBandFrequencyLabel(int bandIndex) const
    {
        const juce::String labels[10] = {
            "31Hz", "62Hz", "125Hz", "250Hz", "500Hz",
            "1kHz", "2kHz", "4kHz", "8kHz", "16kHz"
        };
        return labels[bandIndex];
    }

    PerChannelMixer& mixerRef;
    int channelIndex;
    bool isMasterEQ;

    juce::Slider bandSliders[10];
    juce::Label bandLabels[10];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQEditorComponent)
};

} // namespace flam
