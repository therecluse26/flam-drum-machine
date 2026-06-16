// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FlamLookAndFeel.h"
#include "ChannelStripComponent.h"
#include "MasterChannelStripComponent.h"
#include "../Core/Mixer.h"

namespace flam {

/**
 * @brief Main mixer panel with scrollable channel strips
 *
 * Container for all channel strips + master section.
 * Dynamically creates channel strips based on mixer configuration.
 */
class MixerPanel : public juce::Component
{
public:
    MixerPanel(Mixer& mixer)
        : mixerRef(mixer)
    {
        // Create master channel strip (full featured with all FX)
        masterChannelStrip = std::make_unique<MasterChannelStripComponent>(mixer);
        addAndMakeVisible(masterChannelStrip.get());

        // Initialize with default 2 channels if mixer not configured
        updateChannelStrips();
    }

    ~MixerPanel() override = default;

    /**
     * @brief Refresh channel strips to match current mixer configuration
     *
     * Call this when kit loads with different channel count.
     */
    void refreshChannels()
    {
        updateChannelStrips();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(FlamColors::Surface));

        // Section header with gradient for depth
        auto headerBounds = getLocalBounds().removeFromTop(40).toFloat();
        auto base = juce::Colour(FlamColors::Surface);
        FlamLookAndFeel::paintGradientFill(g, headerBounds,
            base.brighter(0.10f), base.brighter(0.02f));

        auto dividerBounds = headerBounds;
        g.setColour(juce::Colour(FlamColors::BorderSubtle));
        g.fillRect(dividerBounds.removeFromBottom(1.0f));

        g.setColour(juce::Colour(FlamColors::TextSecondary));
        g.setFont(FlamType::labelBold());
        g.drawText("MIXER", getLocalBounds().removeFromTop(40),
                   juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();

        // Title area
        bounds.removeFromTop(40);

        bounds.reduce(10, 10);

        // Master channel strip on the right (full featured, wider than regular channels)
        auto masterBounds = bounds.removeFromRight(130);
        if (masterChannelStrip)
            masterChannelStrip->setBounds(masterBounds);

        bounds.removeFromRight(10);  // Spacing between channels and master

        // Channel strips (scrollable)
        if (viewport == nullptr)
        {
            viewport = std::make_unique<juce::Viewport>();
            addAndMakeVisible(viewport.get());
        }

        // Connect container every time — fixes the initial render path where the
        // constructor calls updateChannelStrips() before the viewport exists.
        if (channelStripsContainer != nullptr)
            viewport->setViewedComponent(channelStripsContainer.get(), false);

        viewport->setBounds(bounds);

        // Layout channel strips in content component
        if (channelStripsContainer != nullptr)
        {
            const int numChannels = channelStrips.size();
            if (numChannels == 0)
                return;

            const int viewportWidth = bounds.getWidth();

            // Scale strip width to fill available space, bounded between min and max.
            // With many channels the viewport will scroll; with few channels the strips
            // grow to fill the view and are centred as a group.
            constexpr int kMinStripWidth = 80;
            constexpr int kMaxStripWidth = 140;
            const int stripWidth = juce::jlimit(kMinStripWidth, kMaxStripWidth,
                                                viewportWidth / numChannels);
            const int totalStripsWidth = numChannels * stripWidth;

            // Container is at least as wide as the viewport so the background
            // is always filled; wider when there are many channels (enables scroll).
            const int containerWidth = juce::jmax(totalStripsWidth, viewportWidth);
            const int startX = (containerWidth - totalStripsWidth) / 2;

            channelStripsContainer->setSize(containerWidth, bounds.getHeight());

            for (int i = 0; i < numChannels; ++i)
                channelStrips[i]->setBounds(startX + i * stripWidth, 0, stripWidth, bounds.getHeight());
        }
    }

    /**
     * @brief Update channel strips based on mixer configuration
     *
     * Call this when kit loads or channel count changes.
     */
    void updateChannelStrips()
    {
        // Clear existing strips
        channelStrips.clear();

        if (channelStripsContainer != nullptr)
            channelStripsContainer.reset();

        // Create new container
        channelStripsContainer = std::make_unique<juce::Component>();

        // Create channel strips
        int numChannels = mixerRef.getNumChannels();

        // If mixer not configured, show 2 default channels
        if (numChannels == 0)
            numChannels = 2;

        for (int i = 0; i < numChannels; ++i)
        {
            auto* strip = new ChannelStripComponent(mixerRef, i);
            channelStrips.add(strip);
            channelStripsContainer->addAndMakeVisible(strip);
        }

        // Set up viewport
        if (viewport != nullptr)
            viewport->setViewedComponent(channelStripsContainer.get(), false);

        resized();
    }

private:
    Mixer& mixerRef;

    // Channel strips (scrollable)
    std::unique_ptr<juce::Viewport> viewport;
    std::unique_ptr<juce::Component> channelStripsContainer;
    juce::OwnedArray<ChannelStripComponent> channelStrips;

    // Master channel strip (full featured with all FX)
    std::unique_ptr<MasterChannelStripComponent> masterChannelStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanel)
};

} // namespace flam
