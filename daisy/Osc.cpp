#include "daisy_seed.h"
//#include "daisy_pod.h"
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
MidiUartHandler midi;
FIFO<MidiEvent, 128> event_log;

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
    uint8_t data[3] = {0x90, 0x00, 0x7f};
    uint8_t note = 40;
    for(uint8_t i=0;i<15;i++) {
	data[1] = note;
	amy_event_midi_message_received(data, 3, 0, start);
        start += 1000;
        note += 2;
    }
}

void event_polyphony(uint32_t start, uint16_t patch) {
    //patch = 256;
    amy_event e = amy_default_event();
    e.time = start;
    e.patch_number = patch;
    for (int i = 0; i < 20; ++i)
	e.voices[i] = i;
    amy_add_event(&e);
    start += 250;
    uint8_t note = 40;
    for(uint8_t i=0;i<20;i++) {
        e = amy_default_event();
        e.time = start;
        e.velocity = 0.5;
        e.voices[0] = i;
        e.midi_note = note;
        amy_add_event(&e);
        start += 1000;
        note += 2;
    }
}   


// Typical Switch case for Message Type.
void HandleMidiMessage(MidiEvent m)
{
    uint8_t data[3];
    data[0] = m.channel;
    data[1] = m.data[0];
    data[2] = m.data[1];
    bool good_event = true;
    switch(m.type)
    {
        case NoteOff:
	    data[0] |= 0x80;
	    break;
        case NoteOn:
	    data[0] |= 0x90;
	    break;
        case ControlChange:
	    data[0] |= 0xB0;
	    break;
        case ProgramChange:
	    data[0] |= 0xC0;
            break;
        case PitchBend:
	    data[0] |= 0xE0;
            break;
        default:
	    good_event = false;
	    break;
    }
    if (good_event) {
	uint32_t time = UINT32_MAX;
	amy_event_midi_message_received(data, 3, 0, time);
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
    amy_config_t amy_config = amy_default_config();
    amy_start(amy_config); // initializes amy 

    //polyphony(0, 0);
    amy_event e = amy_default_event();
    // Switch midi chan 1 voice to piano.
    e.synth = 1;
    e.patch_number = 256;
    e.num_voices = 6;
    e.time = 1000;
    amy_add_event(&e);

    //Start calling the audio callback
    hardware.StartAudio(AudioCallback);

    // Loop forever
    MidiUartHandler::Config midi_config;
    midi.Init(midi_config);
    midi.StartReceive();
    for(;;)
    {
        midi.Listen();
        // Handle MIDI Events
        while(midi.HasEvents())
        {
            HandleMidiMessage(midi.PopEvent());
        }
    }
}
