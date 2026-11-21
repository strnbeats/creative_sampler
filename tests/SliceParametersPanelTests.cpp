#include "SliceParametersPanelTests.h"
#include "../src/ui/SliceParametersPanel.h"

namespace
{
class PanelTests final : public juce::UnitTest
{
public:
    PanelTests() : UnitTest("Slice parameters panel") {}
    void runTest() override
    {
        beginTest("Controls follow selection, edits, split, deletion, reset and replacement");
        juce::ScopedJuceInitialiser_GUI initialise;
        SliceEngine model;
        SliceId selected = 0;
        SliceParametersPanel panel(model, [&] { return selected; });
        panel.setSize(596, 116);
        auto* pan = dynamic_cast<juce::Slider*>(panel.getChildComponent(5));
        auto* release = dynamic_cast<juce::Slider*>(panel.getChildComponent(3));
        expect(pan && release);
        if (!pan || !release) return;
        panel.refresh();
        expect(!pan->isVisible());
        expect(model.setSample(1, 2048));
        selected = model.slices().front().id;
        const auto first = selected;
        panel.refresh();
        expect(pan->isVisible());
        release->setValue(0.75, juce::sendNotificationSync);
        pan->setValue(-0.5, juce::sendNotificationSync);
        expectWithinAbsoluteError(model.find(first)->parameters.envelope.releaseSeconds, 0.75f, 0.0001f);
        const auto child = model.splitSlice(first, 1024);
        expect(child.has_value());
        if (!child) return;
        selected = *child;
        panel.refresh();
        expectWithinAbsoluteError(pan->getValue(), -0.5, 0.0001);
        pan->setValue(0.8, juce::sendNotificationSync);
        selected = first;
        panel.refresh();
        expectWithinAbsoluteError(pan->getValue(), -0.5, 0.0001);
        expect(model.deleteSlice(first));
        selected = *child;
        panel.refresh();
        expectWithinAbsoluteError(pan->getValue(), 0.8, 0.0001);
        expect(model.resetSlices());
        selected = model.slices().front().id;
        panel.refresh();
        expectWithinAbsoluteError(pan->getValue(), 0.0, 0.0001);
        pan->onDragStart();
        expect(model.setSample(2, 2048));
        selected = model.slices().front().id;
        panel.refresh();
        pan->setValue(1, juce::sendNotificationSync);
        pan->onDragEnd();
        expectWithinAbsoluteError(model.find(selected)->parameters.pan, 0.0f, 0.0001f);
        expectWithinAbsoluteError(pan->getValue(), 0.0, 0.0001);
    }
};
}

juce::UnitTest* getSliceParametersPanelTest()
{
    static PanelTests tests;
    return &tests;
}
