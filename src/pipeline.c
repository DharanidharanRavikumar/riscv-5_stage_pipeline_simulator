// This file contains logic to Move instructions through IF/ID/EX/MEM/WB and decide stalls/flushes 

#include "pipeline.h"
#include <stdio.h>
#include <string.h>

// Instruction fetch 

static uint32_t fetch(cpu_t *cpu)
{
    uint32_t address = cpu->pc;

    if (address + 3 >= MEM_SIZE)
    {
        return 0;
    }

    // RISC-V instructions are stored in little-endian byte order. 

    uint32_t b0 = cpu->mem[address + 0];
    uint32_t b1 = cpu->mem[address + 1];
    uint32_t b2 = cpu->mem[address + 2];
    uint32_t b3 = cpu->mem[address + 3];

    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

// Decode stage 

static id_ex_t decode( cpu_t *cpu, if_id_t *stage)
{
    id_ex_t result;

    memset(&result, 0, sizeof(result));

    if (!stage->valid)
    {
        return result;
    }

    decoded_instruction_t decoded = decode_instruction(stage->instruction);

    if (!decoded.valid)
    {
        printf("UNKNOWN instruction 0x%08X at PC 0x%03X\n",
               stage->instruction,
               stage->pc);

        cpu->halted = 1;
        return result;
    }

    result.valid = 1;

    result.pc = stage->pc;
    result.instruction = stage->instruction;

    result.rd = decoded.rd;
    result.rs1 = decoded.rs1;
    result.rs2 = decoded.rs2;

    result.immediate = decoded.immediate;

    result.uses_rs1 = decoded.uses_rs1;
    result.uses_rs2 = decoded.uses_rs2;

    result.control = decoded.control;

    result.instruction_name = decoded.instruction_name;

    // Read source registers during ID.

    if (result.uses_rs1)
    {
        result.read_data1 = reg_read(cpu, result.rs1);
    }

    if (result.uses_rs2)
    {
        result.read_data2 = reg_read(cpu, result.rs2);
    }

    printf("    ID: %-5s (rd=x%u rs1=x%u rs2=x%u imm=%d)\n",
           result.instruction_name,
           result.rd,
           result.rs1,
           result.rs2,
           result.immediate);

    return result;
}

//                                    Pipeline status helpers 

int pipeline_empty(pipeline_t *pipeline)
{
    return !pipeline->if_id.valid &&
           !pipeline->id_ex.valid &&
           !pipeline->ex_mem.valid &&
           !pipeline->mem_wb.valid;
}

//                                     One pipeline clock cycle 

void pipeline_step(cpu_t *cpu, pipeline_t *pipeline)
{
    /*
     * These are the NEXT pipeline registers.
     *
     * Every stage reads the OLD registers and writes its result
     * into these NEXT registers. This prevents an instruction from
     * travelling through multiple stages in a single clock cycle.
     */

    if_id_t  next_if_id;
    id_ex_t  next_id_ex;
    ex_mem_t next_ex_mem;
    mem_wb_t next_mem_wb;

    memset(&next_if_id, 0, sizeof(next_if_id));
    memset(&next_id_ex, 0, sizeof(next_id_ex));
    memset(&next_ex_mem, 0, sizeof(next_ex_mem));
    memset(&next_mem_wb, 0, sizeof(next_mem_wb));

    pipeline->cycles++;

    printf("\n====================================================\n");
    printf("CLOCK CYCLE %u\n", pipeline->cycles);
    printf("====================================================\n");

    // write back stage 

    write_back(cpu, pipeline);

    // mem stage 

    next_mem_wb = memory_access(cpu, &pipeline->ex_mem);

    // EX stage 

    int branch_taken = 0;
    uint32_t branch_target = 0;

    next_ex_mem = execute(
        &pipeline->id_ex,
        &pipeline->ex_mem,
        &pipeline->mem_wb,
        &branch_taken,
        &branch_target,
        pipeline);

    // Branch handling 

    if (branch_taken)
    {
        /*
          The branch is resolved in EX.
          The instruction currently in ID and the instruction
          currently in IF are younger wrong-path instructions.
          Both are flushed.
         */

        cpu->pc = branch_target;

        next_if_id.valid = 0;
        next_id_ex.valid = 0;

        pipeline->flushes += 2;
        pipeline->hazard_stats.branch_flush_events++;
        pipeline->hazard_stats.flushed_instructions += 2;

        printf("    FLUSH: wrong-path IF and ID instructions removed\n");
    }
    else
    {
        // Hazard detection 

        int load_use_hazard =
            detect_hazard(&pipeline->id_ex, &pipeline->if_id);

        if (load_use_hazard)
        {
            /*
              Hold PC and IF/ID.
              Insert a bubble into ID/EX.
              The older load continues into EX/MEM.
             */

            next_if_id = pipeline->if_id;
            next_id_ex.valid = 0;

            pipeline->stalls++;
            pipeline->hazard_stats.load_use_stalls++;
            pipeline->hazard_stats.bubbles++;

            printf("    STALL: load-use hazard detected\n");
            printf("    BUBBLE: inserted into ID/EX\n");
        }
        else
        {
            // ID stage 

            next_id_ex = decode(cpu, &pipeline->if_id);

            // IF stage 

            if (!cpu->fetch_stopped && !cpu->halted)
            {
                uint32_t instruction = fetch(cpu);

                if (instruction == 0)
                {
                    
                     //A zero word is used as the end marker in this educational test program.
                     //Stop fetching and allow the pipeline to drain.
                     

                    cpu->fetch_stopped = 1;
                    printf("    IF: end marker found; stopping fetch\n");
                }
                else
                {
                    next_if_id.valid = 1;
                    next_if_id.pc = cpu->pc;
                    next_if_id.instruction = instruction;

                    printf("    IF: PC=0x%03X instruction=0x%08X\n",
                           cpu->pc,
                           instruction);

                    cpu->pc += 4;
                }
            }
        }
    }

    // Updating all pipeline registers together 

    pipeline->if_id = next_if_id;
    pipeline->id_ex = next_id_ex;
    pipeline->ex_mem = next_ex_mem;
    pipeline->mem_wb = next_mem_wb;

    cpu->regs[0] = 0;
}