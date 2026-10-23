#include "Settings.h"

const LookSettings& Settings::defaults() {
    static const LookSettings d;
    return d;
}

juce::File Settings::folder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VERDIGRIS");
}

Settings::Settings() { load(); }

void Settings::load() {
    const auto f = folder().getChildFile("settings.xml");
    if (!f.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(f);
    if (xml == nullptr || !xml->hasTagName("VERDIGRISSETTINGS")) return;
    const auto& d = defaults();
    look_.body = juce::Colour::fromString(xml->getStringAttribute("body", d.body.toString()));
    look_.accent = juce::Colour::fromString(xml->getStringAttribute("accent", d.accent.toString()));
    look_.texture = juce::jlimit(0, 4, xml->getIntAttribute("texture", d.texture));
    look_.textureStrength = juce::jlimit(0.0f, 1.2f, static_cast<float>(xml->getDoubleAttribute("textureStrength", d.textureStrength)));
    look_.photoFile = xml->getStringAttribute("photo", "");
    look_.photoOpacity = juce::jlimit(0.0f, 1.0f, static_cast<float>(xml->getDoubleAttribute("photoOpacity", d.photoOpacity)));
    look_.size = juce::jlimit(0, 2, xml->getIntAttribute("size", d.size));
    look_.videoPath = xml->getStringAttribute("video", "");
    look_.pedalOnly = xml->getBoolAttribute("pedalOnly", false);
    if (look_.photoFile.isNotEmpty() && !photoPath().existsAsFile()) look_.photoFile = {};
}

void Settings::save() const {
    juce::XmlElement xml("VERDIGRISSETTINGS");
    xml.setAttribute("body", look_.body.toString());
    xml.setAttribute("accent", look_.accent.toString());
    xml.setAttribute("texture", look_.texture);
    xml.setAttribute("textureStrength", static_cast<double>(look_.textureStrength));
    xml.setAttribute("photo", look_.photoFile);
    xml.setAttribute("photoOpacity", static_cast<double>(look_.photoOpacity));
    xml.setAttribute("size", look_.size);
    xml.setAttribute("video", look_.videoPath);
    xml.setAttribute("pedalOnly", look_.pedalOnly);
    const auto dir = folder();
    dir.createDirectory();
    xml.writeTo(dir.getChildFile("settings.xml"));
}

void Settings::setLook(const LookSettings& l) {
    if (l == look_) return;
    look_ = l;
    save();
    listeners_.call([](Listener& li) { li.lookChanged(); });
}

juce::File Settings::photoPath() const {
    return look_.photoFile.isEmpty() ? juce::File() : folder().getChildFile(look_.photoFile);
}

juce::String Settings::importPhoto(const juce::File& src) {
    const auto dir = folder();
    dir.createDirectory();
    // a fresh name each time, so the picture is re-read even if the old one had the same name
    const juce::String name = "picture_" + juce::String(juce::Time::currentTimeMillis()) + src.getFileExtension().toLowerCase();
    if (!src.copyFileTo(dir.getChildFile(name))) return {};
    // tidy up older pictures
    for (const auto& old : dir.findChildFiles(juce::File::findFiles, false, "picture_*"))
        if (old.getFileName() != name) old.deleteFile();
    return name;
}
