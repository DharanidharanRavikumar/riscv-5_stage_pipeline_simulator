#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>
#include "cpu.h"
#include "instruction.h"
#include <stddef.h>

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

    int32_t read_data1; // the actual values of rs1, rs2 register holds is carried here
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

// MEM/WB pipeline register 

typedef struct
{
    int valid;

    uint32_t pc;
    uint32_t instruction;

    uint8_t rd;

    int32_t alu_result;
    int32_t memory_data;
    int32_t link_address;

    control_signals_t control;  //control_signals_t struct called control where this nested struct stores all the control flags for this instn 
    const char *instruction_name;
} mem_wb_t;

// Instruction and pipeline performance statistics

typedef struct
{
    uint32_t arithmetic;
    uint32_t loads;
    uint32_t stores;
    uint32_t branches;
    uint32_t jumps;
    uint32_t upper_immediate;
    uint32_t nops;
    uint32_t unknown;
} instruction_stats_t;

typedef struct
{
    uint32_t ex_mem_forwardings;
    uint32_t mem_wb_forwardings;
    uint32_t load_use_stalls;
    uint32_t branch_taken;
    uint32_t branch_not_taken;
    uint32_t branch_flush_events;
    uint32_t flushed_instructions;
    uint32_t bubbles;
} hazard_stats_t;

// Pipeline state

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
    instruction_stats_t instruction_stats;
    hazard_stats_t hazard_stats;
} pipeline_t;

// The below declarations helps to connect the pipeline, ALU, hazard, memory and writeback modules
// Execute stage
ex_mem_t execute(
    id_ex_t *stage,
    ex_mem_t *old_ex_mem,
    mem_wb_t *old_mem_wb,
    int *branch_taken,
    uint32_t *branch_target,
    pipeline_t *pipeline
);

// Forwarding unit
int32_t forwarding_value(
    uint8_t source_register,
    int32_t original_value,
    ex_mem_t *ex_mem,
    mem_wb_t *mem_wb,
    hazard_stats_t *stats
);

// Hazard detection
int detect_hazard(
    id_ex_t *old_id_ex,
    if_id_t *old_if_id
);

// Memory stage
mem_wb_t memory_access(cpu_t *cpu, ex_mem_t *stage);

// Write-back stage
int32_t writeback_value(mem_wb_t *stage);
void write_back(cpu_t *cpu, pipeline_t *pipeline);

// Pipeline functions
int pipeline_empty(pipeline_t *pipeline);
void pipeline_step(cpu_t *cpu, pipeline_t *pipeline);


/* ---------- Pipeline timeline history ---------- */

#define TIMELINE_MAX_INSTRUCTIONS 128

typedef struct
{
    uint32_t pc;
    char instruction_name[12];
    char stages[MAX_CYCLES][8];
    int valid;
} timeline_instruction_t;

typedef struct
{
    timeline_instruction_t instructions[TIMELINE_MAX_INSTRUCTIONS];
    uint32_t total_cycles;

    char events[MAX_CYCLES][64];
} pipeline_timeline_t;

/* Timeline functions */
void timeline_init(pipeline_timeline_t *timeline);

void timeline_record(
    pipeline_timeline_t *timeline,
    uint32_t cycle,
    uint32_t pc,
    const char *name,
    const char *stage
);

void timeline_add_event(
    pipeline_timeline_t *timeline,
    uint32_t cycle,
    const char *event
);

void timeline_print(pipeline_timeline_t *timeline);


#endif