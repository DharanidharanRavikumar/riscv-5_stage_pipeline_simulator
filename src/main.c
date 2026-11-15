// main.c: encode_*, prepare_benchmarks, load_program: Create test instruction streams and place machine code into memory Test setup; not the processor’s decoder. Create test instruction streams and place machine code into memory
// main.c: Display final state and counters. [ dump_registers, print_statistics: it change simulated CPU execution, reporting only]
#include "pipeline.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include "timeline.h"

// ______________________Benchmark program definitions___________________________ 

typedef struct
{
    const char *name;
    const char *description;
    uint32_t instructions[32];
    size_t count;
} benchmark_t;

static uint32_t encode_addi(unsigned rd, unsigned rs1, int imm)
{
    return (((uint32_t)imm & 0xFFFU) << 20) |
           ((rs1 & 31U) << 15) | ((rd & 31U) << 7) | 0x13U;
}

static uint32_t encode_r(unsigned funct7, unsigned rs2, unsigned rs1,
                         unsigned funct3, unsigned rd)
{
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | (rd << 7) | 0x33U;
}

static uint32_t encode_lw(unsigned rd, unsigned rs1, int imm)
{
    return (((uint32_t)imm & 0xFFFU) << 20) | (rs1 << 15) |
           (2U << 12) | (rd << 7) | 0x03U;
}

static uint32_t encode_sw(unsigned rs2, unsigned rs1, int imm)
{
    uint32_t value = (uint32_t)imm & 0xFFFU;

    return ((value >> 5) << 25) | (rs2 << 20) | (rs1 << 15) |
           (2U << 12) | ((value & 0x1FU) << 7) | 0x23U;
}

static uint32_t encode_beq(unsigned rs1, unsigned rs2, int imm)
{
    uint32_t value = (uint32_t)imm & 0x1FFFU;

    return (((value >> 12) & 1U) << 31) |
           (((value >> 5) & 0x3FU) << 25) |
           (rs2 << 20) | (rs1 << 15) |
           (((value >> 1) & 0xFU) << 8) |
           (((value >> 11) & 1U) << 7) | 0x63U;
}

/*
 * J-type instruction encoder for JAL.
 *
 * Converts the destination register (rd) and jump offset (imm)
 * into a 32-bit RISC-V machine-code instruction.
 *
 * This helper is provided for convenience when constructing
 * benchmark programs. Instead of manually calculating and writing
 * the hexadecimal machine-code value, we can call encode_jal(rd, imm).
 *
 * Both approaches produce the same machine-code instruction.
 * This helper simply makes benchmark construction easier and
 * more readable. The processor still receives and decodes the
 * generated machine code normally.
 */
static uint32_t encode_jal(unsigned rd, int imm)
{
    uint32_t value = (uint32_t)imm & 0x1FFFFFU;

    return (((value >> 20) & 1U) << 31) |
           (((value >> 1) & 0x3FFU) << 21) |
           (((value >> 11) & 1U) << 20) |
           (((value >> 12) & 0xFFU) << 12) |
           ((rd & 31U) << 7) | 0x6FU;
}

static void prepare_benchmarks(benchmark_t tests[5])
{
    memset(tests, 0, sizeof(benchmark_t) * 5);

    tests[0].name = "Independent instructions";
    tests[0].description = "Baseline execution with minimal data dependencies";

    uint32_t independent[] = {
        encode_addi(1, 0, 1), encode_addi(2, 0, 2),
        encode_addi(3, 0, 3), encode_addi(4, 0, 4),
        encode_addi(5, 0, 5), encode_addi(6, 0, 6)
    };

    tests[0].count = sizeof(independent) / sizeof(independent[0]);
    memcpy(tests[0].instructions, independent, sizeof(independent));

    tests[1].name = "Data dependencies and forwarding";
    tests[1].description = "Back-to-back ALU dependencies exercise forwarding paths";

    uint32_t dependencies[] = {
        encode_addi(1, 0, 5), encode_addi(2, 1, 3),
        encode_r(0, 1, 2, 0, 3), encode_r(0x20, 1, 3, 0, 4),
        encode_r(0, 2, 4, 0, 5)
    };

    tests[1].count = sizeof(dependencies) / sizeof(dependencies[0]);
    memcpy(tests[1].instructions, dependencies, sizeof(dependencies));

    tests[2].name = "Load-use dependency";
    tests[2].description = "A load is immediately followed by its dependent instruction";

    uint32_t load_use[] = {
        encode_addi(1, 0, 42), encode_sw(1, 0, 208),
        encode_lw(2, 0, 208), encode_addi(3, 2, 1),
        encode_r(0, 2, 3, 0, 4)
    };

    tests[2].count = sizeof(load_use) / sizeof(load_use[0]);
    memcpy(tests[2].instructions, load_use, sizeof(load_use));

    tests[3].name = "Branch behaviour";
    tests[3].description = "Taken, not-taken and data-dependent BEQ cases";

    uint32_t branches[] = {
        encode_addi(1, 0, 5),
        encode_addi(2, 0, 5),
        encode_beq(1, 2, 8),       /* Taken; dependent on the preceding ADDI */
        encode_addi(3, 0, 99),     /* Wrong path */
        encode_addi(4, 0, 6),
        encode_beq(1, 4, 8),       /* Not taken */
        encode_addi(5, 0, 7),
        encode_beq(1, 1, 8),       /* Taken */
        encode_addi(6, 0, 99),     /* Wrong path */
        encode_addi(6, 0, 8)
    };

    tests[3].count = sizeof(branches) / sizeof(branches[0]);
    memcpy(tests[3].instructions, branches, sizeof(branches));

    // Original combined demonstration program is retained as a regression test.

    /*
     * Test program:
     *
     * addi x1, x0, 5
     * addi x2, x0, 3
     * add  x3, x1, x2       -> forwarding test
     * sub  x4, x1, x2
     * jal  x1, -4
     * sw   x3, 208(x0)
     * jal  x0, -12         -> JAL encoder demonstration
     * lw   x5, 208(x0)
     * addi x6, x5, 1       -> load-use stall test
     * beq  x6, x6, 8       -> taken branch / flush test
     * addi x7, x0, 99      -> should be flushed
     * addi x7, x0, 7       -> branch target
     * lui  x8, 0x12345
     * 0                    -> end marker
     */

    tests[4].name = "Original combined demonstration";
    tests[4].description = "Original forwarding, load-use, store and taken-branch test";

   uint32_t original[] = {
    0x00500093,              // addi x1, x0, 5
    0x00300113,              // addi x2, x0, 3
    0x002081B3,              // add x3, x1, x2
    0x40208233,              // sub x4, x1, x2

    encode_jal(1, 4),         // jal x1, 4: JAL encoder demonstration

    0x0C302823,              // sw x3, 208(x0)

    encode_jal(0, 4),         // jal x0, 4: generated using the JAL encoder

    0x0D002283,              // lw x5, 208(x0)
    0x00128313,              // addi x6, x5, 1
    0x00630463,              // beq x6, x6, 8
    0x06300393,              // addi x7, x0, 99 (wrong path)
    0x00700393,              // addi x7, x0, 7 (branch target)
    0x12345437               // lui x8, 0x12345
};

    tests[4].count = sizeof(original) / sizeof(original[0]);
    memcpy(tests[4].instructions, original, sizeof(original));
}

// Load a small machine code test program

static void load_program(cpu_t *cpu, const benchmark_t *test)
{
    //As Risc-V follows little-endian we Store every instruction in little-endian byte order. 

    for (size_t i = 0; i < test->count; i++)
    {
        uint32_t instruction = test->instructions[i];
        uint32_t address = (uint32_t)(i * 4);

        cpu->mem[address + 0] = (uint8_t)(instruction & 0xFF);
        cpu->mem[address + 1] = (uint8_t)((instruction >> 8) & 0xFF);
        cpu->mem[address + 2] = (uint8_t)((instruction >> 16) & 0xFF);
        cpu->mem[address + 3] = (uint8_t)((instruction >> 24) & 0xFF);
    }

    // A zero word is the simulator's end marker. 
}

// Print register values 

static void dump_registers(cpu_t *cpu)
{
    printf("\n---------------- REGISTER DUMP ----------------\n");

    for (int i = 0; i < NUM_REGS; i++)
    {
        printf("x%-2d = %-10d", i, cpu->regs[i]);

        if (i % 4 == 3)
            printf("\n");
    }

    printf("PC  = 0x%03X\n", cpu->pc);
}

//Print pipeline statistics

static void print_statistics(pipeline_t *pipeline)
{
    instruction_stats_t *ins = &pipeline->instruction_stats;
    hazard_stats_t *haz = &pipeline->hazard_stats;

    printf("\n---------------- PIPELINE PERFORMANCE ----------------\n");
    printf("Clock cycles             : %u\n", pipeline->cycles);
    printf("Retired instructions     : %u\n", pipeline->retired);
    printf("CPI                      : %.3f\n",
           pipeline->retired ? (double)pipeline->cycles / pipeline->retired : 0.0);
    printf("IPC                      : %.3f\n",
           pipeline->cycles ? (double)pipeline->retired / pipeline->cycles : 0.0);

    printf("\n---------------- INSTRUCTION CATEGORY COUNTS ----------------\n");
    printf("Arithmetic               : %u\n", ins->arithmetic);
    printf("Loads                    : %u\n", ins->loads);
    printf("Stores                   : %u\n", ins->stores);
    printf("Branches                 : %u\n", ins->branches);
    printf("Jumps                    : %u\n", ins->jumps);
    printf("Upper immediate (LUI)    : %u\n", ins->upper_immediate);
    printf("NOP                      : %u\n", ins->nops);
    printf("Unknown                  : %u\n", ins->unknown);

    printf("\n---------------- HAZARD AND FORWARDING STATISTICS ----------------\n");
    printf("Load-use stalls          : %u\n", haz->load_use_stalls);
    printf("Bubbles inserted         : %u\n", haz->bubbles);
    printf("EX/MEM forwarding        : %u\n", haz->ex_mem_forwardings);
    printf("MEM/WB forwarding        : %u\n", haz->mem_wb_forwardings);
    printf("Taken BEQ                : %u\n", haz->branch_taken);
    printf("Not-taken BEQ            : %u\n", haz->branch_not_taken);
    printf("Branch flush events      : %u\n", haz->branch_flush_events);
    printf("Flushed instruction slots: %u\n", haz->flushed_instructions);

    /*
      N + 4 is the ideal fill-and-drain reference for a simple five-stage,
      single-issue straight-line instruction stream with no stalls/flushes.
     */

    if (pipeline->retired > 0)
    {
        uint32_t ideal_cycles = pipeline->retired + 4;

        printf("\n__________________IDEAL VS ACTUAL (REFERENCE)___________________\n");
        printf("Ideal cycles (retired + 4): %u\n", ideal_cycles);
        printf("Actual cycles             : %u\n", pipeline->cycles);
        printf("Actual - ideal            : %d\n",
               (int)pipeline->cycles - (int)ideal_cycles);
        printf("Note: this is a reference baseline; control-flow flushes and the simulator's fetch/drain convention affect the comparison.\n");
    }
}

// Record one clock cycle for the timeline 
// record_pipeline_cycle is just to record and print stage history it is used for observability only, it has no logic to do with simulated cpu exceution

static void record_pipeline_cycle(
    pipeline_timeline_t *timeline, uint32_t cycle,
    if_id_t *old_if_id, id_ex_t *old_id_ex,
    ex_mem_t *old_ex_mem, mem_wb_t *old_mem_wb,
    pipeline_t *pipeline, int stalled, int flushed)
{
    if (old_mem_wb->valid)
        timeline_record(timeline, cycle, old_mem_wb->pc,
                        old_mem_wb->instruction_name, "WB");

    if (old_ex_mem->valid)
        timeline_record(timeline, cycle, old_ex_mem->pc,
                        old_ex_mem->instruction_name, "MEM");

    if (old_id_ex->valid)
        timeline_record(timeline, cycle, old_id_ex->pc,
                        old_id_ex->instruction_name, "EX");

    if (old_if_id->valid)
    {
        decoded_instruction_t decoded =
            decode_instruction(old_if_id->instruction);

        const char *name = decoded.valid ?
            decoded.instruction_name : "UNKNOWN";

        timeline_record(timeline, cycle, old_if_id->pc, name,
                        flushed ? "FLUSH" : (stalled ? "HOLD" : "ID"));
    }

    if (!stalled && !flushed && pipeline->if_id.valid)
    {
        decoded_instruction_t decoded =
            decode_instruction(pipeline->if_id.instruction);

        const char *name = decoded.valid ?
            decoded.instruction_name : "UNKNOWN";

        timeline_record(timeline, cycle, pipeline->if_id.pc, name, "IF");
    }

    if (stalled)
        timeline_add_event(timeline, cycle,
            "LOAD-USE STALL: IF/ID held; bubble inserted into ID/EX");

    if (flushed)
        timeline_add_event(timeline, cycle,
            "BRANCH FLUSH: wrong-path instruction slots discarded");
}

static pipeline_t run_benchmark(const benchmark_t *test)
{
    cpu_t cpu;
    pipeline_t pipeline;
    pipeline_timeline_t timeline;

    memset(&cpu, 0, sizeof(cpu));
    memset(&pipeline, 0, sizeof(pipeline));

    timeline_init(&timeline);
    load_program(&cpu, test);

    printf("\n====================================================\n");
    printf("BENCHMARK: %s\n%s\n", test->name, test->description);
    printf("====================================================\n");
    printf("RISC-V RV32I FIVE-STAGE PIPELINE SIMULATOR\n");
    printf("IF -> ID -> EX -> MEM -> WB\n");

    while (!cpu.halted)
    {
        if (pipeline.cycles >= MAX_CYCLES)
        {
            printf("\nMaximum cycle count reached. Stopping.\n");
            break;
        }

        if (cpu.fetch_stopped && pipeline_empty(&pipeline))
            break;

        if_id_t old_if_id = pipeline.if_id;
        id_ex_t old_id_ex = pipeline.id_ex;
        ex_mem_t old_ex_mem = pipeline.ex_mem;
        mem_wb_t old_mem_wb = pipeline.mem_wb;

        uint32_t old_stalls = pipeline.stalls;
        uint32_t old_flushes = pipeline.flushes;
        uint32_t current_cycle = pipeline.cycles + 1;

        pipeline_step(&cpu, &pipeline);

        int stalled = pipeline.stalls > old_stalls;
        int flushed = pipeline.flushes > old_flushes;

        record_pipeline_cycle(
            &timeline, current_cycle,
            &old_if_id, &old_id_ex,
            &old_ex_mem, &old_mem_wb,
            &pipeline, stalled, flushed
        );

        timeline.total_cycles = pipeline.cycles;
    }

    timeline_print(&timeline);
    dump_registers(&cpu);
    print_statistics(&pipeline);

    printf("\nSimulation finished.\n");

    return pipeline;
}

int main(int argc, char *argv[])
{
    benchmark_t tests[5];
    pipeline_t results[5];

    prepare_benchmarks(tests);

    printf("RISC-V PIPELINE BENCHMARKS\n");

    for (int i = 0; i < 5; i++)
        printf("%d. %s -- %s\n",
               i + 1, tests[i].name, tests[i].description);

    printf("Usage: ./pipeline.exe [1-5|all] (default: 5)\n");

    if (argc > 1 && strcmp(argv[1], "all") == 0)
    {
        for (int i = 0; i < 5; i++)
            results[i] = run_benchmark(&tests[i]);

        printf("\n================ BENCHMARK COMPARISON ================\n");

        printf("%-34s %7s %7s %7s %7s %7s %7s\n",
               "Benchmark", "Cycles", "Retired",
               "CPI", "IPC", "Stalls", "Flushes");

        for (int i = 0; i < 5; i++)
        {
            pipeline_t *r = &results[i];

            double cpi = r->retired ?
                (double)r->cycles / r->retired : 0.0;

            double ipc = r->cycles ?
                (double)r->retired / r->cycles : 0.0;

            printf("%-34s %7u %7u %7.3f %7.3f %7u %7u\n",
                   tests[i].name, r->cycles, r->retired,
                   cpi, ipc, r->stalls,
                   r->hazard_stats.flushed_instructions);
        }

        printf("=======================================================\n");
        return 0;
    }

    int selection = 5;

    if (argc > 1)
        selection = atoi(argv[1]);

    if (selection < 1 || selection > 5)
    {
        fprintf(stderr,
                "Invalid benchmark number. Choose 1, 2, 3, 4, 5 or all.\n");
        return 1;
    }

    (void)run_benchmark(&tests[selection - 1]);

    return 0;
}
