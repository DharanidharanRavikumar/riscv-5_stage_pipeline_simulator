// This cpu.c is used to read/write the architectural integer register file, make x0 =0

#include "cpu.h"

// Register read and write 

int32_t reg_read(cpu_t *cpu, uint32_t reg)
{
    if (reg == 0){
        return 0;
    }
    return cpu->regs[reg];
}

void reg_write(cpu_t *cpu, uint32_t reg, int32_t value)
{
    if (reg != 0){
        cpu->regs[reg] = value;
    }
    //  As of RISC-V ISA reg x0 is always zero

    cpu->regs[0] = 0;
}