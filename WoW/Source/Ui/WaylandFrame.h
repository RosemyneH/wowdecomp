#pragma once

#include <cstdint>

struct lua_State;

void WaylandFrame_Register(lua_State* L);
void WaylandFrame_Present();
int32_t WaylandFrame_Open(const char* spec, int32_t width, int32_t height, int32_t* frameRef, char* error, uint32_t errorLen);
void WaylandFrame_Close(int32_t id);
