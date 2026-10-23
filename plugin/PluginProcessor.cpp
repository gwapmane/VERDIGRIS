#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace {
juce::ParameterID pid(const char* id) { return juce::ParameterID{id, 1}; }

std::unique_ptr<juce::AudioParameterFloat> percent(const char* id, const char* name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(
        pid(id), name, juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }));
}

std::unique_ptr<juce::AudioParameterFloat> decibels(const char* id, const char* name) {
    return std::make_unique<juce::AudioParameterFloat>(
        pid(id), name, juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " dB"; }));
}

std::unique_ptr<juce::AudioParameterBool> toggle(const char* id, const char* name, bool def) {
    return std::make_unique<juce::AudioParameterBool>(pid(id), name, def);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout VerdigrisProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto choice = [](const char* id, const char* name, juce::StringArray names, int def) {
        return std::make_unique<juce::AudioParameterChoice>(pid(id), name, names, def);
    };

    // the pedal: three knobs, two toggles, the footswitch
    layout.add(percent(ids::fuzz, "Fuzz", 0.6f));
    layout.add(percent(ids::bias, "Bias", 0.5f));
    layout.add(percent(ids::volume, "Volume", 0.5f));
    layout.add(choice(ids::type, "Type", {"Ge", "Si"}, 0));
    layout.add(choice(ids::voicing, "Voicing", {"Fat", "Zonk", "Vin"}, 2));
    layout.add(toggle(ids::power, "On", true));

    // the six side switches and the small knob under each
    layout.add(toggle(ids::octave, "Octave", false));
    layout.add(toggle(ids::starve, "Starve", false));
    layout.add(toggle(ids::chop, "Chop", false));
    layout.add(toggle(ids::crush, "Crush", false));
    layout.add(toggle(ids::wash, "Wash", false));
    layout.add(toggle(ids::swell, "Swell", false));
    juce::StringArray patterns, steps, ramps;
    for (int i = 0; i < verdigris::kNumPatterns; ++i) patterns.add(verdigris::patternDef(i).name);
    for (int i = 0; i < verdigris::kNumStepRates; ++i) steps.add(verdigris::stepRateName(i));
    for (int i = 0; i < verdigris::kNumRampSyncs; ++i) ramps.add(verdigris::rampSyncName(i));
    layout.add(percent(ids::octAmt, "Octave Amount", 0.6f));
    layout.add(percent(ids::starveAmt, "Starve Amount", 0.6f));
    layout.add(choice(ids::chopPattern, "Chop Pattern", patterns, 1));
    layout.add(percent(ids::crushAmt, "Crush Amount", 0.5f));
    layout.add(percent(ids::washMix, "Wash Mix", 0.4f));
    layout.add(percent(ids::swellTime, "Swell Time", 0.5f));

    // more for the side switches
    layout.add(percent(ids::sag, "Sag", 0.3f));
    layout.add(choice(ids::chopStep, "Chop Step", steps, 2));
    layout.add(percent(ids::chopDepth, "Chop Depth", 1.0f));
    layout.add(percent(ids::chopSmooth, "Chop Smooth", 0.2f));
    layout.add(percent(ids::washDecay, "Wash Decay", 0.5f));

    // under the hood
    layout.add(percent(ids::tone, "Tone", 0.55f));
    layout.add(percent(ids::gate, "Gate", 0.0f));
    layout.add(percent(ids::mix, "Dry/Wet Mix", 1.0f));
    layout.add(percent(ids::hfe, "Transistor Gain", 0.5f));
    layout.add(percent(ids::temp, "Temperature", 0.4f));
    layout.add(percent(ids::drift, "Drift", 0.0f));
    layout.add(percent(ids::spread, "Wash Spread", 0.3f));
    layout.add(percent(ids::pickup, "Pickup Type", 0.5f));
    layout.add(percent(ids::guitar, "Guitar Level", 1.0f));
    layout.add(percent(ids::lowcut, "Low Cut", 0.0f));
    layout.add(percent(ids::mids, "Mids", 0.5f));
    layout.add(percent(ids::supply, "Supply", 0.88f));

    // ramp
    layout.add(percent(ids::rampRate, "Ramp Rate", 0.4f));
    layout.add(percent(ids::rampDepth, "Ramp Depth", 0.0f));
    layout.add(choice(ids::rampShape, "Ramp Shape", {"Sine", "Triangle", "Saw", "Random"}, 0));
    layout.add(choice(ids::rampSync, "Ramp Lock to Tempo", ramps, 0));
    layout.add(toggle(ids::rampFuzz, "Ramp > Fuzz", false));
    layout.add(toggle(ids::rampBias, "Ramp > Bias", false));
    layout.add(toggle(ids::rampTone, "Ramp > Tone", false));
    layout.add(toggle(ids::rampSupply, "Ramp > Supply", false));
    layout.add(toggle(ids::rampMix, "Ramp > Mix", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        pid(ids::manualBpm), "Tempo (if no DAW)", juce::NormalisableRange<float>(40.0f, 240.0f, 0.1f), 120.0f,
        juce::AudioParameterFloatAttributes().withLabel("BPM")));

    // trims
    layout.add(decibels(ids::inputGain, "Input Gain"));
    layout.add(decibels(ids::outputGain, "Output Gain"));
    return layout;
}

VerdigrisProcessor::VerdigrisProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VERDIGRIS", createLayout()) {
    auto get = [this](const char* id) { return apvts.getRawParameterValue(id); };
    raw_.fuzz = get(ids::fuzz); raw_.bias = get(ids::bias); raw_.volume = get(ids::volume); raw_.type = get(ids::type);
    raw_.voicing = get(ids::voicing); raw_.power = get(ids::power);
    raw_.octave = get(ids::octave); raw_.starve = get(ids::starve); raw_.chop = get(ids::chop); raw_.crush = get(ids::crush);
    raw_.wash = get(ids::wash); raw_.swell = get(ids::swell);
    raw_.octAmt = get(ids::octAmt); raw_.starveAmt = get(ids::starveAmt); raw_.chopPattern = get(ids::chopPattern);
    raw_.crushAmt = get(ids::crushAmt); raw_.washMix = get(ids::washMix); raw_.swellTime = get(ids::swellTime);
    raw_.sag = get(ids::sag); raw_.chopStep = get(ids::chopStep); raw_.chopDepth = get(ids::chopDepth);
    raw_.chopSmooth = get(ids::chopSmooth); raw_.washDecay = get(ids::washDecay);
    raw_.tone = get(ids::tone); raw_.gate = get(ids::gate); raw_.mix = get(ids::mix); raw_.hfe = get(ids::hfe);
    raw_.temp = get(ids::temp); raw_.drift = get(ids::drift); raw_.spread = get(ids::spread); raw_.pickup = get(ids::pickup);
    raw_.guitar = get(ids::guitar); raw_.lowcut = get(ids::lowcut); raw_.mids = get(ids::mids); raw_.supply = get(ids::supply);
    raw_.rampRate = get(ids::rampRate); raw_.rampDepth = get(ids::rampDepth); raw_.rampShape = get(ids::rampShape);
    raw_.rampSync = get(ids::rampSync); raw_.rampFuzz = get(ids::rampFuzz); raw_.rampBias = get(ids::rampBias);
    raw_.rampTone = get(ids::rampTone); raw_.rampSupply = get(ids::rampSupply); raw_.rampMix = get(ids::rampMix);
    raw_.manualBpm = get(ids::manualBpm); raw_.inputGain = get(ids::inputGain); raw_.outputGain = get(ids::outputGain);
    for (auto& v : scopeInMin) v.store(0.0f);
    for (auto& v : scopeInMax) v.store(0.0f);
    for (auto& v : scopeOutMin) v.store(0.0f);
    for (auto& v : scopeOutMax) v.store(0.0f);
}

void VerdigrisProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const size_t cap = static_cast<size_t>(juce::jmax(samplesPerBlock, 4096));
    inL_.assign(cap, 0.0f);
    inR_.assign(cap, 0.0f);
    outL_.assign(cap, 0.0f);
    outR_.assign(cap, 0.0f);
    sampleRate_ = sampleRate;
    engine_.setParams(readParams());
    engine_.prepare(sampleRate);
    setLatencySamples(engine_.latencySamples());
    // a scope column is 0.2 ms of signal (a window of 216 columns shows about 43 ms)
    const int colSamples = juce::jmax(2, static_cast<int>(std::lround(sampleRate * 0.0002)));
    scopeColSamples.store(colSamples);
    scopeLatencyCols.store(juce::roundToInt(static_cast<float>(engine_.latencySamples()) / static_cast<float>(colSamples)));
    colCount_ = 0;
}

bool VerdigrisProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

verdigris::Params VerdigrisProcessor::readParams() const {
    using namespace verdigris;
    auto idx = [](std::atomic<float>* a) { return static_cast<int>(a->load() + 0.5f); };
    auto on = [](std::atomic<float>* a) { return a->load() > 0.5f; };
    Params p;
    p.fuzz = raw_.fuzz->load();
    p.bias = raw_.bias->load();
    p.volume = raw_.volume->load();
    p.type = idx(raw_.type);
    p.voicing = idx(raw_.voicing);
    p.on = on(raw_.power);
    p.octave = on(raw_.octave);
    p.starve = on(raw_.starve);
    p.chop = on(raw_.chop);
    p.crush = on(raw_.crush);
    p.wash = on(raw_.wash);
    p.swell = on(raw_.swell);
    p.octAmt = raw_.octAmt->load();
    p.starveAmt = raw_.starveAmt->load();
    p.sag = raw_.sag->load();
    p.chopDepth = raw_.chopDepth->load();
    p.chopSmooth = raw_.chopSmooth->load();
    p.crushAmt = raw_.crushAmt->load();
    p.washMix = raw_.washMix->load();
    p.washDecay = raw_.washDecay->load();
    p.swellTime = raw_.swellTime->load();
    p.chopPattern = idx(raw_.chopPattern);
    p.chopStep = idx(raw_.chopStep);
    p.tone = raw_.tone->load();
    p.gate = raw_.gate->load();
    p.mix = raw_.mix->load();
    p.hfe = raw_.hfe->load();
    p.temp = raw_.temp->load();
    p.drift = raw_.drift->load();
    p.spread = raw_.spread->load();
    p.pickup = raw_.pickup->load();
    p.guitar = raw_.guitar->load();
    p.lowcut = raw_.lowcut->load();
    p.mids = raw_.mids->load();
    p.supply = raw_.supply->load();
    p.rampRate = raw_.rampRate->load();
    p.rampDepth = raw_.rampDepth->load();
    p.rampShape = idx(raw_.rampShape);
    p.rampSync = idx(raw_.rampSync);
    p.rampTargets = (on(raw_.rampFuzz) ? kRampFuzz : 0u) | (on(raw_.rampBias) ? kRampBias : 0u) | (on(raw_.rampTone) ? kRampTone : 0u) |
                    (on(raw_.rampSupply) ? kRampSupply : 0u) | (on(raw_.rampMix) ? kRampMix : 0u);
    p.inputGainDb = raw_.inputGain->load();
    p.outputGainDb = raw_.outputGain->load();
    return p;
}

// the tempo and where the song is: from the DAW when it says, otherwise from the "Tempo" slider
verdigris::Transport VerdigrisProcessor::readTransport() {
    verdigris::Transport t;
    t.bpm = 0.0;
    bool fromHost = false;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition()) {
            if (const auto b = pos->getBpm(); b.hasValue() && *b > 1.0) { t.bpm = *b; fromHost = true; }
            if (const auto q = pos->getPpqPosition(); q.hasValue()) { t.hostValid = true; t.ppq = *q; }
            t.playing = pos->getIsPlaying();
            if (const auto ts = pos->getTimeSignature(); ts.hasValue() && ts->denominator > 0)
                t.beatsPerBar = juce::jlimit(1, 16, static_cast<int>(std::round(4.0 * ts->numerator / ts->denominator)));
        }
    if (!fromHost) t.bpm = raw_.manualBpm->load();
    t.bpm = std::round(juce::jlimit(20.0, 400.0, t.bpm) * 100.0) / 100.0; // steady: no tiny wobble from the host
    tempoBpm.store(static_cast<float>(t.bpm), std::memory_order_relaxed);
    tempoFromHost.store(fromHost, std::memory_order_relaxed);
    return t;
}

// squash the signal into scope columns: the input and the output, min and max of every few samples
void VerdigrisProcessor::feedScope(const float* in, const float* out, int n) {
    const int colSamples = scopeColSamples.load(std::memory_order_relaxed);
    unsigned head = scopeHead.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        if (colCount_ == 0) {
            colInMin_ = colInMax_ = in[i];
            colOutMin_ = colOutMax_ = out[i];
        } else {
            colInMin_ = juce::jmin(colInMin_, in[i]);
            colInMax_ = juce::jmax(colInMax_, in[i]);
            colOutMin_ = juce::jmin(colOutMin_, out[i]);
            colOutMax_ = juce::jmax(colOutMax_, out[i]);
        }
        if (++colCount_ >= colSamples) {
            const size_t k = head % kScopeCols;
            scopeInMin[k].store(colInMin_, std::memory_order_relaxed);
            scopeInMax[k].store(colInMax_, std::memory_order_relaxed);
            scopeOutMin[k].store(colOutMin_, std::memory_order_relaxed);
            scopeOutMax[k].store(colOutMax_, std::memory_order_relaxed);
            ++head;
            scopeHead.store(head, std::memory_order_release);
            colCount_ = 0;
        }
    }
}

void VerdigrisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();

    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isController()) handleCc(m.getControllerNumber(), m.getControllerValue());
    }

    engine_.setParams(readParams());
    engine_.setTransport(readTransport());

    for (int ch = numIn; ch < numOut; ++ch) buffer.clear(ch, 0, total);

    const size_t cap = inL_.size();
    int done = 0;
    while (done < total) {
        const int n = static_cast<int>(juce::jmin(static_cast<size_t>(total - done), cap));
        const float* srcL = buffer.getReadPointer(0) + done;
        const float* srcR = numIn > 1 ? buffer.getReadPointer(1) + done : srcL;
        std::copy(srcL, srcL + n, inL_.begin());
        std::copy(srcR, srcR + n, inR_.begin());
        engine_.process(inL_.data(), inR_.data(), outL_.data(), outR_.data(), n);
        feedScope(inL_.data(), outL_.data(), n);
        std::copy(outL_.begin(), outL_.begin() + n, buffer.getWritePointer(0) + done);
        if (numOut > 1) std::copy(outR_.begin(), outR_.begin() + n, buffer.getWritePointer(1) + done);
        done += n;
    }

    // peaks for the lights and the window
    {
        const float* l = buffer.getReadPointer(0);
        float pk = 0.0f;
        for (int i = 0; i < total; ++i) pk = juce::jmax(pk, std::abs(l[i]));
        float cur = visOut.load(std::memory_order_relaxed);
        while (pk > cur && !visOut.compare_exchange_weak(cur, pk, std::memory_order_relaxed)) {}
        const float pi = engine_.inputLevel();
        cur = visIn.load(std::memory_order_relaxed);
        while (pi > cur && !visIn.compare_exchange_weak(cur, pi, std::memory_order_relaxed)) {}
    }
    uiClip.store(engine_.clipActivity(), std::memory_order_relaxed);
    uiSupply.store(engine_.supplyVolts(), std::memory_order_relaxed);
    uiSwell.store(engine_.swellGain(), std::memory_order_relaxed);
    uiChop.store(engine_.chopLevel(), std::memory_order_relaxed);
    uiChopStep.store(engine_.chopStepNow(), std::memory_order_relaxed);
    uiGateOpen.store(engine_.gateIsOpen(), std::memory_order_relaxed);
    uiRamp.store(engine_.rampValue(), std::memory_order_relaxed);
    uiBeat.store(engine_.beatPosition(), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------- MIDI control
float VerdigrisProcessor::ccToNormalised(const juce::RangedAudioParameter& p, int value) {
    const float x = static_cast<float>(juce::jlimit(0, 127, value)) / 127.0f;
    if (p.isDiscrete()) { // switches and choices: split the controller's travel into equal zones
        const int n = p.getNumSteps();
        if (n > 1) return static_cast<float>(juce::jmin(n - 1, static_cast<int>(x * static_cast<float>(n)))) / static_cast<float>(n - 1);
    }
    return x;
}

void VerdigrisProcessor::handleCc(int cc, int value) {
    if (cc < 0 || cc >= 120) return; // 120 and up are "all sound off" and friends, not knobs
    if (auto* target = learnTarget_.exchange(nullptr)) {
        for (auto& slot : ccMap_)
            if (slot.load() == target) slot.store(nullptr); // one CC per control
        ccMap_[static_cast<size_t>(cc)].store(target);
        learnedParam.store(target);
        learnedCc.store(cc);
        learnedCounter.fetch_add(1);
    }
    if (auto* p = ccMap_[static_cast<size_t>(cc)].load()) p->setValueNotifyingHost(ccToNormalised(*p, value));
}

int VerdigrisProcessor::midiCcOf(const juce::RangedAudioParameter* p) const {
    for (size_t cc = 0; cc < ccMap_.size(); ++cc)
        if (ccMap_[cc].load() == p) return static_cast<int>(cc);
    return -1;
}

void VerdigrisProcessor::clearMidiMapping(const juce::RangedAudioParameter* p) {
    for (auto& slot : ccMap_)
        if (slot.load() == p) slot.store(nullptr);
}

void VerdigrisProcessor::clearAllMidiMappings() {
    for (auto& slot : ccMap_) slot.store(nullptr);
}

juce::String VerdigrisProcessor::midiMappingSummary() const {
    juce::String s;
    for (size_t cc = 0; cc < ccMap_.size(); ++cc)
        if (auto* p = ccMap_[cc].load()) s << "CC " << static_cast<int>(cc) << "  =  " << p->getName(40) << "\n";
    return s.isEmpty() ? juce::String("No MIDI mappings yet.") : s.trimEnd();
}

void VerdigrisProcessor::saveMidiMap() {
    juce::StringArray parts;
    for (size_t cc = 0; cc < ccMap_.size(); ++cc)
        if (auto* p = ccMap_[cc].load()) parts.add(juce::String(static_cast<int>(cc)) + "=" + p->paramID);
    apvts.state.setProperty("midiMap", parts.joinIntoString(","), nullptr);
}

void VerdigrisProcessor::loadMidiMap() {
    clearAllMidiMappings();
    for (auto& token : juce::StringArray::fromTokens(apvts.state.getProperty("midiMap").toString(), ",", "")) {
        const int cc = token.upToFirstOccurrenceOf("=", false, false).getIntValue();
        if (cc < 0 || cc >= 120) continue;
        if (auto* p = apvts.getParameter(token.fromFirstOccurrenceOf("=", false, false))) ccMap_[static_cast<size_t>(cc)].store(p);
    }
}

juce::AudioProcessorEditor* VerdigrisProcessor::createEditor() { return new VerdigrisEditor(*this); }

void VerdigrisProcessor::getStateInformation(juce::MemoryBlock& destData) {
    saveMidiMap();
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void VerdigrisProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            loadMidiMap();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VerdigrisProcessor(); }
