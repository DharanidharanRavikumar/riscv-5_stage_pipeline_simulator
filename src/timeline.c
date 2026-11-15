
//This file timeline.c is just to record and print stage history it is used for observability only, it has no logic to do with simulated cpu exceution


#include "timeline.h"

#include <stdio.h>
#include <string.h>

// Initialize timeline history 

void timeline_init(pipeline_timeline_t *timeline)
{
    memset(timeline, 0, sizeof(*timeline));
}

// Find or create an instruction row 

static int find_instruction(
    pipeline_timeline_t *timeline,
    uint32_t pc,
    const char *name
)
{
    for (int i = 0; i < TIMELINE_MAX_INSTRUCTIONS; i++)
    {
        if (timeline->instructions[i].valid &&
            timeline->instructions[i].pc == pc)
        {
            return i;
        }
    }

    for (int i = 0; i < TIMELINE_MAX_INSTRUCTIONS; i++)
    {
        if (!timeline->instructions[i].valid)
        {
            timeline_instruction_t *row = &timeline->instructions[i];

            row->valid = 1;
            row->pc = pc;

            snprintf(
                row->instruction_name,
                sizeof(row->instruction_name),
                "%s",
                name
            );

            return i;
        }
    }

    return -1;
}

// Record an instruction's pipeline stage 

void timeline_record(
    pipeline_timeline_t *timeline,
    uint32_t cycle,
    uint32_t pc,
    const char *name,
    const char *stage
)
{
    if (cycle == 0 || cycle > MAX_CYCLES)
    {
        return;
    }

    int index = find_instruction(timeline, pc, name);

    if (index < 0)
    {
        return;
    }

    snprintf(
        timeline->instructions[index].stages[cycle - 1],
        sizeof(timeline->instructions[index].stages[cycle - 1]),
        "%s",
        stage
    );
}

// Record stalls, bubbles and flushes 

void timeline_add_event(
    pipeline_timeline_t *timeline,
    uint32_t cycle,
    const char *event
)
{
    if (cycle == 0 || cycle > MAX_CYCLES)
    {
        return;
    }

    snprintf(
        timeline->events[cycle - 1],
        sizeof(timeline->events[cycle - 1]),
        "%s",
        event
    );
}

// Print five clock cycles at a time

void timeline_print(pipeline_timeline_t *timeline)
{
    printf("\n\n");
    printf("============================================================\n");
    printf("          RISC-V FIVE-STAGE PIPELINE TIMING DIAGRAM\n");
    printf("============================================================\n");

    for (uint32_t start = 0; start < timeline->total_cycles; start += 5)
    {
        uint32_t end = start + 5;

        if (end > timeline->total_cycles)
        {
            end = timeline->total_cycles;
        }

        printf("\n---------------- CLOCK CYCLES %u - %u ----------------\n",
               start + 1, end);

        printf("%-18s", "INSTRUCTION");

        for (uint32_t c = start; c < end; c++)
        {
            printf("| %-6s", "");
        }

        printf("\n");

        printf("%-18s", "");

        for (uint32_t c = start; c < end; c++)
        {
            printf("| C%-5u", c + 1);
        }

        printf("\n");

        for (int i = 0; i < TIMELINE_MAX_INSTRUCTIONS; i++)
        {
            timeline_instruction_t *row = &timeline->instructions[i];

            if (!row->valid)
            {
                continue;
            }

            printf("%-8s@0x%03X",
                   row->instruction_name,
                   row->pc);

            for (uint32_t c = start; c < end; c++)
            {
                const char *stage = row->stages[c];

                if (stage[0] == '\0')
                {
                    printf("| %-6s", "-");
                }
                else
                {
                    printf("| %-6s", stage);
                }
            }

            printf("\n");
        }

        printf("\n");

        int has_events = 0;

        for (uint32_t c = start; c < end; c++)
        {
            if (timeline->events[c][0] != '\0')
            {
                if (!has_events)
                {
                    printf("Events:\n");
                    has_events = 1;
                }

                printf("  C%-3u: %s\n",
                       c + 1,
                       timeline->events[c]);
            }
        }

        if (!has_events)
        {
            printf("Events: None\n");
        }
    }

    printf("\n============================================================\n");
    printf("IF = Instruction Fetch | ID = Instruction Decode\n");
    printf("EX = Execute | MEM = Memory Access | WB = Write Back\n");
    printf("============================================================\n");
}
