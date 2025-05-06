#include "GameTime.h"

#include "Application.h"

namespace Studies
{
    GameTime::GameTime()
    {
        int64_t countsPerSecond;
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&countsPerSecond));
        m_SecondsPerCount = 1.f / static_cast<double>(countsPerSecond);
    }

    float GameTime::GetDeltaTime() const
    {
        return static_cast<float>(m_DeltaTime);
    }

    float GameTime::GetTime() const
    {
        if (bIsStopped)
        {
            // If we are stopped, do not count the time that has passed since we stopped.
            // Moreover, if we previously already had a pause, the distance 
            // mStopTime - mBaseTime includes paused time, which we do not want to count.
            // To correct this, we can subtract the paused time from mStopTime:  
            //
            //                     |<--paused time-->|
            // ----*---------------*-----------------*------------*------------*------> time
            //  mBaseTime       mStopTime        startTime     mStopTime    mCurrTime

            return static_cast<float>((m_StopTime - m_PausedTime - m_BaseTime) * m_SecondsPerCount);
        }

        // The distance mCurrTime - mBaseTime includes paused time,
        // which we do not want to count.  To correct this, we can subtract 
        // the paused time from mCurrTime:  
        //
        //  (mCurrTime - mPausedTime) - mBaseTime 
        //
        //                     |<--paused time-->|
        // ----*---------------*-----------------*------------*------> time
        //  mBaseTime       mStopTime        startTime     mCurrTime

        return static_cast<float>((m_CurrentTime - m_PausedTime - m_BaseTime) * m_SecondsPerCount);
    }

    void GameTime::Start()
    {
        if (!bIsStopped)
        {
            return;
        }

        int64_t startTime;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&startTime));

        m_PausedTime += startTime - m_StopTime;
        m_PreviousTime = startTime;

        m_StopTime = 0;
        bIsStopped = false;
    }

    void GameTime::Stop()
    {
        if (bIsStopped)
        {
            return;
        }

        int64_t currentTime;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentTime));
        m_StopTime = currentTime;
        bIsStopped = true;
    }

    void GameTime::Reset()
    {
        int64_t currentTime;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentTime));

        m_BaseTime = currentTime;
        m_PreviousTime = currentTime;
        m_StopTime = 0;
        bIsStopped = false;
    }

    void GameTime::Tick()
    {
        if(bIsStopped)
        {
            m_DeltaTime = 0.0f;
            return;
        }

        // Get the time this frame
        int64_t currentTime;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentTime));
        m_CurrentTime = currentTime;

        // Time difference between this frame and the previous one
        m_DeltaTime = (m_CurrentTime - m_PreviousTime) * m_SecondsPerCount;

        // Prepare for the next frame
        m_PreviousTime = m_CurrentTime;

        // Force nonnegative. The DXSDK`s CDXUTTimer mentions that if the processor goes into a power save mode
        // or we shuffled to another processor, then m_DeltaTime can be negative
        if (m_DeltaTime < 0.0)
        {
            m_DeltaTime = 0.0;
        }
    }
}
