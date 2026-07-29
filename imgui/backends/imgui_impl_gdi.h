// dear imgui: Renderer for GDI
// This needs to be used along with a Platform Binding (e.g. Win32)

// Implemented features:
//  [ ] Renderer: 
//  [ ] Renderer: 

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you are new to dear imgui, read examples/README.txt and read the documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

IMGUI_IMPL_API bool     ImGui_ImplGDI_Init(HWND hwnd);
IMGUI_IMPL_API void     ImGui_ImplGDI_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplGDI_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplGDI_RenderDrawData(ImDrawData* draw_data);

IMGUI_IMPL_API void     ImGui_ImplGDI_SetBackgroundColor(ImVec4* BackgroundColor);
