#include "Screen.h"

namespace Studies
{
    int Screen::m_Width{800};
    int Screen::m_Height{600};

    void Screen::OnResize(int width, int height)
    {
        m_Width = width;
        m_Height = height;
    }

    float Screen::GetAspectRatio()
    {
        return static_cast<float>(m_Width) / static_cast<float>(m_Height);
    }

    int Screen::GetWidth()
    {
        return m_Width;
    }

    int Screen::GetHeight()
    {
        return m_Height;
    }
}
