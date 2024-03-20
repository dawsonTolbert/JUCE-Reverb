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

    //preparing single channel delay feedback
    delayModule.reset();
    delayModule.prepare(spec);

    //preparing multiChannel feedback
    for (int c = 0; c < delayChannels; ++c) {
        delays[c] = delayModule;

        delays[c].reset();
        delays[c].prepare(spec);

        diffDelays[c] = delayModule;

        diffDelays[c].reset();
        diffDelays[c].prepare(spec);
    }
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

    // Create temporary buffers for each delay channel
    const int bufferLength = buffer.getNumSamples();
    
    std::vector<juce::AudioBuffer<float>> delayBuffers;
    std::vector<juce::AudioBuffer<float>> diffusionBuffers;
    for (int c = 0; c < delayChannels; ++c){
        juce::AudioBuffer<float> tempBuffer(totalNumInputChannels, bufferLength);
        delayBuffers.push_back(tempBuffer);

        juce::AudioBuffer<float> diffTempBuffer(totalNumInputChannels, bufferLength);
        diffusionBuffers.push_back(diffTempBuffer);
    }

    double delaySamplesBase = delayMs / 1000.0f * getSampleRate();
    double delaySamplesRange = delayMsRange / 1000.0f * getSampleRate();

    std::array<bool, delayChannels> flipPolarity;
    // Copy input buffer to temporary delay buffers and set delaySamples
    for (int channel = 0; channel < totalNumInputChannels; ++channel){
        auto* data = buffer.getReadPointer(channel);

        for (int c = 0; c < delayChannels; c++){
            //Feedback Delay Configguration
            delayBuffers[c].copyFrom(channel, 0, data, bufferLength);

            double r = c * 1.0f / delayChannels;
            delaySamples[c] = std::pow(2, r) * delaySamplesBase;

            //Diffusion Delay Configuration
            diffusionBuffers[c].copyFrom(channel, 0, data, bufferLength);

            double rangeLow = delaySamplesRange * c / delayChannels;
            double rangeHigh = delaySamplesRange * (c + 1) / delayChannels;
            diffDelaySamples[c] = randomInRange(rangeLow, rangeHigh);
            flipPolarity[c] = rand() % 2;
        }
    }

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.

    for (int channel = 0; channel < totalNumInputChannels; ++channel){
        auto* data = buffer.getWritePointer(channel);

        // ..do something to the data...
        //for each sample in the buffer...
        for (int i = 0; i < bufferLength; i++) {
            //prepare delay channel data for mixing
            for (int c = 0; c < delayChannels; c++) {
                auto* delayChannel = diffusionBuffers[c].getReadPointer(channel);
                diffMixed[c] = delayChannel[i];
            }

            //mix matrix processing
            Hadamard<float, delayChannels>::inPlace(diffMixed.data());

            // Flip some polarities
            for (int c = 0; c < delayChannels; ++c) {
                if (flipPolarity[c]) diffMixed[c] *= -1;
                data[i] += diffMixed[c] / delayChannels;
            }

            processFeedbackDelay(delayBuffers, channel, i, data);
        }
    }
}

void ReverbAudioProcessor::processFeedbackDelay(std::vector<juce::AudioSampleBuffer> delayBuffers, int channel, int sample, float *data)
{
    //prepare delay channel data for mixing
    for (int c = 0; c < delayChannels; c++) {
        auto* delayChannel = delayBuffers[c].getReadPointer(channel);
        mixed[c] = delayChannel[sample];
    }

    //mix matrix processing
    Householder<float, delayChannels>::inPlace(mixed.data());

    //process each mixed delay channel
    for (int c = 0; c < delayChannels; c++) {
        //add delay feedback to mixed channel data
        mixed[c] += delays[c].popSample(channel, delaySamples[c]);
        //send processed and mixed feedback channel back to delays
        delays[c].pushSample(channel, mixed[c] * decayGain);
        //add processed and mixed feedback channel to buffer data
        data[sample] += mixed[c] / delayChannels;
    }
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
