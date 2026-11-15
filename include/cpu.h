#ifndef CPU_H
#define CPU_H

#include <stdint.h>
//CPU configuration 

#define MEM_SIZE       4096
#define NUM_REGS       32
#define MAX_CYCLES     500

// CPU state - whole simulator's architectural state

typedef struct
{
    int32_t  regs[NUM_REGS];
    uint32_t pc;
    uint8_t  mem[MEM_SIZE];
    int      fetch_stopped; // to stop fectching new instructions 
    int      halted; // executed is terminated(used for err or termination condition)
} cpu_t;

// Register read and write functions
int32_t reg_read(cpu_t *cpu, uint32_t reg);
void reg_write(cpu_t *cpu, uint32_t reg, int32_t value);

#endif