/* 
    Configuration for AMY. We make guesses here based on your arch, but feel free to make changes for your own code.
*/
#include "amy.h"

// You can edit these here if you want to 
#define AMY_BLOCK_SIZE 256
#define BLOCK_SIZE_BITS 8 // log2 of BLOCK_SIZE
#define AMY_OSCS 120
#define AMY_SAMPLE_RATE 44100 
#define AMY_NCHANS 2
#define AMY_USE_FIXEDPOINT


// These are overriden for you if you include pcm_X.h {tiny, small, large}

#ifdef PCM_MUTABLE
#define PMUTATE
#define PCM_USER 16
#define PCM_INIT ,0,NULL
#else
#define PMUTATE const
#define PCM_INIT
#define PCM_USER 0
#endif

extern const int16_t pcm[];
extern PMUTATE pcm_map_t pcm_map[];
extern const uint16_t pcm_samples;

#define AMY_CORES amy_global.cores
#define AMY_HAS_REVERB amy_global.has_reverb
#define AMY_HAS_CHORUS amy_global.has_chorus
#define AMY_MAX_DRIFT_MS 20000   // ms of time you can schedule ahead before synth recomputes time base
#define AMY_KS_OSCS 1            // How many karplus-strong oscillators to keep track of (0 disables KS)
#define AMY_HAS_PARTIALS 1       // 1 = Make partials available
#define AMY_HAS_CUSTOM 0         // 1 = Make custom oscillators available
#define PCM_AMY_SAMPLE_RATE 22050
#define AMY_EVENT_FIFO_LEN 2000


//If using an ESP, tell us how to allocate ram here. Not used on other platforms.
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#ifdef TULIP
#define EVENTS_RAM_CAPS MALLOC_CAP_SPIRAM
#define SYNTH_RAM_CAPS MALLOC_CAP_SPIRAM
#define BLOCK_RAM_CAPS MALLOC_CAP_DEFAULT
#define FBL_RAM_CAPS MALLOC_CAP_DEFAULT
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
