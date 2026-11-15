// forwarding_value, detect_hazard : Select newer operand values and detect load-use dependency

#include "pipeline.h"

// Forwarding unit

 int32_t forwarding_value(
    uint8_t source_register,
    int32_t original_value,
    ex_mem_t *ex_mem,
    mem_wb_t *mem_wb,
    hazard_stats_t *stats)
{
    if (source_register == 0)
    {
        return 0;
    }

    // EX/MEM has priority because it contains the newest result.
     // A load result is not ready until the MEM stage, so it is
     // deliberately not forwarded from EX/MEM.
    

    if (ex_mem->valid &&
        ex_mem->control.reg_write &&
        !ex_mem->control.mem_read &&
        ex_mem->rd != 0 &&
        ex_mem->rd == source_register)
    {
        if (stats != NULL) stats->ex_mem_forwardings++;
        if (ex_mem->control.jump)
        {
            return ex_mem->link_address;
        }

        return ex_mem->alu_result;
    }

    /* Forward a value from MEM/WB when required. */

    if (mem_wb->valid &&
        mem_wb->control.reg_write &&
        mem_wb->rd != 0 &&
        mem_wb->rd == source_register)
    {
        if (stats != NULL) stats->mem_wb_forwardings++;
        return writeback_value(mem_wb);
    }

    return original_value;
}

// Load-use hazard detection
 int detect_hazard(
    id_ex_t *old_id_ex,
    if_id_t *old_if_id)
{
    if (!old_id_ex->valid || !old_if_id->valid)
    {
        return 0;
    }

    // Only a load creates this one-cycle load-use dependency.

    if (!old_id_ex->control.mem_read || old_id_ex->rd == 0)
    {
        return 0;
    }

    decoded_instruction_t next_instruction =
        decode_instruction(old_if_id->instruction);

    if (!next_instruction.valid)
    {
        return 0;
    }

    if (next_instruction.uses_rs1 &&
        next_instruction.rs1 == old_id_ex->rd)
    {
        return 1;
    }

    if (next_instruction.uses_rs2 &&
        next_instruction.rs2 == old_id_ex->rd)
    {
        return 1;
    }

    return 0;
}
