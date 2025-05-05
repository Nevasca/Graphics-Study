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
        // TODO
        return 0;
    }

    void GameTime::Start()
    {
    }

    void GameTime::Stop()
    {
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
