// amy_daisy.cpp
// Core daisy function to run AMY on the Daisy hardware/firmware environment.

#include "daisy_seed.h"
#include "daisysp.h"

extern "C" {
    #include "amy.h"
    #include "examples.h"
    extern void sequencer_check_and_fill();
}

using namespace daisy;
using namespace daisysp;

DaisySeed  hardware;
MidiUartHandler midi;

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    amy_render(0, AMY_OSCS, 0);
    short int * block = amy_fill_buffer();

    // Fill the block with samples.
    for(size_t i = 0; i < size; i += 2)
    {
        // Set the left and right outputs.
        out[i]     = (block[i]/32767.0);
        out[i + 1] = (block[i+1]/32767.0);
	// Copy the inputs.
	amy_in_block[i] = (short int)(in[i] * 32767.0);
	amy_in_block[i + 1] = (short int)(in[i + 1] * 32767.0);
    }
}

void test_audio_in() {
    amy_event e = amy_default_event();
    e.reset_osc = RESET_SYNTHS;
    amy_add_event(&e);
    e = amy_default_event();
    e.osc = 80;
    e.wave = AUDIO_IN0;
    e.pan_coefs[COEF_CONST] = 0;
    e.velocity = 10.0f;
    amy_add_event(&e);
    e.osc = 81;
    e.wave = AUDIO_IN1;
    e.pan_coefs[COEF_CONST] = 1.0f;
    amy_add_event(&e);
}

void midi_polyphony(uint32_t start, uint16_t patch) {
    // Play mulitple notes via note-ons to MIDI channel 1.
    uint8_t data[3] = {0x90, 0x00, 0x7f};
    uint8_t note = 40;
    for(uint8_t i=0;i<15;i++) {
	data[1] = note;
	amy_event_midi_message_received(data, 3, 0, start);
        start += 1000;
        note += 2;
    }
}

#define MAX_POLYPHONY 17

void event_polyphony(uint32_t start, uint16_t patch) {
    // Verify polyphony by directly configuring a lot of voices and playing them all at once.
    amy_event e = amy_default_event();
    e.time = start;
    e.patch_number = patch;
    for (int i = 0; i < MAX_POLYPHONY; ++i)
	e.voices[i] = i;
    amy_add_event(&e);
    start += 250;
    uint8_t note = 40;
    for(uint8_t i=0;i<MAX_POLYPHONY;i++) {
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


void HandleMidiMessage(MidiEvent m) {
    // MIDI message already part-digested by libDaisy, work back to raw bytes for AMY.
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

void sequencer_timer_callback(void* arg) {
    sequencer_check_and_fill();
}

void init_sequencer() {
    // Platform support of sequencer is to call sequencer_check_and_fill() every 0.5 ms.
    TimerHandle         tim5;
    TimerHandle::Config tim_cfg;

    /** TIM5 with IRQ enabled */
    tim_cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tim_cfg.enable_irq = true;

    /** Configure frequency (2000Hz) */
    auto tim_target_freq = 2000;
    auto tim_base_freq   = System::GetPClk2Freq();
    tim_cfg.period       = tim_base_freq / tim_target_freq;

    /** Initialize timer */
    tim5.Init(tim_cfg);
    tim5.SetCallback(sequencer_timer_callback);

    /** Start the timer, and generate callbacks at the end of each period */
    tim5.Start();
}

int main(void)
{
    // Configure and Initialize the Daisy Seed
    hardware.Configure();
    hardware.Init();
    hardware.SetAudioBlockSize(128);

    //How many samples we'll output per second
    float samplerate = hardware.AudioSampleRate();

    //Create an ADC configuration
    AdcChannelConfig adcConfig;
    //Add pin 21 as an analog input in this config. We'll use this to read the knob
    adcConfig.InitSingle(hardware.GetPin(21));

    //Set the ADC to use our configuration
    hardware.adc.Init(&adcConfig, 1);

    //Start the adc
    hardware.adc.Start();

    // Initialize Amy
    amy_config_t amy_config = amy_default_config();
    amy_config.features.startup_bleep = 1; 
    amy_start(amy_config); // initializes amy 

    // Start the sequencer timer for AMY.
    init_sequencer();

    //example_sequencer_drums_synth(1000);
    //event_polyphony(0, 0);
    //test_audio_in();

    // Switch midi chan 1 voice to piano.
    //amy_event e = amy_default_event();
    //e.synth = 1;
    //e.patch_number = 256;
    //e.num_voices = 6;
    //e.time = 1000;
    //amy_add_event(&e);

    //config_echo(0.1f, 500.0f, 500.0f, 0.5f, 0.3f);

    //Start calling the audio callback
    hardware.StartAudio(AudioCallback);

    // Loop forever while forwarding MIDI messages.
    MidiUartHandler::Config midi_config;
    midi.Init(midi_config);
    midi.StartReceive();
    for(;;) {
        amy_execute_deltas();
        midi.Listen();
        while(midi.HasEvents()) {
            HandleMidiMessage(midi.PopEvent());
        }
    }
}
