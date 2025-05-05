#pragma once
#include <cstdint>

namespace Studies
{
    class GameTime
    {
    public:
        GameTime();

        float GetDeltaTime() const;
        float GetTime() const;
        void Start();
        void Stop();
        void Tick();

    private:

        double m_SecondsPerCount{0.0};
        double m_DeltaTime{-1.0f};

        int64_t m_BaseTime{0};
        int64_t m_PauseTime{0};
        int64_t m_StopTime{0};
        int64_t m_PreviousTime{0};
        int64_t m_CurrentTime{0};

        bool bIsStopped{false};
    };
}
