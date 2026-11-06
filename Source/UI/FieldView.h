#pragma once

#include "Theme.h"
#include "../Field/FieldSession.h"

#include <functional>

namespace kumamp
{
/**
    CAMPO: the chain as a field of elastic forms, on its own sound (see FieldSession).

    Dragging a form morphs its block, pulling the handle on its edge makes it oscillate,
    overlapping two fuses them, and the two strings below set how fast everything moves
    and how much fused blocks feed each other. A discovered sound is saved as a token on
    the field: a click brings it back, dragging one towards another blends them live.
*/
class FieldView final : public juce::Component, private juce::Timer
{
public:
    explicit FieldView (FieldSession& session);
    ~FieldView() override;

    std::function<void (const juce::String&)> onMessage;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // The gestures, in component coordinates; the mouse and the tests go through here.
    void press (juce::Point<float> position, bool popupClick = false);
    void dragTo (juce::Point<float> position);
    void release();
    /** Abandons a drag and puts the field back as it was. */
    void cancelDrag();
    void toggleAt (juce::Point<float> position);

    juce::Point<float> getBlobCentre (int blob) const;
    juce::Point<float> getBlobHandle (int blob) const;
    juce::Point<float> getTokenCentre (int tone) const;
    juce::Point<float> getStringPoint (bool fusionString) const;
    juce::String getHeldText() const { return heldText; }

    /** Advances the drawing as the timer would. */
    void advanceAnimation (double seconds);

private:
    enum class Target { None, Blob, Handle, Token, Tide, Fusion, Empty };

    void timerCallback() override;

    Target targetAt (juce::Point<float> position, int& index) const;
    juce::Rectangle<float> tokenBounds (int tone) const;
    juce::Path contour (int blob, float depth) const;
    void paintMembrane (juce::Graphics&);
    void paintRipples (juce::Graphics&);
    void paintThread (juce::Graphics&);
    void paintBlob (juce::Graphics&, int blob);
    void paintMandala (juce::Graphics&, int blob);
    void paintFusions (juce::Graphics&);
    void paintFusionLabels (juce::Graphics&);
    void paintToken (juce::Graphics&, int tone);
    void paintString (juce::Graphics&, juce::Rectangle<float> area, const juce::String& name, float value, bool active);
    void paintLabel (juce::Graphics&, juce::Point<float> at, const juce::String& text, bool mono);
    void rebuildPaper();
    void stringTo (bool fusionString, float x);
    void message (const juce::String&);

    FieldSession& session;

    juce::Rectangle<float> field, tideArea, fusionArea;
    juce::Image paper;
    juce::TextButton saveButton { "Guardar tono" }, mutateButton { "Mutar" },
                     resetButton { "Desde Grid" }, pushButton { "Pasar al Grid" };

    double time = 0.0, flow = 0.0;
    float level = 0.0f, stringBuzz = 0.0f;

    Target held = Target::None, hovered = Target::None;
    int heldIndex = -1, hoveredIndex = -1;
    bool moved = false;
    juce::Point<float> grabStart, pointer, startPosition;
    juce::ValueTree before;
    int blendWith = -1;
    juce::String heldText;
};
} // namespace kumamp
