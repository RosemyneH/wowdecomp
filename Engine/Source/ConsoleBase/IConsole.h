#ifndef ENGINE_SOURCE_CONSOLEBASE_ICONSOLE_H
#define ENGINE_SOURCE_CONSOLEBASE_ICONSOLE_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/ConsoleBase/Types.h"

void ConsoleCommandExecute(const char* commandLine, int32_t addToHistory);
void ConsolePostClose();
void ConsolePrintf(const char* format, ...);
void ConsoleWrite(const char* str, COLOR_T color);
void ConsoleSetLogTee(int32_t enabled);
int32_t ConsoleLogTeeEnabled();
void ConsoleWriteA(const char* format, COLOR_T color, ...);

#endif
