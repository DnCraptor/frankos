#pragma once
#ifndef UNI_VGA_DISP
#define UNI_VGA_DISP

#include <stdint.h>

// fake (for compartimility)
void DispHstxCore1Exec(void (*fnc)());
void DispHstxCore1Wait();

void vga_init(void);


#endif // UNI_VGA_DISP
