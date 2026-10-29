#ifndef THE_BUGGER_STACK_H
#define THE_BUGGER_STACK_H
#include "memory.h"

enum { TB_STACK_VIEWS = 32, TB_STACK_POINTS = 32, TB_STACK_NAMES = 64 };
typedef struct {
    uint64_t address, lifetime;
    TbGdbGeneration generation;
    int frame;
    char label[96];
    bool variable;
} TbStackPoint;
typedef struct { uint64_t low, high, lifetime; bool used, upward; } TbStackDeclaration;
typedef struct {
    uint64_t address, lifetime, sequence, sampled_ms;
    TbGdbGeneration generation;
    char thread[32];
    unsigned size;
    TbMemoryOrder order;
    unsigned char bytes[TB_MEMORY_BYTES];
    bool valid[TB_MEMORY_BYTES];
} TbStackSample;
typedef struct { char name[96]; bool eligible; } TbStackName;
typedef struct {
    char thread[32], message[192], anchor_error[192], annotation_error[192];
    uint64_t lifetime, sp, start, revision;
    unsigned size, grouping;
    bool visible, follow, anchor_valid, positioned, historical, reading, cancel_requested;
    bool names_ready, names_limited, guard_confirmation;
    size_t name_count, point_count;
    TbStackName names[TB_STACK_NAMES];
    TbStackPoint points[TB_STACK_POINTS];
    TbStackSample sample;
} TbStackView;
typedef struct TbStack TbStack;
TbStack *tb_stack_create(TbGdb *gdb, TbContext *context, TbMemory *memory, TbBrowseCache *cache);
void tb_stack_destroy(TbStack *stack);
void tb_stack_reset(TbStack *stack);
void tb_stack_clear(TbStack *stack, int view);
void tb_stack_visible(TbStack *stack, int view, bool visible);
void tb_stack_target(TbStack *stack, bool octets, unsigned address_bits, TbMemoryOrder order, const char *architecture);
void tb_stack_invalidate(TbStack *stack);
bool tb_stack_event(TbStack *stack, const TbGdbEvent *event);
void tb_stack_pump(TbStack *stack);
uint64_t tb_stack_revision(const TbStack *stack);
const TbStackView *tb_stack_view(const TbStack *stack, int view);
bool tb_stack_read(TbStack *stack, int view);
void tb_stack_cancel(TbStack *stack, int view);
/* All navigation and presentation calls below are read-free. Signed literals
 * mean offsets from a fresh frame-0 SP; unsigned literals mean window starts. */
bool tb_stack_navigate(TbStack *stack, int view, const char *literal);
bool tb_stack_page(TbStack *stack, int view, int direction);
void tb_stack_manual(TbStack *stack, int view);
bool tb_stack_follow(TbStack *stack, int view);
bool tb_stack_size(TbStack *stack, int view, unsigned bytes);
bool tb_stack_group(TbStack *stack, int view, unsigned bytes);
bool tb_stack_cached(TbStack *stack, int view, int direction);
bool tb_stack_stale(const TbStack *stack, int view);
size_t tb_stack_rows(const TbStack *stack, int view);
void tb_stack_line(const TbStack *stack, int view, size_t row, char *text, size_t capacity);
/* Details are complete, paginated by the TUI, and never query the target. */
void tb_stack_details(const TbStack *stack, int view, size_t row, char *text, size_t capacity);
bool tb_stack_frame_point(TbStack *stack, int view);
bool tb_stack_names(TbStack *stack, int view);
bool tb_stack_variable_point(TbStack *stack, int view, size_t name);
/* Explicit session-wide change: never silently applied or restored. */
bool tb_stack_disable_calls(TbStack *stack, int view);
bool tb_stack_remove_point(TbStack *stack, int view, size_t point);
const TbStackDeclaration *tb_stack_declaration(const TbStack *stack, int view);
bool tb_stack_declare(TbStack *stack, int view, uint64_t low, uint64_t high, bool upward);
void tb_stack_undeclare(TbStack *stack, int view);
void tb_stack_bounds(const TbStack *stack, int view, char *text, size_t capacity);
#endif
