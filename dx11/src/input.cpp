#include "input.h"

namespace {
void SetCursorVisible(bool visible) {
  if (visible) {
    while (ShowCursor(TRUE) < 0) {
    }
  } else {
    while (ShowCursor(FALSE) >= 0) {
    }
  }
}

POINT GetClientCenter(HWND hwnd) {
  POINT center{0, 0};
  if (!hwnd) {
    return center;
  }
  RECT rect{};
  GetClientRect(hwnd, &rect);
  center.x = (rect.right - rect.left) / 2;
  center.y = (rect.bottom - rect.top) / 2;
  return center;
}

void CenterCursor(HWND hwnd) {
  if (!hwnd) {
    return;
  }
  POINT center = GetClientCenter(hwnd);
  ClientToScreen(hwnd, &center);
  SetCursorPos(center.x, center.y);
}
}  // namespace

void InitInput(InputState& input, HWND hwnd) {
  input.hwnd = hwnd;
  input.mouse_captured = false;
  input.escape_down = false;
  input.lmb_down = false;
  input.rmb_down = false;
  input.lmb_pressed = false;
  input.rmb_pressed = false;
  input.jump_down = false;
  input.jump_pressed = false;
}

void UpdateClipRect(InputState& input) {
  if (!input.mouse_captured || !input.hwnd) {
    return;
  }
  RECT rect{};
  GetClientRect(input.hwnd, &rect);
  POINT tl{rect.left, rect.top};
  POINT br{rect.right, rect.bottom};
  ClientToScreen(input.hwnd, &tl);
  ClientToScreen(input.hwnd, &br);
  RECT clip{tl.x, tl.y, br.x, br.y};
  ClipCursor(&clip);
}

void SetMouseCaptured(InputState& input, bool captured) {
  if (captured == input.mouse_captured) {
    return;
  }
  input.mouse_captured = captured;
  if (captured) {
    SetCursorVisible(false);
    UpdateClipRect(input);
    CenterCursor(input.hwnd);
  } else {
    ClipCursor(nullptr);
    SetCursorVisible(true);
  }
}

void HandleWindowActivate(InputState& input, bool active) {
  if (!active) {
    SetMouseCaptured(input, false);
  }
}

void HandleLButtonDown(InputState& input) {
  if (!input.mouse_captured) {
    SetMouseCaptured(input, true);
  }
}

void UpdateInput(InputState& input) {
  const bool esc_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
  if (esc_down && !input.escape_down) {
    SetMouseCaptured(input, !input.mouse_captured);
  }
  input.escape_down = esc_down;

  const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
  const bool jump = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
  input.lmb_pressed = lmb && !input.lmb_down;
  input.rmb_pressed = rmb && !input.rmb_down;
  input.lmb_down = lmb;
  input.rmb_down = rmb;
  input.jump_pressed = jump && !input.jump_down;
  input.jump_down = jump;

  input.mouse_dx = 0.0f;
  input.mouse_dy = 0.0f;
  if (input.mouse_captured && input.hwnd) {
    POINT center = GetClientCenter(input.hwnd);
    ClientToScreen(input.hwnd, &center);
    POINT cursor{};
    GetCursorPos(&cursor);
    const int dx = cursor.x - center.x;
    const int dy = cursor.y - center.y;
    input.mouse_dx = static_cast<float>(dx);
    input.mouse_dy = static_cast<float>(dy);
    if (dx != 0 || dy != 0) {
      CenterCursor(input.hwnd);
    }
  }

  input.move_forward = 0;
  input.move_right = 0;
  input.move_up = 0;
  if (GetAsyncKeyState('W') & 0x8000) {
    ++input.move_forward;
  }
  if (GetAsyncKeyState('S') & 0x8000) {
    --input.move_forward;
  }
  if (GetAsyncKeyState('D') & 0x8000) {
    ++input.move_right;
  }
  if (GetAsyncKeyState('A') & 0x8000) {
    --input.move_right;
  }
  if (jump) {
    ++input.move_up;
  }
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
    --input.move_up;
  }
  input.speed_boost = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}
