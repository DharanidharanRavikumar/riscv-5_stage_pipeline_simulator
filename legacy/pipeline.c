/*
 * =====================================================================
 * RISC-V RV32I FIVE-STAGE PIPELINE SIMULATOR
 *
 * A small cycle-by-cycle pipeline simulator written in C.
 *
 * Coding style follows the existing RISC-V simulator:
 *   - cpu_t structure for processor state
 *   - uint32_t for instructions and addresses
 *   - int32_t for register values and immediates
 *   - explicit bit extraction
 *   - MATCH / MASK instruction identification
 *   - if / else if decoding
 *   - separate functions for fetch, decode, execute, memory and writeback
 *
 * Supported instructions:
 *   ADD, SUB, ADDI, LW, SW, BEQ, JAL, JALR, LUI
 *
 * Pipeline:
 *   IF -> ID -> EX -> MEM -> WB
 *
 * Included:
 *   - Four pipeline registers
 *   - EX/MEM and MEM/WB forwarding
 *   - One-cycle load-use hazard stall
 *   - Branch flushing (branch resolved in EX)
 *   - Cycle, stall and flush counters
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra -O0 pipeline.c -o pipeline
 *
 * Run:
 *   ./pipeline
 *
 * This is an educational simulator, not a complete implementation of
 * every instruction or every privileged feature in the RV32I ISA.
 * =====================================================================
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

//CPU configuration 

#define MEM_SIZE       4096
#define NUM_REGS       32
#define MAX_CYCLES     500

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

// CPU state - whole simulator's architectural state

typedef struct
{
    int32_t  regs[NUM_REGS];
    uint32_t pc;
    uint8_t  mem[MEM_SIZE];
    int      fetch_stopped; // to stop fectching new instructions 
    int      halted; // executed is terminated(used for err or termination condition)
} cpu_t;

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

// IF/ID pipeline register 

typedef struct
{
    int valid;
    uint32_t pc;
    uint32_t instruction;
} if_id_t;

// ID/EX pipeline register

typedef struct
{
    int valid;

    uint32_t pc;
    uint32_t instruction;

    uint8_t rd;
    uint8_t rs1;
    uint8_t rs2;

    int32_t immediate;

    int32_t read_data1;
    int32_t read_data2;

    int uses_rs1;
    int uses_rs2;

    control_signals_t control; // ID/EX pipeline register contains another struct called control where this nested struct stores all the control flags for this instn 

    const char *instruction_name;
} id_ex_t;

/* ---------- EX / MEM pipeline register ---------- */

typedef struct
{
    int valid;

    uint32_t pc;
    uint32_t instruction;

    uint8_t rd;

    int32_t alu_result;
    int32_t store_data;

    int32_t link_address; //helps the program to return to next instruction after execution for jal's x1 (eg:jal x1,16 )

    control_signals_t control;

    const char *instruction_name;
} ex_mem_t;

/* ---------- MEM / WB pipeline register ---------- */

typedef struct
{
    int valid;

    uint32_t pc;
    uint32_t instruction;

    uint8_t rd;

    int32_t alu_result;
    int32_t memory_data;
    int32_t link_address;

    control_signals_t control;  //struct called control where this nested struct stores all the control flags for this instn 
    const char *instruction_name;
} mem_wb_t;

/* ---------- Pipeline state ---------- */

typedef struct
{
    if_id_t  if_id;
    id_ex_t  id_ex;
    ex_mem_t ex_mem;
    mem_wb_t mem_wb;

    uint32_t cycles;
    uint32_t stalls;
    uint32_t flushes;
    uint32_t retired;
} pipeline_t;

/* ---------- Field extraction helpers ---------- */

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

/* Sign-extend a value containing the specified number of bits. */

static int32_t sign_extend(uint32_t value, uint32_t bits)
{
    uint32_t sign_bit = 1U << (bits - 1);

    if (value & sign_bit)
    {
        value |= (~0U << bits);
    }
    return (int32_t)value;
}

/* I-type immediate: instruction bits [31:20]. */

static int32_t imm_i(uint32_t insn)
{
    return sign_extend(insn >> 20, 12);
}

/* S-type immediate: instruction bits [31:25] and [11:7]. */

static int32_t imm_s(uint32_t insn)
{
    uint32_t upper = (insn >> 25) & 0x7F;
    uint32_t lower = (insn >> 7) & 0x1F;

    uint32_t immediate = (upper << 5) | lower;

    return sign_extend(immediate, 12);
}

/* B-type immediate: scattered branch-offset bits. */

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

/* J-type immediate: scattered jump-offset bits. */

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

/* U-type immediate: upper 20 bits. */

static int32_t imm_u(uint32_t insn)
{
    return (int32_t)(insn & 0xFFFFF000);
}

// Register read and write 

static int32_t reg_read(cpu_t *cpu, uint32_t reg)
{
    if (reg == 0)
    {
        return 0;
    }

    return cpu->regs[reg];
}

static void reg_write(cpu_t *cpu, uint32_t reg, int32_t value)
{
    if (reg != 0)
    {
        cpu->regs[reg] = value;
    }

    // x0 is always zero

    cpu->regs[0] = 0;
}

// Instruction decoding

static decoded_instruction_t decode_instruction(uint32_t insn)
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

// Write-back value selection 

static int32_t writeback_value(mem_wb_t *stage)
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

static void write_back(cpu_t *cpu, pipeline_t *pipeline)
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

    pipeline->retired++;
}

// Memory stage

static mem_wb_t memory_access(cpu_t *cpu, ex_mem_t *stage)
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

// Forwarding unit

static int32_t forwarding_value(
    uint8_t source_register,
    int32_t original_value,
    ex_mem_t *ex_mem,
    mem_wb_t *mem_wb)
{
    if (source_register == 0)
    {
        return 0;
    }

    /*
     * EX/MEM has priority because it contains the newest result.
     * A load result is not ready until the MEM stage, so it is
     * deliberately not forwarded from EX/MEM.
     */

    if (ex_mem->valid &&
        ex_mem->control.reg_write &&
        !ex_mem->control.mem_read &&
        ex_mem->rd != 0 &&
        ex_mem->rd == source_register)
    {
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
        return writeback_value(mem_wb);
    }

    return original_value;
}

/* ---------- Execute stage ---------- */

static ex_mem_t execute(
    id_ex_t *stage,
    ex_mem_t *old_ex_mem,
    mem_wb_t *old_mem_wb,
    int *branch_taken, // decides whether it's branch instrn or not (either 0 or 1)
    uint32_t *branch_target) // calculates the jump target (eg: PC + 16)
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
            old_mem_wb);
    }

    if (stage->uses_rs2)
    {
        operand2 = forwarding_value(
            stage->rs2,
            operand2,
            old_ex_mem,
            old_mem_wb);
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
            *branch_taken = 1;
            *branch_target =
                stage->pc + (uint32_t)stage->immediate;
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

// Decode stage 

static id_ex_t decode(
    cpu_t *cpu,
    if_id_t *stage)
{
    id_ex_t result;

    memset(&result, 0, sizeof(result));

    if (!stage->valid)
    {
        return result;
    }

    decoded_instruction_t decoded =
        decode_instruction(stage->instruction);

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

// Load-use hazard detection

static int detect_hazard(
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

//                                    Pipeline status helpers 

static int pipeline_empty(pipeline_t *pipeline)
{
    return !pipeline->if_id.valid &&
           !pipeline->id_ex.valid &&
           !pipeline->ex_mem.valid &&
           !pipeline->mem_wb.valid;
}

//                                     One pipeline clock cycle 

static void pipeline_step(cpu_t *cpu, pipeline_t *pipeline)
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

    /* ---------- WB stage ---------- */

    write_back(cpu, pipeline);

    /* ---------- MEM stage ---------- */

    next_mem_wb = memory_access(cpu, &pipeline->ex_mem);

    /* ---------- EX stage ---------- */

    int branch_taken = 0;
    uint32_t branch_target = 0;

    next_ex_mem = execute(
        &pipeline->id_ex,
        &pipeline->ex_mem,
        &pipeline->mem_wb,
        &branch_taken,
        &branch_target);

    /* ---------- Branch handling ---------- */

    if (branch_taken)
    {
        /*
         * The branch is resolved in EX.
         * The instruction currently in ID and the instruction
         * currently in IF are younger wrong-path instructions.
         * Both are flushed.
         */

        cpu->pc = branch_target;

        next_if_id.valid = 0;
        next_id_ex.valid = 0;

        pipeline->flushes += 2;

        printf("    FLUSH: wrong-path IF and ID instructions removed\n");
    }
    else
    {
        /* ---------- Hazard detection ---------- */

        int load_use_hazard =
            detect_hazard(&pipeline->id_ex, &pipeline->if_id);

        if (load_use_hazard)
        {
            /*
             * Hold PC and IF/ID.
             * Insert a bubble into ID/EX.
             * The older load continues into EX/MEM.
             */

            next_if_id = pipeline->if_id;
            next_id_ex.valid = 0;

            pipeline->stalls++;

            printf("    STALL: load-use hazard detected\n");
            printf("    BUBBLE: inserted into ID/EX\n");
        }
        else
        {
            /* ---------- ID stage ---------- */

            next_id_ex = decode(cpu, &pipeline->if_id);

            /* ---------- IF stage ---------- */

            if (!cpu->fetch_stopped && !cpu->halted)
            {
                uint32_t instruction = fetch(cpu);

                if (instruction == 0)
                {
                    /*
                     * A zero word is used as the end marker in
                     * this educational test program.
                     * Stop fetching and allow the pipeline to drain.
                     */

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

    /* ---------- Update all pipeline registers together ---------- */

    pipeline->if_id = next_if_id;
    pipeline->id_ex = next_id_ex;
    pipeline->ex_mem = next_ex_mem;
    pipeline->mem_wb = next_mem_wb;

    cpu->regs[0] = 0;
}

/* ---------- Load a small machine-code test program ---------- */

static void load_program(cpu_t *cpu)
{
    /*
     * Test program:
     *
     * addi x1, x0, 5
     * addi x2, x0, 3
     * add  x3, x1, x2       -> forwarding test
     * sub  x4, x1, x2
     * sw   x3, 208(x0)
     * lw   x5, 208(x0)
     * addi x6, x5, 1        -> load-use stall test
     * beq  x6, x6, 8         -> taken branch / flush test
     * addi x7, x0, 99        -> should be flushed
     * addi x7, x0, 7         -> branch target
     * 0                      -> end marker
     */

    uint32_t program[] =
    {
        0x00500093,   /* addi x1, x0, 5       */
        0x00300113,   /* addi x2, x0, 3       */
        0x002081B3,   /* add  x3, x1, x2      */
        0x40208233,   /* sub  x4, x1, x2      */
        0x0C302823,   /* sw   x3, 208(x0)     */
        0x0D002283,   /* lw   x5, 208(x0)     */
        0x00128313,   /* addi x6, x5, 1       */
        0x00630463,   /* beq  x6, x6, 8       */
        0x06300393,   /* addi x7, x0, 99      */
        0x00700393,   /* addi x7, x0, 7       */
        0x00000000    /* end marker           */
    };

    /* Store every instruction in little-endian byte order. */

    for (size_t i = 0; i < sizeof(program) / sizeof(program[0]); i++)
    {
        uint32_t instruction = program[i];
        uint32_t address = (uint32_t)(i * 4);

        cpu->mem[address + 0] = (uint8_t)(instruction & 0xFF);
        cpu->mem[address + 1] = (uint8_t)((instruction >> 8) & 0xFF);
        cpu->mem[address + 2] = (uint8_t)((instruction >> 16) & 0xFF);
        cpu->mem[address + 3] = (uint8_t)((instruction >> 24) & 0xFF);
    }
}

/* ---------- Print register values ---------- */

static void dump_registers(cpu_t *cpu)
{
    printf("\n---------------- REGISTER DUMP ----------------\n");

    for (int i = 0; i < NUM_REGS; i++)
    {
        printf("x%-2d = %-10d", i, cpu->regs[i]);

        if (i % 4 == 3)
        {
            printf("\n");
        }
    }

    printf("PC  = 0x%03X\n", cpu->pc);
}

/* ---------- Print pipeline statistics ---------- */

static void print_statistics(pipeline_t *pipeline)
{
    printf("\n---------------- PIPELINE STATISTICS ----------------\n");

    printf("Clock cycles       : %u\n", pipeline->cycles);
    printf("Retired instructions: %u\n", pipeline->retired);
    printf("Load-use stalls    : %u\n", pipeline->stalls);
    printf("Flushed instructions: %u\n", pipeline->flushes);

    if (pipeline->retired != 0)
    {
        double cpi =
            (double)pipeline->cycles / (double)pipeline->retired;

        double ipc =
            (double)pipeline->retired / (double)pipeline->cycles;

        printf("CPI                : %.3f\n", cpi);
        printf("IPC                : %.3f\n", ipc);
    }
}

/* ---------- Main function ---------- */

int main(void)
{
    cpu_t cpu;
    pipeline_t pipeline;

    memset(&cpu, 0, sizeof(cpu));
    memset(&pipeline, 0, sizeof(pipeline));

    load_program(&cpu);

    printf("RISC-V RV32I FIVE-STAGE PIPELINE SIMULATOR\n");
    printf("IF -> ID -> EX -> MEM -> WB\n");

    printf("\nStarting pipeline simulation...\n");

    while (!cpu.halted)
    {
        if (pipeline.cycles >= MAX_CYCLES)
        {
            printf("\nMaximum cycle count reached. Stopping.\n");
            break;
        }

        if (cpu.fetch_stopped && pipeline_empty(&pipeline))
        {
            break;
        }

        pipeline_step(&cpu, &pipeline);
    }

    dump_registers(&cpu);
    print_statistics(&pipeline);

    printf("\nSimulation finished.\n");

    return 0;
}
