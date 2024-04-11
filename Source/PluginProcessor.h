/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
*/
class ReverbAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    ReverbAudioProcessor();
    ~ReverbAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void processDelay(const juce::dsp::AudioBlock<const float>& input, const juce::dsp::AudioBlock<float>& output);
    void mixDownToTwoChannels(const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output);

    double randomInRange(double low, double high);
    float applyLowPassFilter(float sample, float cutoffFrequency, float sampleRate);

private:
    static constexpr auto effectDelaySamples = 192000;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayModule{ effectDelaySamples };
    std::array<float, 8> lastDelayOutput;
    std::array<float, 8> delayValue{ {} };
    std::array<juce::LinearSmoothedValue<float>, 8> delayFeedbackVolume;
    juce::dsp::DryWetMixer<float> mixer;
    juce::AudioBuffer<float> delayBuffers;

    float delayMs = 150.0f;
    float decayGain = 0.85f;

    //Multi Channel Feedback Array
    static constexpr int delayChannels = 8;
    std::array<int, delayChannels> delaySamples;
    //std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> , delayChannels> delays;

    std::array<float, delayChannels> mixed;

    //Diffusion
    //static constexpr int diffusionSteps = 4;
    //float delayMsRange = 50.0f;
    //std::array<int, delayChannels> diffDelaySamples;
    //std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, delayChannels> diffDelays;

    //std::array<float, delayChannels> diffMixed;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbAudioProcessor)
};
