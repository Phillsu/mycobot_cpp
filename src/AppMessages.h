// AppMessages.h
//
// All custom window messages used by the app, kept in one place so the
// WM_APP+n values can't collide between modules.
#pragma once
#include <windows.h>

namespace mycobot {

// wParam = points sent so far, lParam = total points
constexpr UINT WM_APP_PROGRESS        = WM_APP + 1;
// wParam = 1 if completed normally, 0 if stopped/cancelled/error
constexpr UINT WM_APP_DONE            = WM_APP + 2;
// lParam = new'd wide C-string; receiver must delete[]
constexpr UINT WM_APP_LOG             = WM_APP + 3;
// lParam = new'd PoseUpdate*; receiver must delete
constexpr UINT WM_APP_POSE            = WM_APP + 4;
// posted by the whiteboard whenever the stroke list changes
constexpr UINT WM_APP_STROKES_CHANGED = WM_APP + 5;
// lParam = new'd std::vector<Vec3>* of path points the simulator could not
// reach; receiver must delete
constexpr UINT WM_APP_UNREACHABLE     = WM_APP + 6;

} // namespace mycobot
