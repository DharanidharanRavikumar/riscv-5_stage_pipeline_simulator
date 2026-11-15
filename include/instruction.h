#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <stdint.h>

//Instruction MATCH / MASK constants followed the same logic as project-1 Simulator for Field extraction

#define MASK_ADD       0xFE00707F
#define MATCH_ADD      0x00000033

#define MASK_SUB       0xFE00707F
#define MATCH_SUB      0x40000033

#define MASK_ADDI      0x0000707F
#define MATCH_ADDI     0x00000013

#define MASK_LW        0x0000707F
#define MATCH_LW       0x00002003

#define MASK_SW        0x0000707F
#define MATCH_SW       0x00002023

#define MASK_BEQ       0x0000707F
#define MATCH_BEQ      0x00000063

#define MASK_JAL       0x0000007F
#define MATCH_JAL      0x0000006F

#define MASK_JALR      0x0000707F
#define MATCH_JALR     0x00000067

#define MASK_LUI       0x0000007F
#define MATCH_LUI      0x00000037

//ALU operation identifiers like ALUOp from main control

#define ALU_NONE       0
#define ALU_ADD        1
#define ALU_SUB        2

//Control signals - collection of control info for one instruction

typedef struct
{
    int reg_write; // decides whether write a result into a register 
    int mem_read;  // to read data from memory for LW
    int mem_write; //writes data memory for SW
    int branch; // decides is it a conditional branch instn
    int jump; //decides whether it's a unconditional jump instn
    int jalr; // a flag to decide whether its jalr instn or not (jalr=1(yes, its jalr instn) / jalr=0(not a jalr instn)) // does this jump calculates its address using a register
    int alu_src_imm; // decides the ALU's second output from immd or register rs2
    int alu_op; // decides ALUOperation like add for address calculation and sub for branch comparisons
} control_signals_t;

//Decoded instruction information - this struct decoded_instruction_t represents the info obtained after decoding one instruction

typedef struct
{
    uint32_t instruction;

    uint8_t rd;
    uint8_t rs1; // rs1 and rs2 store register numbers
    uint8_t rs2;

    int32_t immediate;

    int uses_rs1; //to decide whether the instn needs rs1, here rs1 is not the actual data
    int uses_rs2; // these 2 flags uses_rs1, uses_rs2 are important for hazard detection

    control_signals_t control; //control signal's required for decoded instruction 

    const char *instruction_name;// points to existing string literal for printing instruction name
    int valid;
} decoded_instruction_t;

// Instruction decoding function
decoded_instruction_t decode_instruction(uint32_t insn);

#endif