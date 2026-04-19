#pragma once
#ifndef UNI_VGA_DISP
#define UNI_VGA_DISP

#include <stdint.h>

// fake (for compartimility)
#define DISPHSTX_FORMAT_4_PAL 1
#define DISPHSTX_FORMAT_8_PAL 2
void DispHstxCore1Exec(void (*fnc)());
void DispHstxCore1Wait();

#endif // UNI_VGA_DISP
