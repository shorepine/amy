#ifdef __IMXRT1062__

#pragma once

#include "Arduino.h"
class AudioOutputI2S;

class Timers
{
    friend class AudioOutputI2S;
public:
    static const int TIMER_COUNT = 20;
    static const uint8_t TIMER_TOTAL = 19;
    static float TimeAvg[TIMER_COUNT];
    static float TimePeak[TIMER_COUNT];
    static float TimeMax[TIMER_COUNT];

    static void Lap(uint8_t timerIndex);
    static float GetAvg(uint8_t timerIndex=0);
    static float GetPeak(uint8_t timerIndex=0);
    static float GetMax(uint8_t timerIndex=0);
    static void Clear(uint8_t timerIndex=0);
    static float GetAvgPeriod();
    static float GetCpuLoad();
private:
    static int TimeFrameStart;
    static float TimeFramePeriod;
    static void ResetFrame();
    static void LapInner(uint8_t timerIndex);
};

#endif