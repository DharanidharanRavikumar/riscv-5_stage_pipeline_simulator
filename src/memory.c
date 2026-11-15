// To Perform LW/SW data-memory operations

#include "memory.h"
#include <stdio.h>
#include <string.h>
// Memory read and write 

static int memory_read_word(cpu_t *cpu, int32_t address, int32_t *value)
{
    if (address < 0 || address + 3 >= MEM_SIZE)
    {
        printf("MEMORY ERROR: invalid read address %d\n", address);
        return 0;
    }

    uint32_t a = (uint32_t)address;

    uint32_t b0 = cpu->mem[a + 0];
    uint32_t b1 = cpu->mem[a + 1];
    uint32_t b2 = cpu->mem[a + 2];
    uint32_t b3 = cpu->mem[a + 3];

    *value = (int32_t)(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));

    return 1;
}

static int memory_write_word(cpu_t *cpu, int32_t address, int32_t value)
{
    if (address < 0 || address + 3 >= MEM_SIZE)
    {
        printf("MEMORY ERROR: invalid write address %d\n", address);
        return 0;
    }

    uint32_t a = (uint32_t)address;
    uint32_t data = (uint32_t)value;

    cpu->mem[a + 0] = (uint8_t)(data & 0xFF);
    cpu->mem[a + 1] = (uint8_t)((data >> 8) & 0xFF);
    cpu->mem[a + 2] = (uint8_t)((data >> 16) & 0xFF);
    cpu->mem[a + 3] = (uint8_t)((data >> 24) & 0xFF);

    return 1;
}

// Memory stage

 mem_wb_t memory_access(cpu_t *cpu, ex_mem_t *stage)
{
    mem_wb_t result;

    memset(&result, 0, sizeof(result));

    if (!stage->valid)
    {
        return result;
    }

    result.valid = 1;

    result.pc = stage->pc;
    result.instruction = stage->instruction;

    result.rd = stage->rd;

    result.alu_result = stage->alu_result;
    result.link_address = stage->link_address;

    result.control = stage->control;
    result.instruction_name = stage->instruction_name;

    if (stage->control.mem_read)
    {
        if (!memory_read_word(cpu, stage->alu_result, &result.memory_data))
        {
            cpu->halted = 1;
        }
        else
        {
            printf("    MEM: LW from address %d -> %d\n",
                   stage->alu_result,
                   result.memory_data);
        }
    }
    else if (stage->control.mem_write)
    {
        if (!memory_write_word(cpu, stage->alu_result, stage->store_data))
        {
            cpu->halted = 1;
        }
        else
        {
            printf("    MEM: SW value %d -> address %d\n",
                   stage->store_data,
                   stage->alu_result);
        }
    }

    return result;
}