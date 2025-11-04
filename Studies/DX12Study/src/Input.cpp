#include "Input.h"

#include "Application.h"

namespace Studies
{
    bool Input::m_Inputs[Input::MAX_INPUTS] = {};
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
        return m_Inputs[static_cast<int>(button)];
    }

    Vector2 Input::GetMousePosition()
    {
        return m_MousePosition;
    }

    void Input::UpdateMouseButtonState(WPARAM buttonState)
    {
        m_Inputs[static_cast<int>(MouseButton::Left)] = (buttonState & MK_LBUTTON) != 0;
        m_Inputs[static_cast<int>(MouseButton::Right)] = (buttonState & MK_RBUTTON) != 0;
        m_Inputs[static_cast<int>(MouseButton::Middle)] = (buttonState & MK_MBUTTON) != 0;
    }
}
