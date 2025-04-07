#include "daisy_seed.h"
#include "daisysp.h"
extern "C" {
    #include "amy.h"
    #include "examples.h"
}
// Use the daisy namespace to prevent having to type
// daisy:: before all libdaisy functions
using namespace daisy;
using namespace daisysp;

// Declare a DaisySeed object called hardware
DaisySeed  hardware;

Switch button1;

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    short int * block = amy_simple_fill_buffer();

    //Fill the block with samples
    for(size_t i = 0; i < size; i += 2)
    {
        //Set the left and right outputs
        out[i]     = (block[i]/32767.0); //osc_out;
        out[i + 1] = (block[i+1]/32767.0); //osc_out;
    }
}

void polyphony(uint32_t start, uint16_t patch) {
    struct event e = amy_default_event();
    e.time = start;
    e.load_patch = patch;
    strcpy(e.voices, "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14");
    amy_add_event(e);
    start += 250;
    uint8_t note = 30;
    for(uint8_t i=0;i<15;i++) {
        e = amy_default_event();
        e.time = start;
        e.velocity=0.5;
        sprintf(e.voices, "%d", i);
        e.midi_note = note;
        amy_add_event(e);
        start += 1000;
        note += 2;
    }
}   



int main(void)
{
    // Configure and Initialize the Daisy Seed
    // These are separate to allow reconfiguration of any of the internal
    // components before initialization.
    hardware.Configure();
    hardware.Init();
    hardware.SetAudioBlockSize(128);

    //How many samples we'll output per second
    float samplerate = hardware.AudioSampleRate();

    //Create an ADC configuration
    AdcChannelConfig adcConfig;
    //Add pin 21 as an analog input in this config. We'll use this to read the knob
    adcConfig.InitSingle(hardware.GetPin(21));

    //Initialize the button on pin 28
    button1.Init(hardware.GetPin(28), samplerate / 48.f);

    //Set the ADC to use our configuration
    hardware.adc.Init(&adcConfig, 1);

    //Start the adc
    hardware.adc.Start();

    amy_start(1,1,1, 0);
    polyphony(0, 0);

    //Start calling the audio callback
    hardware.StartAudio(AudioCallback);

    // Loop forever
    for(;;) {}
}
