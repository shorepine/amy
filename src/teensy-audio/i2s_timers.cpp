#ifdef __IMXRT1062__

#include "i2s_timers.h"

float Timers::TimeAvg[Timers::TIMER_COUNT];
float Timers::TimePeak[Timers::TIMER_COUNT];
float Timers::TimeMax[Timers::TIMER_COUNT];
int Timers::TimeFrameStart = 0;
float Timers::TimeFramePeriod = 1333.33f; // 64 sample blocks at 48khz

void Timers::Lap(uint8_t timerIndex)
{
    // don't allow external update of total timer
    if (timerIndex >= TIMER_TOTAL)
        return;
    LapInner(timerIndex);
}

void Timers::LapInner(uint8_t timerIndex)
{
    if (timerIndex >= TIMER_COUNT)
        return;

    int val = float(micros() - TimeFrameStart);
    
    TimeAvg[timerIndex] = 0.995 * TimeAvg[timerIndex] + 0.005 * val;
    
    TimePeak[timerIndex] = 0.995 * TimePeak[timerIndex];
    if (val > TimePeak[timerIndex])
        TimePeak[timerIndex] = val;

    if (val > TimeMax[timerIndex])
        TimeMax[timerIndex] = val;
}

float Timers::GetAvg(uint8_t timerIndex)
{
    if (timerIndex >= TIMER_COUNT)
        return -1;

    return Timers::TimeAvg[timerIndex];
}

float Timers::GetPeak(uint8_t timerIndex)
{
    if (timerIndex >= TIMER_COUNT)
        return -1;

    return Timers::TimePeak[timerIndex];
}

float Timers::GetMax(uint8_t timerIndex)
{
    if (timerIndex >= TIMER_COUNT)
        return -1;

    return Timers::TimeMax[timerIndex];
}

void Timers::Clear(uint8_t timerIndex)
{
    if (timerIndex >= TIMER_COUNT)
        return;

    Timers::TimeAvg[timerIndex] = 0;
    Timers::TimePeak[timerIndex] = 0;
    Timers::TimeMax[timerIndex] = 0;
}

float Timers::GetAvgPeriod()
{
    return TimeFramePeriod;
}

float Timers::GetCpuLoad()
{
    return Timers::TimePeak[Timers::TIMER_TOTAL] / TimeFramePeriod;
}

void Timers::ResetFrame()
{
    if (TimeFrameStart == 0)
    {
        TimeFrameStart = micros();
        return;
    }

    int oldStart = TimeFrameStart;
    TimeFrameStart = micros();
    TimeFramePeriod = 0.995f * TimeFramePeriod + 0.005f * (TimeFrameStart - oldStart);
}
#endif