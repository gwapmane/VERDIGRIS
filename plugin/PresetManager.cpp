#include "PresetManager.h"

#include "ParamIds.h"
#include "Settings.h"
#include "presets_factory.h"

namespace {
const char* kName = "presetName";
const char* kUser = "presetUser";
const char* kHint = "presetHint";
const char* kRef = "presetRef";
const char* kSlot = "abSlot";
const char* kSlotKeys[3] = {"slotA", "slotLive", "slotB"};
} // namespace

const std::vector<const char*>& PresetManager::soundIds() {
    static const std::vector<const char*> v(verdigris::soundIdNames(), verdigris::soundIdNames() + verdigris::kNumSoundIds);
    return v;
}

juce::File PresetManager::userFolder() { return Settings::folder().getChildFile("Presets"); }

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) { rescanUser(); }

// ---------------------------------------------------------------------------------------------- parameters
void PresetManager::setNorm(const char* id, float v) {
    if (auto* p = apvts_.getParameter(id)) p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
}

float PresetManager::getNorm(const char* id) const {
    if (auto* p = apvts_.getParameter(id)) return p->getValue();
    return 0.0f;
}

PresetManager::Snapshot PresetManager::capture() const {
    Snapshot s;
    for (auto* id : soundIds()) s.push_back(getNorm(id));
    return s;
}

void PresetManager::applySnapshot(const Snapshot& s) {
    const auto& ids = soundIds();
    for (size_t i = 0; i < ids.size() && i < s.size(); ++i) setNorm(ids[i], s[i]);
}

juce::String PresetManager::toText(const Snapshot& s) {
    juce::StringArray a;
    for (float v : s) a.add(juce::String(v, 5));
    return a.joinIntoString(",");
}

PresetManager::Snapshot PresetManager::fromText(const juce::String& t) {
    Snapshot s;
    if (t.isEmpty()) return s;
    juce::StringArray a;
    a.addTokens(t, ",", "");
    for (auto& x : a) s.push_back(x.getFloatValue());
    return s;
}

// ---------------------------------------------------------------------------------------------- current preset
void PresetManager::setCurrent(const juce::String& name, bool user, const juce::String& hint) {
    apvts_.state.setProperty(kName, name, nullptr);
    apvts_.state.setProperty(kUser, user, nullptr);
    apvts_.state.setProperty(kHint, hint, nullptr);
    apvts_.state.setProperty(kRef, toText(capture()), nullptr);
}

juce::String PresetManager::currentName() const {
    const auto n = apvts_.state.getProperty(kName).toString();
    return n.isEmpty() ? juce::String("Init") : n;
}
juce::String PresetManager::currentHint() const { return apvts_.state.getProperty(kHint).toString(); }
bool PresetManager::currentIsUser() const { return static_cast<bool>(apvts_.state.getProperty(kUser, false)); }

bool PresetManager::isModified() const {
    const auto ref = fromText(apvts_.state.getProperty(kRef).toString());
    if (ref.empty()) return false;
    const auto now = capture();
    for (size_t i = 0; i < now.size() && i < ref.size(); ++i)
        if (std::abs(now[i] - ref[i]) > 0.004f) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------- the list
void PresetManager::rescanUser() {
    entries_.clear();
    const auto& bank = verdigris::factoryPresets();
    for (size_t i = 0; i < bank.size(); ++i) {
        Entry e;
        e.category = bank[i].category;
        e.name = bank[i].name;
        e.hint = bank[i].hint;
        e.factoryIndex = static_cast<int>(i);
        entries_.push_back(e);
    }
    auto files = userFolder().findChildFiles(juce::File::findFiles, false, "*.verdigrispreset");
    files.sort();
    for (const auto& f : files) {
        auto xml = juce::XmlDocument::parse(f);
        if (xml == nullptr || !xml->hasTagName("VERDIGRISPRESET")) continue;
        Entry e;
        e.user = true;
        // a preset you saved yourself is "My Presets"; a preset pack names its own category and hint
        e.category = xml->getStringAttribute("category", "My Presets").trim();
        if (e.category.isEmpty()) e.category = "My Presets";
        e.hint = xml->getStringAttribute("hint");
        e.name = xml->getStringAttribute("name", f.getFileNameWithoutExtension());
        e.file = f;
        entries_.push_back(e);
    }
}

int PresetManager::indexOfCurrent() const {
    const auto n = currentName();
    const bool u = currentIsUser();
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].name == n && entries_[i].user == u) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------------------------- applying
void PresetManager::applyFactory(int index) {
    const auto& bank = verdigris::factoryPresets();
    if (index < 0 || index >= static_cast<int>(bank.size())) return;
    const auto& f = bank[static_cast<size_t>(index)];
    const auto& names = soundIds();
    for (size_t i = 0; i < names.size(); ++i) setNorm(names[i], f.v[i]);
}

bool PresetManager::applyFile(const juce::File& file) {
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("VERDIGRISPRESET")) return false;
    for (auto* id : soundIds()) {
        // a preset made by an older build may lack a parameter: leave that one where it is
        for (auto* p = xml->getFirstChildElement(); p != nullptr; p = p->getNextElement())
            if (p->hasTagName("P") && p->getStringAttribute("id") == id) setNorm(id, static_cast<float>(p->getDoubleAttribute("v")));
    }
    return true;
}

void PresetManager::apply(const Entry& e) {
    if (e.user) {
        if (!applyFile(e.file)) return;
        setCurrent(e.name, true, e.hint);
    } else {
        applyFactory(e.factoryIndex);
        setCurrent(e.name, false, e.hint);
    }
}

void PresetManager::step(int direction) {
    if (entries_.empty()) return;
    int i = indexOfCurrent();
    const int n = static_cast<int>(entries_.size());
    i = i < 0 ? (direction > 0 ? 0 : n - 1) : (i + direction + n) % n;
    apply(entries_[static_cast<size_t>(i)]);
}

juce::String PresetManager::suggestName() const {
    for (int i = 1; i < 1000; ++i) {
        const auto n = "My Preset " + juce::String(i);
        bool taken = false;
        for (auto& e : entries_)
            if (e.user && e.name == n) taken = true;
        if (!taken) return n;
    }
    return "My Preset";
}

bool PresetManager::saveUser(const juce::String& rawName) {
    const auto name = rawName.trim().substring(0, 48);
    if (name.isEmpty()) return false;
    juce::XmlElement xml("VERDIGRISPRESET");
    xml.setAttribute("name", name);
    xml.setAttribute("category", "My Presets");
    xml.setAttribute("version", 1);
    const auto& ids = soundIds();
    const auto s = capture();
    for (size_t i = 0; i < ids.size(); ++i) {
        auto* p = xml.createNewChildElement("P");
        p->setAttribute("id", ids[i]);
        p->setAttribute("v", static_cast<double>(s[i]));
    }
    const auto dir = userFolder();
    if (!dir.createDirectory()) return false;
    const auto file = dir.getChildFile(juce::File::createLegalFileName(name) + ".verdigrispreset");
    if (!xml.writeTo(file)) return false;
    rescanUser();
    setCurrent(name, true, {});
    return true;
}

bool PresetManager::deleteUser(const Entry& e) {
    if (!e.user || !e.file.existsAsFile()) return false;
    const bool ok = e.file.moveToTrash() || e.file.deleteFile();
    rescanUser();
    return ok;
}

// ---------------------------------------------------------------------------------------------- A / LIVE / B
int PresetManager::slot() const { return juce::jlimit(0, 2, static_cast<int>(apvts_.state.getProperty(kSlot, 1))); }

bool PresetManager::slotHasMemory(int s) const {
    return apvts_.state.getProperty(kSlotKeys[juce::jlimit(0, 2, s)]).toString().isNotEmpty();
}

void PresetManager::setSlot(int newSlot) {
    newSlot = juce::jlimit(0, 2, newSlot);
    const int cur = slot();
    if (newSlot == cur) return;
    // leaving a slot stores what is on the knobs now; entering one recalls what it holds (an empty one just keeps the
    // current sound, so it starts as a copy of it)
    apvts_.state.setProperty(kSlotKeys[cur], toText(capture()), nullptr);
    const auto mem = fromText(apvts_.state.getProperty(kSlotKeys[newSlot]).toString());
    if (!mem.empty()) applySnapshot(mem);
    apvts_.state.setProperty(kSlot, newSlot, nullptr);
}
