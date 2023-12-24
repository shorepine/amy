/* 
    Configuration for AMY. We make guesses here based on your arch, but feel free to make changes for your own code.
*/


// Feel free to override any of these in your code
extern const uint16_t amy_oscs;
extern const uint16_t amy_block_size;
extern const uint8_t amy_block_size_bits;
extern const uint8_t amy_cores;
extern const uint8_t amy_nchans;
extern const uint16_t amy_sample_rate;
extern const uint8_t amy_has_chorus;
extern const uint8_t amy_has_reverb;


#define AMY_BLOCK_SIZE amy_block_size
#define AMY_HAS_REVERB amy_has_reverb
#define AMY_HAS_CHORUS amy_has_chorus
#define AMY_OSCS amy_oscs
#define AMY_SAMPLE_RATE amy_sample_rate
#define AMY_NCHANS amy_nchans
#define AMY_CORES amy_cores
#define BLOCK_SIZE_BITS amy_block_size_bits



#define AMY_EVENT_FIFO_LEN 1000  // number of events the queue can store
#define AMY_MAX_DRIFT_MS 20000   // ms of time you can schedule ahead before synth recomputes time base
#define AMY_KS_OSCS 1            // How many karplus-strong oscillators to keep track of (0 disables KS)
#define AMY_HAS_PARTIALS 1       // 1 = Make partials available

//If using an ESP, tell us how to allocate ram here. Not used on other platforms.
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#ifdef TULIP
#define EVENTS_RAM_CAPS MALLOC_CAP_SPIRAM
#define SYNTH_RAM_CAPS MALLOC_CAP_SPIRAM
#define BLOCK_RAM_CAPS MALLOC_CAP_INTERNAL
#define FBL_RAM_CAPS MALLOC_CAP_INTERNAL
#define CHORUS_RAM_CAPS MALLOC_CAP_SPIRAM 
#define REVERB_RAM_CAPS MALLOC_CAP_SPIRAM 
#else
#define EVENTS_RAM_CAPS MALLOC_CAP_DEFAULT
#define SYNTH_RAM_CAPS MALLOC_CAP_DEFAULT
#define BLOCK_RAM_CAPS MALLOC_CAP_DEFAULT
#define FBL_RAM_CAPS MALLOC_CAP_DEFAULT
#define CHORUS_RAM_CAPS MALLOC_CAP_DEFAULT
#define REVERB_RAM_CAPS MALLOC_CAP_DEFAULT
#endif
#else
#define EVENTS_RAM_CAPS 0
#define SYNTH_RAM_CAPS 0
#define BLOCK_RAM_CAPS 0
#define FBL_RAM_CAPS 0
#define CHORUS_RAM_CAPS 0 
#define REVERB_RAM_CAPS 0 
#endif

// 0.5 Hz modulation at 50% depth of 320 samples (i.e., 80..240 samples = 2..6 ms), mix at 0 (inaudible).
#define CHORUS_DEFAULT_LFO_FREQ 0.5
#define CHORUS_DEFAULT_MOD_DEPTH 0.5
#define CHORUS_DEFAULT_LEVEL 0
#define CHORUS_DEFAULT_MAX_DELAY 320
#define CHORUS_ARATE
// Chorus control modulator is hardcoded to OSC 63 (NOSCS - 1)
#define CHORUS_MOD_SOURCE (AMY_OSCS - 1)

// center frequencies for the EQ
#define EQ_CENTER_LOW 800.0
#define EQ_CENTER_MED 2500.0
#define EQ_CENTER_HIGH 7000.0

// reverb setup
#define REVERB_DEFAULT_LEVEL 0
#define REVERB_DEFAULT_LIVENESS 0.85f
#define REVERB_DEFAULT_DAMPING 0.5f
#define REVERB_DEFAULT_XOVER_HZ 3000.0f

#define DELAY_LINE_LEN 512  // 11 ms @ 44 kHz

// D is how close the sample gets to the clip limit before the nonlinearity engages.  
// So D=0.1 means output is linear for -0.9..0.9, then starts clipping.
#define CLIP_D 0.1
#define MAX_VOLUME 11.0
#define MINIMUM_SCALE 0.000190 // computed from TRUE_EXPONENTIAL's end point after a while 
#define BREAKPOINT_EPS 0.0002
