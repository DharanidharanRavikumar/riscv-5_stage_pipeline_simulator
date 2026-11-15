
// timeline.h includes pipeline.h timeline structs
// it's inputs are timeline pointer, cycle, PC, name, stage string and most importantly it has nothing to do with simulator execution 
// it returns  the Changes of timeline history only
#ifndef TIMELINE_H
#define TIMELINE_H

#include "pipeline.h"

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
