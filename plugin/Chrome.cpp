#include "Chrome.h"

#include "Assets.h"

// ======================================================================================== look and feel
VerdigrisLookAndFeel::VerdigrisLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, ui::panel);
    setColour(juce::PopupMenu::textColourId, ui::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff3a1a24));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::PopupMenu::headerTextColourId, ui::dim);
    setColour(juce::ComboBox::backgroundColourId, ui::panel);
    setColour(juce::ComboBox::textColourId, ui::text);
    setColour(juce::ComboBox::outlineColourId, ui::line);
    setColour(juce::ComboBox::arrowColourId, ui::dim);
    setColour(juce::Label::textColourId, ui::text);
    setColour(juce::Slider::textBoxTextColourId, ui::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0e090b));
    setColour(juce::Slider::textBoxOutlineColourId, ui::line);
    setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(0xff6b2a3a));
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0e090b));
    setColour(juce::TextEditor::textColourId, ui::text);
    setColour(juce::TextEditor::outlineColourId, ui::line);
    setColour(juce::TextEditor::focusedOutlineColourId, ui::lineHi);
    setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff6b2a3a));
    setColour(juce::CaretComponent::caretColourId, ui::silver);
    setColour(juce::ScrollBar::thumbColourId, ui::lineHi);
}

juce::Font VerdigrisLookAndFeel::getTextButtonFont(juce::TextButton&, int h) { return assets::font(500, juce::jmin(15.0f, h * 0.42f)); }
juce::Font VerdigrisLookAndFeel::getLabelFont(juce::Label&) { return assets::font(400, 13.0f); }
juce::Font VerdigrisLookAndFeel::getComboBoxFont(juce::ComboBox&) { return assets::font(500, 13.0f); }
juce::Font VerdigrisLookAndFeel::getPopupMenuFont() { return assets::font(500, 14.0f); }

void VerdigrisLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    g.setColour(on ? ui::silver : (down ? juce::Colour(0xff2a1c22) : (over ? ui::panelHi : ui::panel)));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(on ? ui::silver : (over ? ui::lineHi : ui::line));
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
}

void VerdigrisLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()).withExtraKerningFactor(0.04f));
    g.setColour(b.getToggleState() ? ui::ink : (b.isEnabled() ? ui::text : ui::dim));
    g.drawText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, true);
}

void VerdigrisLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    auto r = b.getLocalBounds().toFloat();
    const float sw = 34.0f, sh = 18.0f;
    const juce::Rectangle<float> sw_(r.getRight() - sw - 2, r.getCentreY() - sh / 2, sw, sh);
    const bool on = b.getToggleState();
    g.setColour(on ? accent_.withMultipliedBrightness(0.85f) : juce::Colour(0xff0e090b));
    g.fillRoundedRectangle(sw_, sh / 2);
    g.setColour(over ? ui::lineHi : ui::line);
    g.drawRoundedRectangle(sw_.reduced(0.5f), sh / 2, 1.0f);
    const float kx = on ? sw_.getRight() - sh / 2 - 2 : sw_.getX() + sh / 2 + 2;
    g.setColour(on ? juce::Colours::white : ui::dim);
    g.fillEllipse(kx - 6.5f, sw_.getCentreY() - 6.5f, 13, 13);
    g.setColour(ui::text);
    g.setFont(assets::font(400, 13.0f));
    g.drawText(b.getButtonText(), r.withTrimmedRight(sw + 8).toNearestInt(), juce::Justification::centredLeft, true);
}

void VerdigrisLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) {
    const float cy = y + h * 0.5f;
    g.setColour(juce::Colour(0xff0e090b));
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(w), 4.0f, 2.0f);
    g.setColour(accent_.withMultipliedBrightness(0.85f));
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, juce::jmax(0.0f, pos - x), 4.0f, 2.0f);
    g.setColour(ui::silver);
    g.fillEllipse(pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.drawEllipse(pos - 7.0f, cy - 7.0f, 14.0f, 14.0f, 1.0f);
}

juce::Label* VerdigrisLookAndFeel::createSliderTextBox(juce::Slider& s) {
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(assets::font(400, 12.0f));
    l->setJustificationType(juce::Justification::centredRight);
    return l;
}

void VerdigrisLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    auto r = juce::Rectangle<float>(0, 0, static_cast<float>(w), static_cast<float>(h)).reduced(0.5f);
    g.setColour(ui::panel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(box.isMouseOver() ? ui::lineHi : ui::line);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    juce::Path p;
    p.addTriangle(w - 20.0f, h * 0.5f - 2.0f, w - 12.0f, h * 0.5f - 2.0f, w - 16.0f, h * 0.5f + 3.0f);
    g.setColour(ui::dim);
    g.fillPath(p);
}

void VerdigrisLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.fillAll(ui::panel);
    g.setColour(ui::lineHi);
    g.drawRect(0, 0, w, h, 1);
}

void VerdigrisLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int start, int size, bool over, bool) {
    g.setColour(over ? ui::lineHi : ui::line);
    if (vertical) g.fillRoundedRectangle(static_cast<float>(x) + 2, static_cast<float>(y + start), static_cast<float>(w) - 4, static_cast<float>(size), 2.0f);
    else g.fillRoundedRectangle(static_cast<float>(x + start), static_cast<float>(y) + 2, static_cast<float>(size), static_cast<float>(h) - 4, 2.0f);
}

// ======================================================================================== icon buttons
void IconButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(down ? juce::Colour(0xff2a1c22) : (over ? ui::panelHi : ui::panel));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(over ? ui::lineHi : ui::line);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    g.setColour(ui::text);
    const auto c = r.getCentre();
    if (icon_ == Prev || icon_ == Next) {
        const float d = icon_ == Prev ? -1.0f : 1.0f;
        juce::Path p;
        p.addTriangle(c.x - 4.0f * d, c.y - 5.5f, c.x - 4.0f * d, c.y + 5.5f, c.x + 4.0f * d, c.y);
        g.fillPath(p);
    } else if (icon_ == Hide) {
        // "tuck the bar away": an up chevron over a line (the bar) - hover it for the tip
        juce::Path p;
        p.startNewSubPath(c.x - 5.5f, c.y + 3.0f);
        p.lineTo(c.x, c.y - 2.5f);
        p.lineTo(c.x + 5.5f, c.y + 3.0f);
        g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle(c.x - 6.5f, c.y - 7.0f, 13.0f, 2.0f, 1.0f);
    } else {
        // a cog: a ring with eight teeth
        juce::Path cog;
        for (int i = 0; i < 8; ++i) {
            juce::Path tooth;
            tooth.addRoundedRectangle(-2.0f, -8.5f, 4.0f, 5.0f, 1.0f);
            cog.addPath(tooth, juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.25f * static_cast<float>(i)));
        }
        cog.addEllipse(-6.0f, -6.0f, 12.0f, 12.0f);
        g.fillPath(cog, juce::AffineTransform::translation(c.x, c.y));
        g.setColour(down ? juce::Colour(0xff2a1c22) : (over ? ui::panelHi : ui::panel));
        g.fillEllipse(c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
    }
}

// ======================================================================================== preset bar
PresetBar::PresetBar(VerdigrisProcessor& p) : proc_(p) {
    for (auto* c : std::initializer_list<juce::Component*>{&prev_, &next_, &gear_, &hide_, &nameButton_, &save_, &size_}) addAndMakeVisible(c);
    prev_.onClick = [this] { proc_.presets.step(-1); };
    next_.onClick = [this] { proc_.presets.step(+1); };
    nameButton_.onClick = [this] { showPresetMenu(); };
    save_.onClick = [this] { if (onSave) onSave(); };
    gear_.onClick = [this] { if (onSettings) onSettings(); };
    hide_.onClick = [this] { if (onHide) onHide(); };
    size_.onClick = [this] { showSizeMenu(); };
    nameButton_.set(proc_.presets.currentName(), proc_.presets.currentHint(), proc_.presets.isModified());
    startTimerHz(6);
}

void PresetBar::setSize_(int s) { size_.setButtonText(s == 0 ? "S" : (s == 1 ? "M" : "L")); }

void PresetBar::resized() {
    auto r = getLocalBounds().reduced(6, 6);
    gear_.setBounds(r.removeFromRight(28));
    r.removeFromRight(4);
    hide_.setBounds(r.removeFromRight(28));
    r.removeFromRight(4);
    size_.setBounds(r.removeFromRight(28));
    r.removeFromRight(4);
    save_.setBounds(r.removeFromRight(50));
    r.removeFromRight(6);
    prev_.setBounds(r.removeFromLeft(28));
    r.removeFromLeft(4);
    next_.setBounds(r.removeFromRight(28));
    r.removeFromRight(2);
    nameButton_.setBounds(r);
}

void PresetBar::paint(juce::Graphics& g) {
    juce::ColourGradient bgGrad(juce::Colour(0xff1d1418), 0, 0, juce::Colour(0xff130d10), 0, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGrad);
    g.fillAll();
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void MidiBanner::setText(const juce::String& t, bool learning, juce::Colour accent) {
    text_ = t;
    learning_ = learning;
    accent_ = accent;
    repaint();
}

void MidiBanner::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colour(0xf0130d10));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(learning_ ? accent_ : ui::lineHi);
    g.drawRoundedRectangle(r, 10.0f, 1.2f);
    g.setColour(ui::text);
    g.setFont(assets::font(500, 12.5f));
    g.drawFittedText(text_, getLocalBounds().reduced(12, 4), juce::Justification::centred, 2, 1.0f);
}

void RevealHandle::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const float a = over_ ? 0.95f : 0.45f;
    g.setColour(juce::Colour(0xff130d10).withAlpha(a));
    g.fillEllipse(r);
    g.setColour((over_ ? ui::silver : ui::lineHi).withAlpha(a));
    g.drawEllipse(r, 1.0f);
    const auto c = r.getCentre();
    juce::Path p;
    p.startNewSubPath(c.x - 4.0f, c.y - 2.0f);
    p.lineTo(c.x, c.y + 2.0f);
    p.lineTo(c.x + 4.0f, c.y - 2.0f);
    g.setColour(ui::text.withAlpha(over_ ? 1.0f : 0.75f));
    g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void NameButton::set(const juce::String& name, const juce::String& hint, bool modified) {
    name_ = name;
    hint_ = hint;
    modified_ = modified;
    repaint();
}

void NameButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(down ? juce::Colour(0xff2a1c22) : (over ? ui::panelHi : juce::Colour(0xff0e090b)));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(over ? ui::lineHi : ui::line);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    // a small chevron on the right says "this opens a list"
    juce::Path chev;
    const float cx = r.getRight() - 14.0f, cy = r.getCentreY();
    chev.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chev.lineTo(cx, cy + 2.0f);
    chev.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(ui::dim);
    g.strokePath(chev, juce::PathStrokeType(1.5f));

    auto a = getLocalBounds().reduced(24, 0);
    const bool hasHint = hint_.isNotEmpty();
    auto nameRow = hasHint ? a.removeFromTop(a.getHeight() * 58 / 100).withTrimmedTop(2) : a;
    auto font = assets::font(500, 14.5f).withExtraKerningFactor(0.02f);
    g.setFont(font);
    g.setColour(ui::text);
    g.drawText(name_, nameRow, hasHint ? juce::Justification::centredBottom : juce::Justification::centred, true);
    if (modified_) {
        const float tw = juce::GlyphArrangement::getStringWidth(font, name_);
        const float x = juce::jmin(nameRow.getCentreX() + tw * 0.5f + 8.0f, static_cast<float>(nameRow.getRight()) - 4.0f);
        const float y = nameRow.getBottom() - (hasHint ? 8.0f : nameRow.getHeight() * 0.5f);
        g.setColour(accent_);
        g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
    if (hasHint) {
        g.setColour(ui::dim);
        g.setFont(assets::font(400, 10.5f));
        g.drawText(hint_, a, juce::Justification::centredTop, true);
    }
}

void PresetBar::timerCallback() {
    const auto& pm = proc_.presets;
    const auto name = pm.currentName();
    const auto hint = pm.currentHint();
    const bool mod = pm.isModified();
    if (name != shownName_ || hint != shownHint_ || mod != shownModified_) {
        shownName_ = name;
        shownHint_ = hint;
        shownModified_ = mod;
        nameButton_.set(name, hint, mod);
    }
}

void PresetBar::showPresetMenu() {
    auto& pm = proc_.presets;
    pm.rescanUser();
    const auto& list = pm.entries();
    const int cur = pm.indexOfCurrent();

    juce::PopupMenu root;
    // factory categories in the order they first appear, then the user's own
    juce::StringArray cats;
    for (auto& e : list)
        if (!e.user && !cats.contains(e.category)) cats.add(e.category);
    for (auto& cat : cats) {
        juce::PopupMenu sub;
        for (size_t i = 0; i < list.size(); ++i)
            if (!list[i].user && list[i].category == cat) sub.addItem(static_cast<int>(i) + 1, list[i].name, true, static_cast<int>(i) == cur);
        root.addSubMenu(cat, sub);
    }
    // preset packs dropped into the Presets folder bring their own categories; they get a menu each, like the factory ones
    juce::StringArray packCats;
    for (auto& e : list)
        if (e.user && e.category != "My Presets" && !packCats.contains(e.category)) packCats.add(e.category);
    for (auto& cat : packCats) {
        juce::PopupMenu sub;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].user && list[i].category == cat) sub.addItem(static_cast<int>(i) + 1, list[i].name, true, static_cast<int>(i) == cur);
        root.addSubMenu(cat, sub);
    }
    juce::PopupMenu mine;
    int userCount = 0;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].user && list[i].category == "My Presets") { mine.addItem(static_cast<int>(i) + 1, list[i].name, true, static_cast<int>(i) == cur); ++userCount; }
    if (userCount == 0) mine.addItem(-1, "(nothing saved yet)", false, false);
    root.addSeparator();
    root.addSubMenu("My Presets", mine);
    root.addSeparator();
    root.addItem(100001, "Save current sound as a preset...");
    if (cur >= 0 && list[static_cast<size_t>(cur)].user) root.addItem(100002, "Delete \"" + list[static_cast<size_t>(cur)].name + "\"");

    root.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nameButton_).withMinimumWidth(nameButton_.getWidth()),
                       [this](int result) {
                           if (result <= 0) return;
                           if (result == 100001) { if (onSave) onSave(); return; }
                           auto& m = proc_.presets;
                           if (result == 100002) {
                               const int c = m.indexOfCurrent();
                               if (c >= 0) m.deleteUser(m.entries()[static_cast<size_t>(c)]);
                               return;
                           }
                           const auto& l = m.entries();
                           const auto idx = static_cast<size_t>(result - 1);
                           if (idx < l.size()) m.apply(l[idx]);
                       });
}

void PresetBar::showSizeMenu() {
    juce::PopupMenu m;
    m.addItem(1, "Small");
    m.addItem(2, "Medium");
    m.addItem(3, "Large");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&size_), [this](int r) {
        if (r > 0 && onSize) onSize(r - 1);
    });
}
