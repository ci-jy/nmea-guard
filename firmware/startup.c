/* Vector table and reset handler for a Cortex-M3. Copies .data from flash,
 * clears .bss, runs static constructors and calls main(). C rather than C++
 * because C++ doesn't allow calling main(). */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

int main(void);
/* semihost.cpp */
void platform_write(const char* text);
void platform_exit(int status) __attribute__((noreturn));

void Reset_Handler(void) {
    for (uint32_t *src = &_sidata, *dst = &_sdata; dst < &_edata;) *dst++ = *src++;
    for (uint32_t* dst = &_sbss; dst < &_ebss;) *dst++ = 0;
    for (void (**ctor)(void) = __init_array_start; ctor < __init_array_end; ++ctor) (*ctor)();
    platform_exit(main());
}

/* Any fault ends the run with a failure instead of hanging QEMU. */
void Fault_Handler(void) {
    platform_write("FAULT: hard fault or exception\n");
    platform_exit(3);
}

typedef void (*Handler)(void);

/* The first word is the initial stack pointer, the rest are handlers. */
struct VectorTable {
    uint32_t* initial_sp;
    Handler handlers[15];
};

__attribute__((section(".isr_vector"), used)) const struct VectorTable kVectorTable = {&_estack, {
    Reset_Handler,
    Fault_Handler, /* NMI */
    Fault_Handler, /* HardFault */
    Fault_Handler, /* MemManage */
    Fault_Handler, /* BusFault */
    Fault_Handler, /* UsageFault */
    0, 0, 0, 0,
    Fault_Handler, /* SVCall */
    Fault_Handler, /* DebugMon */
    0,
    Fault_Handler, /* PendSV */
    Fault_Handler, /* SysTick */
}};
