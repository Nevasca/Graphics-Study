#include "Input.h"

#include "Application.h"

namespace Studies
{
    bool Input::m_MouseInputs[Input::MAX_MOUSE_INPUTS] = {};
    Vector2 Input::m_MousePosition = {};

    void Input::OnMouseDown(WPARAM buttonState, int x, int y)
    {
        UpdateMouseButtonState(buttonState);
        OnMouseMove(buttonState, x, y);
    }

    void Input::OnMouseUp(WPARAM buttonState, int x, int y)
    {
        UpdateMouseButtonState(buttonState);
        OnMouseMove(buttonState, x, y);
    }

    void Input::OnMouseMove(WPARAM buttonState, int x, int y)
    {
        m_MousePosition.X = static_cast<float>(x);
        m_MousePosition.Y = static_cast<float>(y);
    }

    bool Input::GetMouseButton(const MouseButton button)
    {
        return m_MouseInputs[static_cast<int>(button)];
    }

    bool Input::GetKeyboardKey(const char key)
    {
        return GetAsyncKeyState(key) & 0x8000;
    }

    Vector2 Input::GetMousePosition()
    {
        return m_MousePosition;
    }

    void Input::UpdateMouseButtonState(WPARAM buttonState)
    {
        m_MouseInputs[static_cast<int>(MouseButton::Left)] = (buttonState & MK_LBUTTON) != 0;
        m_MouseInputs[static_cast<int>(MouseButton::Right)] = (buttonState & MK_RBUTTON) != 0;
        m_MouseInputs[static_cast<int>(MouseButton::Middle)] = (buttonState & MK_MBUTTON) != 0;
    }
}
