#pragma once

#include <windows.h>

struct InputState {
  HWND hwnd = nullptr;
  bool mouse_captured = false;
  bool escape_down = false;
  bool lmb_down = false;
  bool rmb_down = false;
  bool lmb_pressed = false;
  bool rmb_pressed = false;
  bool jump_down = false;
  bool jump_pressed = false;
  bool crouch_down = false;
  float mouse_dx = 0.0f;
  float mouse_dy = 0.0f;
  int move_forward = 0;
  int move_right = 0;
  int move_up = 0;
  bool speed_boost = false;
};

void InitInput(InputState& input, HWND hwnd);
void SetMouseCaptured(InputState& input, bool captured);
void UpdateClipRect(InputState& input);
void HandleWindowActivate(InputState& input, bool active);
void HandleLButtonDown(InputState& input);
void UpdateInput(InputState& input);
