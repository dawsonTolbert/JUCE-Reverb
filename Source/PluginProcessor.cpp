/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "mix-matrix.h"
#include <vector>

double ReverbAudioProcessor::randomInRange(double low, double high)
{
    double unitRand = rand() / double(RAND_MAX);
    return low + unitRand * (high - low);
}

//==============================================================================
ReverbAudioProcessor::ReverbAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

ReverbAudioProcessor::~ReverbAudioProcessor()
{
}

//==============================================================================
const juce::String ReverbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ReverbAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ReverbAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ReverbAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ReverbAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ReverbAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int ReverbAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ReverbAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ReverbAudioProcessor::getProgramName (int index)
{
    return {};
}

void ReverbAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void ReverbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    spec.numChannels = getTotalNumOutputChannels();

    mixer.prepare(spec);

    for (auto& volume : delayFeedbackVolume) {
        volume.reset(spec.sampleRate, 0.05);
    }

    //preparing 8 channel delay feedback
    spec.numChannels = delayChannels;
    delayModule.prepare(spec);
    float delaySamplesBase = delayMs / 1000.0 * getSampleRate();
    for (int c = 0; c < delayChannels; ++c) {
        float r = c * 1.0 / delayChannels;
        delayValue[c] = std::pow(2, r) * delaySamplesBase;
    }

    delayModule.reset();
    mixer.reset();
    std::fill(lastDelayOutput.begin(), lastDelayOutput.end(), 0.0f);

    mixer.setWetMixProportion(0.8f);
    for (auto& volume : delayFeedbackVolume) {
        volume.setTargetValue(decayGain);
    }

    delayBuffers.setSize(delayChannels, samplesPerBlock);
}

void ReverbAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void ReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    //// This is the place where you'd normally do the guts of your plugin's
    //// audio processing...
    //// Make sure to reset the state if your inner loop is processing
    //// the samples and the outer loop is handling the channels.
    //// Alternatively, you can process the samples with the channels
    //// interleaved by keeping the same state.
    auto audioBlock = juce::dsp::AudioBlock<float>(buffer).getSubsetChannelBlock(0, (size_t)totalNumInputChannels);
    auto context = juce::dsp::ProcessContextReplacing<float>(audioBlock);
    const auto& input = context.getInputBlock();
    const auto& output = context.getOutputBlock();

    //copy buffer across delayChannels
    for (int i = 0; i < delayChannels; ++i) {
        if (i == 0 || i % 2 == 0) {
            delayBuffers.copyFrom(i, 0, buffer, 0, 0, buffer.getNumSamples());
        }
        else {
            delayBuffers.copyFrom(i, 0, buffer, 1, 0, buffer.getNumSamples());
        }
    }

    auto delayBlock = juce::dsp::AudioBlock<float>(delayBuffers).getSubsetChannelBlock(0, delayChannels);
    auto delayContext = juce::dsp::ProcessContextReplacing<float>(delayBlock);
    const auto& delayInput = delayContext.getInputBlock();
    const auto& delayOutput = delayContext.getOutputBlock();

    //Push dry stereo channels to mixer, process delay
    mixer.pushDrySamples(input);
    processDelay(delayInput, delayOutput);

    auto* mixDownLeft = output.getChannelPointer(0);
    auto* mixDownRight = output.getChannelPointer(1);
    //for each delay channel, mix down samples
    for (size_t i = 0; i < buffer.getNumSamples(); ++i) {
        for (size_t channel = 0; channel < delayChannels; ++channel) {
            //for each sample, add to mixDown channel at 25% gain
            auto* preMix = delayOutput.getChannelPointer(channel);
            if (channel == 0 || channel % 2 == 0) {
                mixDownLeft[i] += preMix[i] * 0.25;
            }
            else {
                mixDownRight[i] += preMix[i] * 0.25;
            }
        }
    }

    mixer.mixWetSamples(output);
}

void ReverbAudioProcessor::processDelay(const juce::dsp::AudioBlock<const float>& input, const juce::dsp::AudioBlock<float>& output)
{
    const auto numChannels = input.getNumChannels();
    const auto numSamples = input.getNumSamples();

    for (size_t i = 0; i < numSamples; ++i)
    {
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* samplesIn = input.getChannelPointer(channel);
            auto* samplesOut = output.getChannelPointer(channel);

            auto input = samplesIn[i] - lastDelayOutput[channel];
            delayModule.setDelay((float)delayValue[channel]);

            delayModule.pushSample(int(channel), input);
            samplesOut[i] = delayModule.popSample(int(channel));

            lastDelayOutput[channel] = samplesOut[i] * delayFeedbackVolume[channel].getNextValue();
        }
    }
}

float ReverbAudioProcessor::applyLowPassFilter(float sample, float cutoffFrequency, float sampleRate) 
{
    // Calculate the filter coefficient based on cutoff frequency and sample rate
    constexpr auto PI = 3.14159265359f;
    float tau = 1.0f / (2.0f * PI * cutoffFrequency);
    float alpha = exp(-tau / sampleRate);

    // Static member to store the previous filtered sample (initially 0)
    static float previousSample = 0.0f;

    // Apply the filter formula
    float filteredSample = alpha * previousSample + (1.0f - alpha) * sample;

    // Update the previous sample for the next call
    previousSample = filteredSample;

    return filteredSample;
}

//==============================================================================
bool ReverbAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* ReverbAudioProcessor::createEditor()
{
    return new ReverbAudioProcessorEditor (*this);
}

//==============================================================================
void ReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void ReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverbAudioProcessor();
}
