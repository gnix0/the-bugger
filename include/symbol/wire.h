#ifndef THE_BUGGER_SYMBOL_WIRE_H
#define THE_BUGGER_SYMBOL_WIRE_H

#include "symbol/budget.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <poll.h>

enum {
    TB_SYMBOL_WIRE_HEADER_BYTES = 48,
    TB_SYMBOL_WIRE_MAX_PAYLOAD  = 1024 * 1024,
    TB_SYMBOL_WIRE_DEFAULT_MAX  = 4 * 1024 * 1024,
    TB_SYMBOL_WIRE_IO_BYTES     = 64 * 1024,
    TB_SYMBOL_WIRE_IO_STEPS     = 256,
    TB_SYMBOL_WIRE_CONTROL_BYTES = 32
};

typedef enum {
    TB_SYMBOL_MESSAGE_HELLO = 1,
    TB_SYMBOL_MESSAGE_READY,
    TB_SYMBOL_MESSAGE_REQUEST,
    TB_SYMBOL_MESSAGE_PROGRESS,
    TB_SYMBOL_MESSAGE_RESULT,
    TB_SYMBOL_MESSAGE_CANCEL,
    TB_SYMBOL_MESSAGE_SHUTDOWN,
    TB_SYMBOL_MESSAGE_SETTLED,
    TB_SYMBOL_MESSAGE_DIAGNOSTIC,
    TB_SYMBOL_MESSAGE_OFFER,
    TB_SYMBOL_MESSAGE_GRANT
} TbSymbolMessageKind_e;

typedef enum {
    TB_SYMBOL_DESCRIPTOR_NONE,
    TB_SYMBOL_DESCRIPTOR_BACKING
} TbSymbolDescriptorRole_e;

typedef struct {
    TbSymbolMessageKind_e    kind;
    uint64_t                 request;
    uint64_t                 consumer;
    uint64_t                 revision;
    uint64_t                 generation;
    const unsigned char     *payload;
    size_t                   length;
    int                      descriptor;
    TbSymbolDescriptorRole_e descriptor_role;
} TbSymbolMessage_t;

typedef enum {
    TB_SYMBOL_WIRE_OK,
    TB_SYMBOL_WIRE_MESSAGE,
    TB_SYMBOL_WIRE_EOF,
    TB_SYMBOL_WIRE_PROTOCOL,
    TB_SYMBOL_WIRE_RESOURCE,
    TB_SYMBOL_WIRE_IO
} TbSymbolWireStatus_e;

typedef struct tb_symbol_wire TbSymbolWire_t;

/* Requires a nonblocking, close-on-exec UNIX stream socket. Success adopts it;
 * failure leaves it owned by the caller. max_bytes includes the wire object and
 * all retained TX/RX payload capacity, not kernel buffers or allocator internals.
 * Optional aggregate is shared with the owning engine and must outlive the wire. */
TbSymbolWire_t *tb_symbol_wire_create(int socket_fd, size_t max_bytes, TbSymbolBudget_t *aggregate);
void tb_symbol_wire_destroy(TbSymbolWire_t *wire);

/* One queued transmission. Copies payload and duplicates any backing FD before
 * accepting; rejection changes nothing. Backing descriptors must be read-only
 * regular files, never O_PATH. Writers/mappings remain the admission layer's proof. */
bool tb_symbol_wire_send(TbSymbolWire_t *wire, const TbSymbolMessage_t *message);
bool tb_symbol_wire_sending(const TbSymbolWire_t *wire);

/* Nonblocking: at most 64 KiB and 256 syscall attempts per call, including EINTR.
 * A completed message remains borrowed until consume/destroy. No next-frame read
 * occurs before consume. Terminal failure closes the channel and incomplete FDs.
 * No callbacks or reentrancy; neither data nor metadata is interpreted here. */
TbSymbolWireStatus_e tb_symbol_wire_pump(TbSymbolWire_t *wire);
const TbSymbolMessage_t *tb_symbol_wire_message(const TbSymbolWire_t *wire);
/* Transfers the completed message's received descriptor, or returns -1. */
int tb_symbol_wire_take_descriptor(TbSymbolWire_t *wire);
void tb_symbol_wire_consume(TbSymbolWire_t *wire);
size_t tb_symbol_wire_overhead(void);
size_t tb_symbol_wire_allocated(const TbSymbolWire_t *wire);
struct pollfd tb_symbol_wire_pollfd(const TbSymbolWire_t *wire);

#endif // THE_BUGGER_SYMBOL_WIRE_H
