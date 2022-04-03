#pragma once
#include "std_include.hpp"

extern "C" {

void _sldt(uint16_t* ldtr);
void _ltr(uint16_t tr);
void _str(uint16_t* tr);
void __lgdt(void* gdtr);

void _sgdt(void*);

[[ noreturn ]] void vm_launch();
[[ noreturn ]] void vm_exit();
[[ noreturn ]] void restore_context(CONTEXT* context);

}
