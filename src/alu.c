// This file Compute's arithmetic/address/branch/jump results and request forwarding

#include "pipeline.h"
#include <stdio.h>
#include <string.h>

ex_mem_t execute(
    id_ex_t *stage,
    ex_mem_t *old_ex_mem,
    mem_wb_t *old_mem_wb,
    int *branch_taken, // decides whether it's branch instrn or not (either 0 or 1)
    uint32_t *branch_target,
    pipeline_t *pipeline) // calculates the jump target (eg: PC + 16)
{
    ex_mem_t result;

    memset(&result, 0, sizeof(result));

    *branch_taken = 0;
    *branch_target = 0;

    if (!stage->valid)
    {
        return result;
    }

    result.valid = 1;

    result.pc = stage->pc;
    result.instruction = stage->instruction;

    result.rd = stage->rd;

    result.control = stage->control;
    result.instruction_name = stage->instruction_name;

    int32_t operand1 = stage->read_data1;
    int32_t operand2 = stage->read_data2;

    // Apply forwarding to source operands

    if (stage->uses_rs1)
    {
        operand1 = forwarding_value(
            stage->rs1,
            operand1,
            old_ex_mem,
            old_mem_wb,
            &pipeline->hazard_stats);
    }

    if (stage->uses_rs2)
    {
        operand2 = forwarding_value(
            stage->rs2,
            operand2,
            old_ex_mem,
            old_mem_wb,
            &pipeline->hazard_stats);
    }

    result.store_data = operand2;

    int32_t alu_operand2 = operand2;

    if (stage->control.alu_src_imm)
    {
        alu_operand2 = stage->immediate;
    }

    // ALU operations 

    if (stage->control.alu_op == ALU_ADD)
    {
        result.alu_result = operand1 + alu_operand2;
    }
    else if (stage->control.alu_op == ALU_SUB)
    {
        result.alu_result = operand1 - alu_operand2;
    }

    // LUI writes its upper immediate directly

    if (strcmp(stage->instruction_name, "LUI") == 0)
    {
        result.alu_result = stage->immediate;
    }

    // JAL writes PC + 4 and calculates its target separately

    if (stage->control.jump && !stage->control.jalr)
    {
        result.link_address = (int32_t)(stage->pc + 4);

        *branch_taken = 1;
        *branch_target = stage->pc + (uint32_t)stage->immediate;
    }

    // JALR uses the forwarded rs1 value plus its immediate

    if (stage->control.jalr)
    {
        result.link_address = (int32_t)(stage->pc + 4);

        *branch_taken = 1;
        *branch_target =
            (uint32_t)(operand1 + stage->immediate) & ~1U;
    }

    // BEQ compares the forwarded source operands.

    if (stage->control.branch)
    {
        if (operand1 == operand2)
        {
            pipeline->hazard_stats.branch_taken++;
            *branch_taken = 1;
            *branch_target =
                stage->pc + (uint32_t)stage->immediate;
        }
        else
        {
            pipeline->hazard_stats.branch_not_taken++;
        }
    }

    printf("    EX: %-5s", stage->instruction_name);

    if (stage->control.alu_op != ALU_NONE)
    {
        printf(" ALU result = %d", result.alu_result);
    }

    if (*branch_taken)
    {
        printf(" -> branch/jump target = 0x%03X", *branch_target);
    }

    printf("\n");

    return result;
}