
// Select result and commit it to rd; count retired instructions

#include "pipeline.h"
#include <stdio.h>
#include <string.h>
// Write-back value selection 

int32_t writeback_value(mem_wb_t *stage)
{
    if (stage->control.jump)
    {
        return stage->link_address;
    }

    if (stage->control.mem_read)
    {
        return stage->memory_data;
    }

    return stage->alu_result;
}

// Write-back stage 
 void write_back(cpu_t *cpu, pipeline_t *pipeline)
{
    mem_wb_t *stage = &pipeline->mem_wb;

    if (!stage->valid)
    {
        return;
    }

    if (stage->control.reg_write)
    {
        int32_t value = writeback_value(stage);

        reg_write(cpu, stage->rd, value);

        printf("    WB: %-5s -> x%-2u = %d\n",
               stage->instruction_name,
               stage->rd,
               value);
    }

    // Count each instruction once, at retirement in the WB stage.
    const char *name = stage->instruction_name;
    if (name != NULL &&
        (strcmp(name, "ADD") == 0 || strcmp(name, "SUB") == 0 || strcmp(name, "ADDI") == 0))
        pipeline->instruction_stats.arithmetic++;
    else if (name != NULL && strcmp(name, "LW") == 0)
        pipeline->instruction_stats.loads++;
    else if (name != NULL && strcmp(name, "SW") == 0)
        pipeline->instruction_stats.stores++;
    else if (name != NULL && strcmp(name, "BEQ") == 0)
        pipeline->instruction_stats.branches++;
    else if (name != NULL && (strcmp(name, "JAL") == 0 || strcmp(name, "JALR") == 0))
        pipeline->instruction_stats.jumps++;
    else if (name != NULL && strcmp(name, "LUI") == 0)
        pipeline->instruction_stats.upper_immediate++;
    else if (name != NULL && strcmp(name, "NOP") == 0)
        pipeline->instruction_stats.nops++;
    else
        pipeline->instruction_stats.unknown++;

    pipeline->retired++;
}