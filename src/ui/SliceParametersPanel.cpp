#include "SliceParametersPanel.h"

namespace
{
struct DialSpec { SliceField field; const char* name; double low, high; juce::uint32 colour; };
constexpr DialSpec specs[] {
    {SliceField::attack, "Attack", 0, 10, 0xff79dadc}, {SliceField::decay, "Decay", 0, 10, 0xff8abaf1},
    {SliceField::sustain, "Sustain", 0, 1, 0xffb39ded}, {SliceField::release, "Release", 0, 10, 0xffda9bd1},
    {SliceField::volume, "Volume", 0, 2, 0xff81d9ce}, {SliceField::pan, "Pan", -1, 1, 0xff94bdea},
    {SliceField::pitch, "Pitch", -24, 24, 0xffbd9fe2} };

juce::String format(SliceField field, double v)
{
    if (field == SliceField::pitch)
        return (v > 0 ? "+" : "") + juce::String(v, 1) + " st";
    if (field == SliceField::pan)
        return std::abs(v) < 0.0001 ? "Center" : juce::String(v < 0 ? "L " : "R ") + juce::String(juce::roundToInt(std::abs(v)*100)) + "%";
    if (field == SliceField::sustain || field == SliceField::volume)
        return juce::String(juce::roundToInt(v*100)) + "%";
    return v < 1 ? juce::String(juce::roundToInt(v*1000)) + " ms" : juce::String(v, 2) + " s";
}
}

SliceParametersPanel::SliceParametersPanel(SliceEngine& m, std::function<SliceId()> s)
    : model(m), selection(std::move(s))
{
    SliceParameters defaults;
    for (size_t i = 0; i < dials.size(); ++i)
    {
        auto& d = dials[i];
        const auto spec = specs[i];
        d.field = spec.field;
        d.accent = juce::Colour(spec.colour);
        d.setName(spec.name);
        d.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        d.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        d.setRange(spec.low, spec.high);
        if (spec.field == SliceField::attack || spec.field == SliceField::decay
            || spec.field == SliceField::release)
            d.setSkewFactorFromMidPoint(0.45);
        d.setDoubleClickReturnValue(true, sliceField(defaults, spec.field));
        d.textFromValueFunction = [spec](double v) { return format(spec.field, v); };
        d.onDragStart = [this, &d] { d.dragTarget = std::make_pair(sample, selected); };
        d.onDragEnd = [&d] { d.dragTarget.reset(); };
        d.onValueChange = [this, &d]
        {
            if ((!d.dragTarget || *d.dragTarget == std::make_pair(sample, selected))
                && selected != 0 && selected == selection())
                editSliceField(model, sample, selected, d.field, static_cast<float>(d.getValue()));
            revision = 0;
            refresh();
        };
        addChildComponent(d);
    }
}

void SliceParametersPanel::refresh()
{
    const auto id = selection();
    if (id == selected && sample == model.sampleId() && revision == model.revision())
        return;
    const auto* slice = model.find(id);
    selected = slice ? id : 0;
    sample = model.sampleId();
    revision = model.revision();
    for (auto& d : dials)
    {
        d.setVisible(slice != nullptr);
        if (slice)
        {
            auto p = slice->parameters;
            d.setValue(sliceField(p, d.field), juce::dontSendNotification);
        }
    }
    repaint();
}

void SliceParametersPanel::resized()
{
    auto area = getLocalBounds().reduced(8, 4).withTrimmedTop(24);
    const int width = (area.getWidth()-18)/7;
    for (size_t i = 0; i < dials.size(); ++i)
    {
        if (i == 4) area.removeFromLeft(18);
        dials[i].setBounds(area.removeFromLeft(width));
    }
}

void SliceParametersPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff232630));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 9);
    g.setColour(juce::Colour(0xffdedce8));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    if (!selected)
    {
        g.drawText("Select a slice to edit its parameters", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const auto split = dials[4].getX();
    g.drawText("Amplitude envelope", 16, 4, split-20, 22, juce::Justification::centredLeft);
    g.drawText("Slice parameters", split, 4, getWidth()-split, 22, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff40434f));
    g.drawVerticalLine(split-9, 10, static_cast<float>(getHeight()-10));
}

void SliceParametersPanel::Dial::paint(juce::Graphics& g)
{
    const auto box = getLocalBounds();
    const auto cx = box.toFloat().getCentreX(), cy = 24.0f;
    constexpr float r = 18, start = -2.35f, span = 4.7f;
    const auto angle = start + span * static_cast<float>(valueToProportionOfLength(getValue()));
    const auto arc = [&](float a, float b, juce::Colour c)
    {
        juce::Path path;
        path.addCentredArc(cx, cy, r, r, 0, a, b, true);
        g.setColour(c);
        g.strokePath(path, juce::PathStrokeType(2.5f));
    };
    arc(start, start+span, juce::Colour(0xff454955));
    const float origin = getMinimum() < 0 ? 0.0f : start;
    arc(std::min(origin, angle), std::max(origin, angle), accent);
    g.setColour(isMouseOverOrDragging() ? accent.darker(0.75f) : juce::Colour(0xff323644));
    g.fillEllipse(cx-14, cy-14, 28, 28);
    g.setColour(accent);
    g.drawLine(cx+5*std::sin(angle), cy-5*std::cos(angle), cx+13*std::sin(angle), cy-13*std::cos(angle), 2);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(getTextFromValue(getValue()), 0, 44, box.getWidth(), 19, juce::Justification::centred);
    g.setColour(juce::Colour(0xffdedce8));
    g.drawText(getName(), 0, 64, box.getWidth(), 19, juce::Justification::centred);
}
