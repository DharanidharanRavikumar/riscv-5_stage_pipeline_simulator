// decoder.c contains functions that extracts the  32-bit instruction bits into decoded fields, immediates and control flags

#include "instruction.h"
#include <string.h>
// Field extraction helpers

static inline uint32_t opcode(uint32_t insn){
    return insn & 0x7F;}

static inline uint32_t rd(uint32_t insn){
    return (insn >> 7) & 0x1F;
}

static inline uint32_t funct3(uint32_t insn){
    return (insn >> 12) & 0x07;
}

static inline uint32_t rs1(uint32_t insn){
    return (insn >> 15) & 0x1F;
}

static inline uint32_t rs2(uint32_t insn){
    return (insn >> 20) & 0x1F;
}

static inline uint32_t funct7(uint32_t insn){
    return (insn >> 25) & 0x7F;
}

// Sign-extend a value containing the specified number of bits

static int32_t sign_extend(uint32_t value, uint32_t bits)
{
    uint32_t sign_bit = 1U << (bits - 1);

    if (value & sign_bit)
    {
        value |= (~0U << bits);
    }
    return (int32_t)value;
}

// I-type immediate: instruction bits [31:20]

static int32_t imm_i(uint32_t insn)
{
    return sign_extend(insn >> 20, 12);
}

// S-type immediate: instruction bits [31:25] and [11:7]

static int32_t imm_s(uint32_t insn)
{
    uint32_t upper = (insn >> 25) & 0x7F;
    uint32_t lower = (insn >> 7) & 0x1F;

    uint32_t immediate = (upper << 5) | lower;

    return sign_extend(immediate, 12);
}

// B-type immediate: scattered branch-offset bits

static int32_t imm_b(uint32_t insn)
{
    uint32_t bit12    = (insn >> 31) & 0x1;
    uint32_t bit11    = (insn >> 7)  & 0x1;
    uint32_t bits10_5 = (insn >> 25) & 0x3F;
    uint32_t bits4_1  = (insn >> 8)  & 0x0F;

    uint32_t immediate =
        (bit12 << 12) |
        (bit11 << 11) |
        (bits10_5 << 5) |
        (bits4_1 << 1);

    return sign_extend(immediate, 13);
}

// J-type immediate: scattered jump-offset bits

static int32_t imm_j(uint32_t insn)
{
    uint32_t bit20     = (insn >> 31) & 0x1;
    uint32_t bits10_1  = (insn >> 21) & 0x3FF;
    uint32_t bit11     = (insn >> 20) & 0x1;
    uint32_t bits19_12 = (insn >> 12) & 0xFF;

    uint32_t immediate =
        (bit20 << 20) |
        (bits19_12 << 12) |
        (bit11 << 11) |
        (bits10_1 << 1);

    return sign_extend(immediate, 21);
}

// U-type immediate: upper 20 bits.

static int32_t imm_u(uint32_t insn)
{
    return (int32_t)(insn & 0xFFFFF000);
}

// Instruction decoding

decoded_instruction_t decode_instruction(uint32_t insn)
{
    decoded_instruction_t result;

    memset(&result, 0, sizeof(result));

    result.instruction = insn;
    result.rd = (uint8_t)rd(insn);
    result.rs1 = (uint8_t)rs1(insn);
    result.rs2 = (uint8_t)rs2(insn);

    result.instruction_name = "UNKNOWN";
    result.valid = 1;

    // The instruction is identified with the same MATCH / MASK comparisons used in the sequential simulator.

    if ((insn & MASK_ADD) == MATCH_ADD)
    {
        result.instruction_name = "ADD";

        result.uses_rs1 = 1;
        result.uses_rs2 = 1;

        result.control.reg_write = 1;
        result.control.alu_op = ALU_ADD;
    }
    else if ((insn & MASK_SUB) == MATCH_SUB)
    {
        result.instruction_name = "SUB";

        result.uses_rs1 = 1;
        result.uses_rs2 = 1;

        result.control.reg_write = 1;
        result.control.alu_op = ALU_SUB;
    }
    else if ((insn & MASK_ADDI) == MATCH_ADDI)
    {
        result.instruction_name = "ADDI";

        result.immediate = imm_i(insn);
        result.uses_rs1 = 1;

        result.control.reg_write = 1;
        result.control.alu_src_imm = 1;
        result.control.alu_op = ALU_ADD;
    }
    else if ((insn & MASK_LW) == MATCH_LW)
    {
        result.instruction_name = "LW";

        result.immediate = imm_i(insn);
        result.uses_rs1 = 1;

        result.control.reg_write = 1;
        result.control.mem_read = 1;
        result.control.alu_src_imm = 1;
        result.control.alu_op = ALU_ADD;
    }
    else if ((insn & MASK_SW) == MATCH_SW)
    {
        result.instruction_name = "SW";

        result.immediate = imm_s(insn);
        result.uses_rs1 = 1;
        result.uses_rs2 = 1;

        result.control.mem_write = 1;
        result.control.alu_src_imm = 1;
        result.control.alu_op = ALU_ADD;
    }
    else if ((insn & MASK_BEQ) == MATCH_BEQ)
    {
        result.instruction_name = "BEQ";

        result.immediate = imm_b(insn);
        result.uses_rs1 = 1;
        result.uses_rs2 = 1;

        result.control.branch = 1;
        result.control.alu_op = ALU_SUB;
    }
    else if ((insn & MASK_JAL) == MATCH_JAL)
    {
        result.instruction_name = "JAL";

        result.immediate = imm_j(insn);

        result.control.reg_write = 1;
        result.control.jump = 1;
    }
    else if ((insn & MASK_JALR) == MATCH_JALR)
    {
        result.instruction_name = "JALR";

        result.immediate = imm_i(insn);
        result.uses_rs1 = 1;

        result.control.reg_write = 1;
        result.control.jump = 1;
        result.control.jalr = 1;
    }
    else if ((insn & MASK_LUI) == MATCH_LUI)
    {
        result.instruction_name = "LUI";

        result.immediate = imm_u(insn);

        result.control.reg_write = 1;
    }
    else if (insn == 0x00000013)
    {
        //ADDI x0, x0, 0 is the architectural NOP

        result.instruction_name = "NOP";
        result.valid = 1;
    }
    else
    {
        result.instruction_name = "UNKNOWN";
        result.valid = 0;
    }

    return result;
}