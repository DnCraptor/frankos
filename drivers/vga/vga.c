#pragma GCC optimize("Ofast")

#include <pico.h>
#include <pico/sync.h>
#include <pico/multicore.h>

static semaphore_t vga_start_semaphore;
volatile static void (*_fnc)();

void __time_critical_func(render_core)() {
    __dmb();
    _fnc();
    sem_acquire_blocking(&vga_start_semaphore);
    // TODO:
}

void DispHstxCore1Exec(void (*fnc)()) {
    _fnc = fnc;
    sem_init(&vga_start_semaphore, 0, 1);
    multicore_launch_core1(render_core);
}

void DispHstxCore1Wait() {
    sem_release(&vga_start_semaphore);
}
