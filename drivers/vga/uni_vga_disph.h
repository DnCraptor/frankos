#pragma once
#ifndef UNI_VGA_DISP
#define UNI_VGA_DISP

#include <stdint.h>
#include <stdbool.h>

// fake (for compartimility)
void DispHstxCore1Exec(void (*fnc)());
void DispHstxCore1Wait();

void vga_init(void);
bool vga_set_mode(uint8_t mode);

#endif // UNI_VGA_DISP
