#pragma once
#include "SliceParameterBinding.h"
#include <juce_gui_basics/juce_gui_basics.h>

class SliceParametersPanel final : public juce::Component
{
public:
    SliceParametersPanel(SliceEngine&, std::function<SliceId()> selection);
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    struct Dial final : juce::Slider
    {
        SliceField field {};
        juce::Colour accent;
        std::optional<std::pair<std::uint64_t, SliceId>> dragTarget;
        void paint(juce::Graphics&) override;
        void mouseEnter(const juce::MouseEvent& e) override { Slider::mouseEnter(e); repaint(); }
        void mouseExit(const juce::MouseEvent& e) override { Slider::mouseExit(e); repaint(); }
    };
    SliceEngine& model;
    std::function<SliceId()> selection;
    std::array<Dial, 7> dials;
    std::uint64_t sample = 0, revision = 0;
    SliceId selected = 0;
};
