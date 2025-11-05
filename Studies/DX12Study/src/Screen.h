#pragma once

namespace Studies
{
    class Screen
    {
    public:
        static void OnResize(int width, int height);

        static float GetAspectRatio();
        static int GetWidth();
        static int GetHeight();
        
    private:
        static int m_Width;
        static int m_Height;
    };
}
