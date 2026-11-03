#ifndef THE_BUGGER_STACK_H
#define THE_BUGGER_STACK_H

#include "model/memory.h"

enum {
    TB_STACK_VIEWS  = 32,
    TB_STACK_POINTS = 32,
    TB_STACK_NAMES  = 64
};

typedef struct {
    uint64_t          address;
    uint64_t          lifetime;
    TbGdbGeneration_t generation;
    int               frame;
    char              label[96];
    bool              variable;
} TbStackPoint_t;

typedef struct {
    uint64_t low;
    uint64_t high;
    uint64_t lifetime;
    bool     used;
    bool     upward;
} TbStackDeclaration_t;

typedef struct {
    uint64_t          address;
    uint64_t          lifetime;
    uint64_t          sequence;
    uint64_t          sampled_ms;
    TbGdbGeneration_t generation;
    char              thread[32];
    unsigned          size;
    TbMemoryOrder_e     order;
    unsigned char     bytes[TB_MEMORY_BYTES];
    bool              valid[TB_MEMORY_BYTES];
} TbStackSample_t;

typedef struct {
    char name[96];
    bool eligible;
} TbStackName_t;

typedef struct {
    char            thread[32];
    char            message[192];
    char            anchor_error[192];
    char            annotation_error[192];
    uint64_t        lifetime;
    uint64_t        sp;
    uint64_t        start;
    uint64_t        revision;
    unsigned        size;
    unsigned        grouping;
    bool            visible;
    bool            follow;
    bool            anchor_valid;
    bool            positioned;
    bool            historical;
    bool            reading;
    bool            cancel_requested;
    bool            names_ready;
    bool            names_limited;
    bool            guard_confirmation;
    size_t          name_count;
    size_t          point_count;
    TbStackName_t   names[TB_STACK_NAMES];
    TbStackPoint_t  points[TB_STACK_POINTS];
    TbStackSample_t sample;
} TbStackView_t;

typedef struct tb_stack TbStack_t;

TbStack_t *tb_stack_create(TbGdb_t *gdb, TbContext_t *context, TbMemory_t *memory, TbBrowseCache_t *cache);
void tb_stack_destroy(TbStack_t *stack);
void tb_stack_reset(TbStack_t *stack);
void tb_stack_clear(TbStack_t *stack, int view);
void tb_stack_visible(TbStack_t *stack, int view, bool visible);
void tb_stack_target(TbStack_t *stack, bool octets, unsigned address_bits, TbMemoryOrder_e order,
                     const char *architecture);
void tb_stack_invalidate(TbStack_t *stack);
bool tb_stack_event(TbStack_t *stack, const TbGdbEvent_t *event);
void tb_stack_pump(TbStack_t *stack);
uint64_t tb_stack_revision(const TbStack_t *stack);
const TbStackView_t *tb_stack_view(const TbStack_t *stack, int view);
bool tb_stack_read(TbStack_t *stack, int view);
void tb_stack_cancel(TbStack_t *stack, int view);
/* All navigation and presentation calls below are read-free. Signed literals
 * mean offsets from a fresh frame-0 SP; unsigned literals mean window starts. */
bool tb_stack_navigate(TbStack_t *stack, int view, const char *literal);
bool tb_stack_page(TbStack_t *stack, int view, int direction);
void tb_stack_manual(TbStack_t *stack, int view);
bool tb_stack_follow(TbStack_t *stack, int view);
bool tb_stack_size(TbStack_t *stack, int view, unsigned bytes);
bool tb_stack_group(TbStack_t *stack, int view, unsigned bytes);
bool tb_stack_cached(TbStack_t *stack, int view, int direction);
bool tb_stack_stale(const TbStack_t *stack, int view);
size_t tb_stack_rows(const TbStack_t *stack, int view);
void tb_stack_line(const TbStack_t *stack, int view, size_t row, char *text, size_t capacity);
/* Details are complete, paginated by the TUI, and never query the target. */
void tb_stack_details(const TbStack_t *stack, int view, size_t row, char *text, size_t capacity);
bool tb_stack_frame_point(TbStack_t *stack, int view);
bool tb_stack_names(TbStack_t *stack, int view);
bool tb_stack_variable_point(TbStack_t *stack, int view, size_t name);
/* Explicit session-wide change: never silently applied or restored. */
bool tb_stack_disable_calls(TbStack_t *stack, int view);
bool tb_stack_remove_point(TbStack_t *stack, int view, size_t point);
const TbStackDeclaration_t *tb_stack_declaration(const TbStack_t *stack, int view);
bool tb_stack_declare(TbStack_t *stack, int view, uint64_t low, uint64_t high, bool upward);
void tb_stack_undeclare(TbStack_t *stack, int view);
void tb_stack_bounds(const TbStack_t *stack, int view, char *text, size_t capacity);

#endif // THE_BUGGER_STACK_H
