extern "C" {
#include "amy.h"
}

static_assert(AMY_SAMPLE_RATE == 48000,
              "Android AMY service requires a 48 kHz AMY build");
static_assert(AMY_BLOCK_SIZE == 128,
              "Android AMY service requires 128-frame AMY blocks");
static_assert(AMY_NCHANS == 2,
              "Android AMY service expects stereo AMY output");
