#include "FieldView.h"

#include <algorithm>
#include <cmath>

namespace kumamp
{
namespace
{
    const juce::Colour kPaper { 0xffe4ddc8 };
    const juce::Colour kRed { 0xffd74528 };
    constexpr float kTokenWidth = 62.0f, kTokenHeight = 54.0f;
    constexpr float kDragThreshold = 3.0f;
    constexpr int kMaxTokens = 10;
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
    constexpr float kHandleReach = 1.0f;   // how far the edge goes at full stretch, in radii

    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

    juce::Image makeGrain()
    {
        juce::Image tile (juce::Image::ARGB, 96, 96, true);
        juce::uint32 seed = 17;
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 96; ++x)
            {
                seed = seed * 1664525u + 1013904223u;
                const auto value = (juce::uint8) (seed >> 24);
                tile.setPixelAt (x, y, juce::Colour (value, value, value, (juce::uint8) (value > 180 ? 8 : 16)));
            }
        return tile;
    }
} // namespace

FieldView::FieldView (FieldSession& s) : session (s)
{
    setWantsKeyboardFocus (true);
    setTitle ("Campo");
    setDescription (utf8 ("La cadena como formas el\xc3\xa1sticas, con su propio sonido."));

    saveButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    saveButton.setTooltip ("Guarda este sonido como una ficha en el campo");
    saveButton.onClick = [this]
    {
        if ((int) session.getTones().size() >= kMaxTokens)
            message ("Hay " + juce::String (kMaxTokens) + utf8 (" fichas: borr\xc3\xa1 una con clic derecho"));
        else if (! session.saveTone())
            message ("Error: no se pudo guardar el tono");
        repaint();
    };

    mutateButton.setTooltip (utf8 ("Otra relaci\xc3\xb3n entre las formas y los par\xc3\xa1metros, y las formas movidas al azar"));
    mutateButton.onClick = [this] { session.mutate(); repaint(); };

    resetButton.setTooltip (utf8 ("Vuelve a copiar los valores del Grid y borra lo hecho ac\xc3\xa1 (las fichas guardadas quedan)"));
    resetButton.onClick = [this] { session.resetFromGrid(); repaint(); };

    pushButton.setTooltip ("Copia estos valores al Grid. Las fusiones no pasan: no existen en el Grid");
    pushButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::QuestionIcon, "Pasar al Grid",
            "Reemplaza los valores actuales del Grid por los del Campo, y no se puede deshacer. Las oscilaciones y las fusiones no pasan.",
            "Pasar", "Cancelar", this,
            juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<FieldView> (this)] (int result)
            {
                if (safe != nullptr && result != 0)
                    safe->message ("Valores de " + juce::String (safe->session.pushToGrid()) + " bloques copiados al Grid");
            }));
    };

    for (auto* button : { &saveButton, &mutateButton, &resetButton, &pushButton })
        addAndMakeVisible (*button);
}

FieldView::~FieldView()
{
    stopTimer();
}

void FieldView::message (const juce::String& text)
{
    if (onMessage)
        onMessage (text);
}

void FieldView::visibilityChanged()
{
    if (isVisible())
    {
        startTimerHz (24);
    }
    else
    {
        stopTimer();
        if (held != Target::None)
            release();
    }
}

void FieldView::timerCallback()
{
    advanceAnimation (1.0 / 24.0);
    repaint();
}

void FieldView::advanceAnimation (double seconds)
{
    session.setAspect (field.getHeight() > 1.0f ? field.getWidth() / field.getHeight() : 2.0f);
    const float tide = session.getTide();
    time += seconds;
    flow += seconds * (0.05 + 0.75 * (double) (tide * tide));
    level += 0.35f * (session.getEnvelope() - level);
    stringBuzz *= 0.9f;
}

void FieldView::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto strip = area.removeFromBottom (58);
    area.removeFromBottom (10);
    field = area.toFloat();

    auto buttons = strip.removeFromRight (560).withSizeKeepingCentre (560, 40);
    const int w = 134;
    for (auto* button : { &pushButton, &resetButton, &mutateButton, &saveButton })
    {
        button->setBounds (buttons.removeFromRight (w));
        buttons.removeFromRight (8);
    }
    strip.removeFromRight (20);
    const int stringWidth = (strip.getWidth() - 20) / 2;
    tideArea = strip.removeFromLeft (stringWidth).toFloat();
    strip.removeFromLeft (20);
    fusionArea = strip.toFloat();

    rebuildPaper();
    session.setAspect (field.getHeight() > 1.0f ? field.getWidth() / field.getHeight() : 2.0f);
}

void FieldView::rebuildPaper()
{
    if (field.isEmpty())
        return;
    paper = juce::Image (juce::Image::RGB, juce::roundToInt (field.getWidth()), juce::roundToInt (field.getHeight()), false);
    juce::Graphics g (paper);
    g.fillAll (kPaper);
    g.setTiledImageFill (makeGrain(), 0, 0, 1.0f);
    g.fillAll();
}

//==============================================================================
juce::Point<float> FieldView::getBlobCentre (int b) const
{
    const auto& blob = session.getBlobs()[(size_t) b];
    return field.getPosition() + juce::Point<float> (field.getWidth() * blob.x, field.getHeight() * blob.y);
}

juce::Point<float> FieldView::getBlobHandle (int b) const
{
    const auto& blob = session.getBlobs()[(size_t) b];
    const float r = session.getBlobRadius (b) * field.getHeight() * (1.04f + kHandleReach * blob.stretch);
    return getBlobCentre (b) + juce::Point<float> (r * std::cos (blob.angle), r * std::sin (blob.angle));
}

juce::Rectangle<float> FieldView::tokenBounds (int tone) const
{
    return juce::Rectangle<float> (kTokenWidth, kTokenHeight)
        .withPosition (field.getRight() - 10.0f - (float) (tone + 1) * (kTokenWidth + 8.0f) + 8.0f, field.getY() + 10.0f);
}

juce::Point<float> FieldView::getTokenCentre (int tone) const
{
    if (held == Target::Token && heldIndex == tone)
        return tokenBounds (tone).getCentre() + (pointer - grabStart);
    return tokenBounds (tone).getCentre();
}

juce::Point<float> FieldView::getStringPoint (bool fusionString) const
{
    const auto& area = fusionString ? fusionArea : tideArea;
    const auto line = area.withTrimmedLeft (74.0f).withTrimmedRight (12.0f);
    return { line.getX() + line.getWidth() * (fusionString ? session.getFusion() : session.getTide()), line.getCentreY() };
}

juce::Path FieldView::contour (int b, float depth) const
{
    const auto& blob = session.getBlobs()[(size_t) b];
    const auto centre = getBlobCentre (b);
    const float radius = session.getBlobRadius (b) * field.getHeight();
    const float liveness = blob.on ? 1.0f : 0.25f;
    const float wobble = (0.05f + 0.13f * blob.stretch + 0.10f * level) * liveness;
    const float t = (float) flow * kTwoPi + (float) b * 1.7f;
    const float lobes = (float) (3 + (int) blob.store % 4);

    juce::Path path;
    for (int i = 0; i <= 48; ++i)
    {
        const float a = (float) i * kTwoPi / 48.0f;
        const float pull = std::pow (std::abs (std::cos (a - blob.angle)), 3.0f) * (std::cos (a - blob.angle) > 0.0f ? 1.0f : 0.15f);
        const float r = radius * depth * (1.0f + wobble * std::sin (lobes * a + t + depth * 5.0f)
                                               + (wobble * 0.5f) * std::cos ((lobes + 2.0f) * a - t * 1.3f - depth * 3.0f)
                                               + kHandleReach * blob.stretch * pull);
        const juce::Point<float> p (centre.x + r * std::cos (a), centre.y + r * std::sin (a));
        if (i == 0) path.startNewSubPath (p);
        else        path.lineTo (p);
    }
    path.closeSubPath();
    return path;
}

FieldView::Target FieldView::targetAt (juce::Point<float> position, int& index) const
{
    index = -1;
    if (tideArea.contains (position))   return Target::Tide;
    if (fusionArea.contains (position)) return Target::Fusion;

    for (int i = 0; i < std::min ((int) session.getTones().size(), kMaxTokens); ++i)
        if (tokenBounds (i).contains (position))
        {
            index = i;
            return Target::Token;
        }
    if (! field.contains (position))
        return Target::None;

    const auto& blobs = session.getBlobs();
    for (int i = (int) blobs.size(); --i >= 0;)
        if (getBlobHandle (i).getDistanceFrom (position) < 13.0f)
        {
            index = i;
            return Target::Handle;
        }
    for (int i = (int) blobs.size(); --i >= 0;)
        if (contour (i, 1.0f).contains (position))
        {
            index = i;
            return Target::Blob;
        }
    return Target::Empty;
}

//==============================================================================
void FieldView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (field.isEmpty())
        return;

    {
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (field.getSmallestIntegerContainer());
        g.drawImageAt (paper, juce::roundToInt (field.getX()), juce::roundToInt (field.getY()));
        paintMembrane (g);
        paintRipples (g);
        paintThread (g);

        const auto& blobs = session.getBlobs();
        for (int i = 0; i < (int) blobs.size(); ++i)
            if (! blobs[(size_t) i].tanpura)
                paintBlob (g, i);
        paintFusions (g);

        // The mandala goes on top of what it fuses with, and the readings on top of it.
        for (int i = 0; i < (int) blobs.size(); ++i)
            if (blobs[(size_t) i].tanpura)
                paintBlob (g, i);
        paintFusionLabels (g);

        g.setColour (theme::ink);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("CAMPO DE SE\xc3\x91" "AL")), field.reduced (14.0f, 10.0f).removeFromTop (18.0f), juce::Justification::centredLeft);
        for (int i = 0; i <= 80; ++i)
        {
            const float x = field.getX() + 14.0f + (float) i * (field.getWidth() - 28.0f) / 80.0f;
            g.fillRect (x, field.getBottom() - (i % 5 == 0 ? 12.0f : 8.0f), 1.0f, i % 5 == 0 ? 8.0f : 4.0f);
        }

        const char* hint = "Arrastr\xc3\xa1 una forma para transformar su efecto. Tir\xc3\xa1 del cuadradito para que oscile. Superpon\xc3\xa9 dos para fusionarlas.";
        if (! session.isChainReady())
            hint = "Cargando la cadena...";
        else if (session.getTones().size() >= 2)
            hint = "Clic en una ficha la recupera; arrastrala hacia otra para mezclarlas. Doble clic en una forma la apaga.";
        g.setColour (theme::ink.withAlpha (0.75f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (hint)), field.reduced (14.0f, 16.0f).removeFromBottom (20.0f), juce::Justification::centredLeft);

        for (int i = 0; i < std::min ((int) session.getTones().size(), kMaxTokens); ++i)
            paintToken (g, i);

        if (heldText.isNotEmpty())
            paintLabel (g, pointer.translated (18.0f, -30.0f), heldText, true);
    }

    paintString (g, tideArea, "MAREA", session.getTide(), held == Target::Tide || hovered == Target::Tide);
    paintString (g, fusionArea, juce::String (juce::CharPointer_UTF8 ("FUSI\xc3\x93N")), session.getFusion(), held == Target::Fusion || hovered == Target::Fusion);
}

void FieldView::paintMembrane (juce::Graphics& g)
{
    // A sheet of lines that the forms press down and the guitar shakes.
    const auto& blobs = session.getBlobs();
    const float w = field.getWidth(), h = field.getHeight();
    g.setColour (theme::ink.withAlpha (0.16f));
    for (float y = 8.0f; y < h; y += 18.0f)
    {
        juce::Path line;
        for (int i = 0; i <= 50; ++i)
        {
            const float x = w * (float) i / 50.0f;
            float offset = (1.6f + 5.0f * level) * std::sin (x * 0.02f + y * 0.04f + (float) flow * kTwoPi);
            for (int b = 0; b < (int) blobs.size(); ++b)
            {
                const auto c = getBlobCentre (b) - field.getPosition();
                const float reach = session.getBlobRadius (b) * h * 1.9f;
                const float dx = x - c.x, dy = y - c.y;
                const float d2 = (dx * dx + dy * dy) / (reach * reach);
                if (d2 < 5.0f)
                    offset += (dy >= 0.0f ? 1.0f : -1.0f) * reach * 0.34f * std::exp (-d2);
            }
            const juce::Point<float> p (field.getX() + x, field.getY() + y + offset);
            if (i == 0) line.startNewSubPath (p);
            else        line.lineTo (p);
        }
        g.strokePath (line, juce::PathStrokeType (0.8f));
    }
}

void FieldView::paintRipples (juce::Graphics& g)
{
    // Waves leaving each live form: where two meet, they interfere.
    const auto& blobs = session.getBlobs();
    for (int b = 0; b < (int) blobs.size(); ++b)
    {
        const auto& blob = blobs[(size_t) b];
        if (! blob.on)
            continue;
        auto colour = theme::categoryColour (blob.type).darker (0.2f);
        const auto centre = getBlobCentre (b);
        const float radius = session.getBlobRadius (b) * field.getHeight();
        const float scroll = (float) (flow * (0.6 + 2.0 * (double) blob.stretch)) + (float) b * 0.31f;
        const float frac = scroll - std::floor (scroll);
        const int count = 4 + juce::roundToInt (blob.stretch * 3.0f + level * 2.0f);
        for (int i = 0; i < count; ++i)
        {
            const float u = ((float) i + frac) / (float) count;
            const float r = radius * (1.1f + 2.2f * u);
            juce::Path ring;
            for (int s = 0; s <= 28; ++s)
            {
                const float a = (float) s * kTwoPi / 28.0f;
                const float wave = 1.0f + (0.03f + 0.07f * level) * std::sin (5.0f * a + (float) flow * kTwoPi * 2.0f + u * 6.0f);
                const juce::Point<float> p (centre.x + r * wave * std::cos (a), centre.y + r * wave * std::sin (a));
                if (s == 0) ring.startNewSubPath (p);
                else        ring.lineTo (p);
            }
            ring.closeSubPath();
            if (blob.tanpura)
                colour = juce::Colour::fromHSV (std::fmod ((float) flow * 0.3f + u * 0.5f, 1.0f), 0.8f, 0.8f, 1.0f);
            g.setColour (colour.withAlpha (0.55f * (1.0f - u)));
            g.strokePath (ring, juce::PathStrokeType (1.0f + 1.6f * (1.0f - u)));
        }
    }
}

void FieldView::paintThread (juce::Graphics& g)
{
    // The chain's order: a loose cable from IN to OUT, through every form.
    const auto& blobs = session.getBlobs();
    juce::Path thread;
    juce::Point<float> previous (field.getX(), field.getCentreY());
    thread.startNewSubPath (previous);
    const auto curveTo = [&] (juce::Point<float> next, int index)
    {
        const float sag = field.getHeight() * 0.09f * std::sin ((float) flow * kTwoPi * 0.7f + (float) index);
        const auto mid = (previous + next) * 0.5f;
        thread.quadraticTo (mid.x, mid.y + sag, next.x, next.y);
        previous = next;
    };
    for (int b = 0; b < (int) blobs.size(); ++b)
        curveTo (getBlobCentre (b), b);
    curveTo ({ field.getRight(), field.getCentreY() }, (int) blobs.size());
    g.setColour (theme::ink);
    g.strokePath (thread, juce::PathStrokeType (1.5f));
}

void FieldView::paintBlob (juce::Graphics& g, int b)
{
    const auto& blob = session.getBlobs()[(size_t) b];
    const auto colour = theme::categoryColour (blob.type);
    const auto outer = contour (b, 1.0f);
    if (blob.tanpura)
        paintMandala (g, b);
    const bool active = (held == Target::Blob || held == Target::Handle) ? heldIndex == b
                      : ((hovered == Target::Blob || hovered == Target::Handle) && hoveredIndex == b);

    if (blob.tanpura)
    {
        // Drawn above.
    }
    else if (blob.on)
    {
        // Rings flow towards the centre, through ink, the family's colour, paper and a darker pass.
        const juce::Colour inks[] { theme::ink, colour, kPaper, colour.darker (0.55f) };
        const int rings = 9 + juce::roundToInt (blob.stretch * 7.0f);
        const float scroll = (float) (flow * (1.5 + (double) blob.stretch * 3.0)) + (float) b * 0.37f;
        const float frac = scroll - std::floor (scroll);
        const int first = (int) std::floor (scroll);

        g.setColour (inks[first & 3]);
        g.fillPath (outer);
        for (int i = 0; i < rings; ++i)
        {
            const float depth = 1.0f - ((float) i + frac) / (float) rings;
            g.setColour (inks[(first + i + 1) & 3]);
            g.fillPath (contour (b, std::max (0.02f, depth)));
        }
    }
    else
    {
        g.setColour (theme::ink.withAlpha (0.45f));
        for (const float depth : { 1.0f, 0.66f, 0.33f })
            g.strokePath (contour (b, depth), juce::PathStrokeType (1.0f));
    }

    if (active)
    {
        g.setColour (kPaper);
        g.strokePath (outer, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
        g.setColour (kRed.darker (0.3f));
        g.strokePath (outer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
    }

    g.setColour (theme::ink);
    g.fillRect (juce::Rectangle<float> (9.0f, 9.0f).withCentre (getBlobHandle (b)));

    if (! (held == Target::Blob && heldIndex == b))
        paintLabel (g, getBlobCentre (b).translated (0.0f, session.getBlobRadius (b) * field.getHeight() * 1.12f), blob.name.toUpperCase() + (blob.on ? "" : " OFF"), false);
}

void FieldView::paintMandala (juce::Graphics& g, int b)
{
    // The drone never stops, so its form never settles: petals turning against each other
    // through every colour, inside rings that keep flowing, with sparks going around.
    const auto& blob = session.getBlobs()[(size_t) b];
    const auto centre = getBlobCentre (b);
    const float radius = session.getBlobRadius (b) * field.getHeight();
    const float t = (float) flow;

    if (! blob.on)
    {
        g.setColour (theme::ink.withAlpha (0.45f));
        for (int i = 0; i < 12; ++i)
        {
            juce::Path petal;
            petal.addEllipse (-radius * 0.16f, -radius * 0.96f, radius * 0.32f, radius * 0.96f);
            g.strokePath (petal, juce::PathStrokeType (0.9f), juce::AffineTransform::rotation ((float) i * kTwoPi / 12.0f).translated (centre));
        }
        return;
    }

    const auto hue = [t] (float offset, float saturation = 0.85f, float brightness = 0.95f)
    {
        const float h = t * 0.35f + offset;
        return juce::Colour::fromHSV (h - std::floor (h), saturation, brightness, 1.0f);
    };

    g.setColour (theme::ink);
    g.fillPath (contour (b, 1.0f));

    // Rings, from the outside in.
    const int rings = 7 + juce::roundToInt (blob.stretch * 5.0f);
    const float scroll = t * (2.0f + blob.stretch * 3.0f);
    const float frac = scroll - std::floor (scroll);
    for (int i = 0; i < rings; ++i)
    {
        const float depth = 0.97f - ((float) i + frac) * 0.55f / (float) rings;
        g.setColour (i % 2 == 0 ? hue ((float) i * 0.07f) : theme::ink);
        g.fillPath (contour (b, depth));
    }

    // Three layers of petals, each turning its own way.
    const struct { int count; float reach, width, speed, phase; } layers[] {
        { 16, 0.78f, 0.17f,  0.45f, 0.00f },
        { 12, 0.60f, 0.20f, -0.70f, 0.33f },
        {  8, 0.42f, 0.22f,  1.10f, 0.66f },
    };
    const float breath = 1.0f + 0.06f * std::sin (t * kTwoPi * 1.5f) + 0.12f * level;
    for (const auto& layer : layers)
        for (int i = 0; i < layer.count; ++i)
        {
            const float angle = (float) i * kTwoPi / (float) layer.count + t * kTwoPi * layer.speed;
            const float reach = radius * layer.reach * breath;
            juce::Path petal;
            petal.startNewSubPath (0.0f, 0.0f);
            petal.quadraticTo (-radius * layer.width, -reach * 0.55f, 0.0f, -reach);
            petal.quadraticTo (radius * layer.width, -reach * 0.55f, 0.0f, 0.0f);
            petal.closeSubPath();
            const auto transform = juce::AffineTransform::rotation (angle).translated (centre);
            g.setColour (hue (layer.phase + (float) (i % 4) * 0.08f));
            g.fillPath (petal, transform);
            g.setColour (theme::ink);
            g.strokePath (petal, juce::PathStrokeType (0.8f), transform);
        }

    g.setColour (theme::ink);
    g.fillEllipse (juce::Rectangle<float> (radius * 0.2f, radius * 0.2f).withCentre (centre));
    g.setColour (hue (0.5f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.1f, radius * 0.1f).withCentre (centre));

    // Sparks, further out the louder you play.
    for (int i = 0; i < 18; ++i)
    {
        const float a = (float) i * kTwoPi / 18.0f - t * kTwoPi * 0.3f;
        const float r = radius * (1.18f + 0.14f * std::sin (t * kTwoPi * 3.0f + (float) i * 1.3f) + 0.35f * level);
        g.setColour (hue ((float) i / 18.0f));
        g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ centre.x + r * std::cos (a), centre.y + r * std::sin (a) }));
    }
}

void FieldView::paintFusions (juce::Graphics& g)
{
    const auto& blobs = session.getBlobs();
    for (int a = 0; a < (int) blobs.size(); ++a)
        for (int b = a + 1; b < (int) blobs.size(); ++b)
        {
            const float overlap = session.getOverlap (a, b);
            if (overlap <= 0.0f || ! blobs[(size_t) a].on || ! blobs[(size_t) b].on)
                continue;

            const auto p = getBlobCentre (a), q = getBlobCentre (b);
            {
                // Where they share ground the two print over each other.
                const juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (contour (a, 1.0f));
                g.reduceClipRegion (contour (b, 1.0f));
                g.setColour (kRed);
                g.fillRect (field);
                g.setColour (kPaper);
                const float spacing = 8.0f - 4.0f * overlap;
                const float drift = std::fmod ((float) flow * 40.0f, spacing);
                for (float x = field.getX() - field.getHeight() + drift; x < field.getRight(); x += spacing)
                    g.drawLine (x, field.getY(), x + field.getHeight(), field.getBottom(), 1.4f);
            }

            // Filaments: more of them, and tauter, as they fuse.
            g.setColour (theme::ink);
            const int filaments = 2 + juce::roundToInt (overlap * 6.0f);
            for (int i = 0; i < filaments; ++i)
            {
                const float spread = ((float) i - (float) (filaments - 1) * 0.5f) * 11.0f * (1.2f - overlap);
                const auto mid = (p + q) * 0.5f;
                const auto normal = (q - p).rotatedAboutOrigin (juce::MathConstants<float>::halfPi) / std::max (1.0f, p.getDistanceFrom (q));
                const auto bend = mid + normal * (spread + 8.0f * std::sin ((float) flow * kTwoPi * 2.0f + (float) i));
                juce::Path filament;
                filament.startNewSubPath (p);
                filament.quadraticTo (bend, q);
                g.strokePath (filament, juce::PathStrokeType (0.9f));
            }
        }
}

void FieldView::paintFusionLabels (juce::Graphics& g)
{
    const auto& blobs = session.getBlobs();
    for (int a = 0; a < (int) blobs.size(); ++a)
        for (int b = a + 1; b < (int) blobs.size(); ++b)
            if (const float overlap = session.getOverlap (a, b); overlap > 0.0f && blobs[(size_t) a].on && blobs[(size_t) b].on)
                paintLabel (g, (getBlobCentre (a) + getBlobCentre (b)) * 0.5f,
                            juce::String (juce::CharPointer_UTF8 ("FUSI\xc3\x93N ")) + juce::String (juce::roundToInt (overlap * session.getFusion() * 100.0f)) + " %", true);
}

void FieldView::paintToken (juce::Graphics& g, int tone)
{
    auto area = juce::Rectangle<float> (kTokenWidth, kTokenHeight).withCentre (getTokenCentre (tone));
    const bool active = (held == Target::Token && heldIndex == tone) || (held == Target::None && hovered == Target::Token && hoveredIndex == tone);
    g.setColour (theme::ink);
    g.fillRect (area);
    g.setColour (active || blendWith == tone ? theme::bone : theme::border);
    g.drawRect (area, active || blendWith == tone ? 2.0f : 1.0f);

    // A small signature, different for each one.
    const auto& name = session.getTones()[(size_t) tone].name;
    const auto centre = area.withTrimmedBottom (16.0f).getCentre();
    const float h = (float) (((juce::uint32) name.hashCode() * 2654435761u) >> 24) / 255.0f;
    g.setColour (theme::accent);
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::pi / 8.0f;
        const float tilt = 0.65f * h + 0.12f;
        juce::Path orbit;
        for (int j = 0; j <= 36; ++j)
        {
            const float theta = (float) j * kTwoPi / 36.0f;
            const float r = 10.0f + (2.5f + 5.0f * h) * std::cos (2.0f * theta + a) + 2.8f * h * std::sin (3.0f * theta - a);
            const float x = r * std::cos (theta) * 1.2f, y = r * std::sin (theta) * 0.68f;
            const juce::Point<float> p (centre.x + x * std::cos (tilt) - y * std::sin (tilt), centre.y + x * std::sin (tilt) + y * std::cos (tilt));
            if (j == 0) orbit.startNewSubPath (p);
            else        orbit.lineTo (p);
        }
        orbit.closeSubPath();
        g.strokePath (orbit, juce::PathStrokeType (0.6f));
    }
    g.setColour (theme::bone);
    g.setFont (theme::mono (9.0f, true));
    g.drawText (name.toUpperCase(), area.removeFromBottom (15.0f), juce::Justification::centred);
}

void FieldView::paintString (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name, float value, bool active)
{
    g.setColour (theme::text);
    g.setFont (theme::mono (12.0f, true));
    g.drawText (name, area.removeFromLeft (74.0f), juce::Justification::centredLeft);

    // A rubber band pinned at both ends: the value is where it's being pulled from.
    const auto line = area.withTrimmedRight (12.0f);
    const auto point = line.getX() + line.getWidth() * value;
    const float amp = 3.0f + 5.0f * level + 10.0f * stringBuzz;
    juce::Path band;
    for (int i = 0; i <= 80; ++i)
    {
        const float u = (float) i / 80.0f;
        const float x = line.getX() + line.getWidth() * u;
        const float envelope = u < value ? u / std::max (0.001f, value) : (1.0f - u) / std::max (0.001f, 1.0f - value);
        const float y = line.getCentreY() + amp * envelope * std::sin (u * 42.0f * (0.3f + value) + (float) flow * kTwoPi * 6.0f);
        if (i == 0) band.startNewSubPath (x, y);
        else        band.lineTo (x, y);
    }
    g.setColour (active ? theme::bone : theme::textDim);
    g.strokePath (band, juce::PathStrokeType (1.5f));
    g.setColour (theme::accent);
    g.fillRect (juce::Rectangle<float> (10.0f, 22.0f).withCentre ({ point, line.getCentreY() }));
}

void FieldView::paintLabel (juce::Graphics& g, juce::Point<float> at, const juce::String& text, bool mono)
{
    const juce::Font font (mono ? theme::mono (12.0f, true) : juce::FontOptions (11.0f, juce::Font::bold));
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 12.0f;
    auto box = juce::Rectangle<float> (w, 20.0f).withCentre (at);
    box.setPosition (juce::jlimit (field.getX(), std::max (field.getX(), field.getRight() - w), box.getX()),
                     juce::jlimit (field.getY(), std::max (field.getY(), field.getBottom() - 20.0f), box.getY()));
    g.setColour (theme::ink);
    g.fillRect (box);
    g.setColour (theme::bone);
    g.setFont (font);
    g.drawText (text, box, juce::Justification::centred);
}

//==============================================================================
void FieldView::mouseMove (const juce::MouseEvent& e)
{
    int index = -1;
    hovered = targetAt (e.position, index);
    hoveredIndex = index;
    setMouseCursor (hovered == Target::None || hovered == Target::Empty ? juce::MouseCursor::NormalCursor
                                                                        : juce::MouseCursor::DraggingHandCursor);
}

void FieldView::mouseExit (const juce::MouseEvent&)
{
    hovered = Target::None;
    hoveredIndex = -1;
}

void FieldView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    press (e.position, e.mods.isPopupMenu());
}

void FieldView::mouseDrag (const juce::MouseEvent& e)        { dragTo (e.position); }
void FieldView::mouseUp (const juce::MouseEvent&)            { release(); }
void FieldView::mouseDoubleClick (const juce::MouseEvent& e) { toggleAt (e.position); }

void FieldView::toggleAt (juce::Point<float> position)
{
    int index = -1;
    if (targetAt (position, index) == Target::Blob)
        session.toggleBlob (index);
    repaint();
}

void FieldView::press (juce::Point<float> position, bool popupClick)
{
    int index = -1;
    const auto target = targetAt (position, index);

    if (popupClick)
    {
        if (target == Target::Token)
        {
            session.removeTone (index);
            repaint();
        }
        return;
    }

    held = target;
    heldIndex = index;
    moved = false;
    grabStart = pointer = position;
    heldText.clear();
    blendWith = -1;
    before = session.captureState();

    if (target == Target::Blob)
    {
        const auto& blob = session.getBlobs()[(size_t) index];
        startPosition = { blob.x, blob.y };
        heldText = session.describeBlob (index);
    }
    else if (target == Target::Tide || target == Target::Fusion)
    {
        stringBuzz = 1.0f;
        stringTo (target == Target::Fusion, position.x);
    }
    repaint();
}

void FieldView::stringTo (bool fusionString, float x)
{
    const auto& area = fusionString ? fusionArea : tideArea;
    const auto line = area.withTrimmedLeft (74.0f).withTrimmedRight (12.0f);
    const float value = juce::jlimit (0.0f, 1.0f, (x - line.getX()) / line.getWidth());
    if (fusionString) session.setFusion (value);
    else              session.setTide (value);
    heldText.clear();
}

void FieldView::dragTo (juce::Point<float> position)
{
    if (held == Target::None)
        return;
    pointer = position;
    if (! moved && position.getDistanceFrom (grabStart) < kDragThreshold)
        return;
    moved = true;

    if (held == Target::Blob)
    {
        const auto delta = position - grabStart;
        session.setBlobPosition (heldIndex, startPosition.x + delta.x / field.getWidth(), startPosition.y + delta.y / field.getHeight());
        heldText = session.describeBlob (heldIndex);
    }
    else if (held == Target::Handle)
    {
        const auto from = position - getBlobCentre (heldIndex);
        const float radius = session.getBlobRadius (heldIndex) * field.getHeight();
        const float stretch = (from.getDistanceFromOrigin() / radius - 1.04f) / kHandleReach;
        session.setBlobStretch (heldIndex, stretch, std::atan2 (from.y, from.x));
        const auto& blob = session.getBlobs()[(size_t) heldIndex];
        heldText = blob.name.toUpperCase() + " OSCILA "
                 + juce::String (juce::roundToInt (blob.stretch * 100.0f)) + " %  " + juce::String (FieldSession::rateHz (blob.angle), 2) + " Hz";
    }
    else if (held == Target::Tide || held == Target::Fusion)
    {
        stringBuzz = 1.0f;
        stringTo (held == Target::Fusion, position.x);
    }
    else if (held == Target::Token)
    {
        // Towards the nearest other token: at its centre, the sound is all that one.
        const auto centre = getTokenCentre (heldIndex);
        float nearest = 1.0e9f;
        int other = -1;
        for (int i = 0; i < std::min ((int) session.getTones().size(), kMaxTokens); ++i)
            if (i != heldIndex && tokenBounds (i).getCentre().getDistanceFrom (centre) < nearest)
            {
                nearest = tokenBounds (i).getCentre().getDistanceFrom (centre);
                other = i;
            }
        blendWith = other;
        if (other >= 0)
        {
            const float reach = tokenBounds (heldIndex).getCentre().getDistanceFrom (tokenBounds (other).getCentre());
            const float amount = juce::jlimit (0.0f, 1.0f, 1.0f - nearest / std::max (1.0f, reach));
            session.blendTones (heldIndex, other, amount);
            const int percent = juce::roundToInt (amount * 100.0f);
            const auto& tones = session.getTones();
            heldText = tones[(size_t) heldIndex].name.toUpperCase() + " " + juce::String (100 - percent) + " %  /  "
                     + tones[(size_t) other].name.toUpperCase() + " " + juce::String (percent) + " %";
        }
    }
    repaint();
}

void FieldView::release()
{
    if (held == Target::None)
        return;
    if (held == Target::Token && ! moved && heldIndex < (int) session.getTones().size())
    {
        if (session.loadTone (heldIndex) > 0)
            message (utf8 ("Ese tono es de otra cadena: se aplic\xc3\xb3 lo que coincide"));
    }
    held = Target::None;
    heldIndex = -1;
    blendWith = -1;
    heldText.clear();
    repaint();
}

void FieldView::cancelDrag()
{
    if (held == Target::None)
        return;
    session.applyState (before);
    held = Target::None;
    heldIndex = -1;
    blendWith = -1;
    heldText.clear();
    repaint();
}

bool FieldView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && held != Target::None)
    {
        cancelDrag();
        return true;
    }
    return false;
}
} // namespace kumamp
