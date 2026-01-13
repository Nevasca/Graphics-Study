#pragma once
#include <cstdint>
#include <windows.h>

#include "Vector2.h"

namespace Studies
{
    class Input
    {
    public:
        enum class MouseButton : uint8_t
        {
            Left = 0,
            Right,
            Middle
        };

        static void OnMouseDown(WPARAM buttonState, int x, int y);
        static void OnMouseUp(WPARAM buttonState, int x, int y);
        static void OnMouseMove(WPARAM buttonState, int x, int y);

        static bool GetMouseButton(const MouseButton button);
        static bool GetKeyboardKey(const char key);
        static Vector2 GetMousePosition();

    private:
        static constexpr int MAX_MOUSE_INPUTS = 3;
        
        static bool m_MouseInputs[MAX_MOUSE_INPUTS];
        static Vector2 m_MousePosition;

        static void UpdateMouseButtonState(WPARAM buttonState);
    };
}
