#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Atomic received-FD close-on-exec. */
#include "symbol/wire.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    WIRE_VERSION = 1,
    WIRE_TRAILER = 0xa5,
    WIRE_MAX_RECEIVED_FDS = 16
};

static const unsigned char wire_magic[4] = {'T', 'B', 'S', 'E'};

typedef struct {
    unsigned char     header[TB_SYMBOL_WIRE_HEADER_BYTES];
    unsigned char    *payload;
    unsigned char     control[TB_SYMBOL_WIRE_CONTROL_BYTES];
    size_t            offset;
    int               descriptor;
    TbSymbolMessage_t message;
    bool              inline_payload;
    bool              active;
    bool              complete;
} Frame_t;

struct tb_symbol_wire {
    int                  socket_fd;
    size_t               max_bytes;
    size_t               allocated;
    TbSymbolBudget_t    *aggregate;
    TbSymbolWireStatus_e status;
    Frame_t              tx;
    Frame_t              rx;
};

static void close_fd(int *descriptor)
{
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}


static void frame_clear(TbSymbolWire_t *wire, Frame_t *frame)
{
    if (frame->payload && !frame->inline_payload) {
        wire->allocated -= frame->message.length;
        free(frame->payload);
        if (wire->aggregate)
            (void)tb_symbol_budget_release(wire->aggregate, frame->message.length);
    }
    close_fd(&frame->descriptor);
    *frame = (Frame_t){
        .descriptor = -1
    };
}


static TbSymbolWireStatus_e fail(TbSymbolWire_t *wire, TbSymbolWireStatus_e status)
{
    frame_clear(wire, &wire->tx);
    frame_clear(wire, &wire->rx);
    close_fd(&wire->socket_fd);
    wire->status = status;
    return status;
}


static bool backing_valid(int descriptor)
{
    struct stat info;
    int         flags = fcntl(descriptor, F_GETFL);
    return flags >= 0 && !((unsigned)flags & (unsigned)O_PATH) &&
           ((unsigned)flags & (unsigned)O_ACCMODE) == O_RDONLY &&
           fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode);
}


static bool kind_valid(TbSymbolMessageKind_e kind)
{
    return kind >= TB_SYMBOL_MESSAGE_HELLO && kind <= TB_SYMBOL_MESSAGE_GRANT;
}


static bool role_valid(int descriptor, TbSymbolDescriptorRole_e role)
{
    return descriptor < 0 ? role == TB_SYMBOL_DESCRIPTOR_NONE
                          : role == TB_SYMBOL_DESCRIPTOR_BACKING && backing_valid(descriptor);
}


static void put_uint(unsigned char *output, uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i)
        output[i] = (unsigned char)(value >> ((width - i - 1) * 8));
}


static uint64_t get_uint(const unsigned char *input, unsigned width)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value = (value << 8U) | input[i];
    return value;
}


static void encode_header(Frame_t *frame)
{
    unsigned char *header = frame->header;
    memcpy(header, wire_magic, sizeof(wire_magic));
    put_uint(header + 4, WIRE_VERSION, 2);
    put_uint(header + 6, frame->message.kind, 2);
    put_uint(header + 8, frame->message.length, 4);
    header[12] = frame->descriptor >= 0 ? 1 : 0;
    header[13] = (unsigned char)frame->message.descriptor_role;
    put_uint(header + 16, frame->message.request, 8);
    put_uint(header + 24, frame->message.consumer, 8);
    put_uint(header + 32, frame->message.revision, 8);
    put_uint(header + 40, frame->message.generation, 8);
}


static bool reserve_payload(TbSymbolWire_t *wire, Frame_t *frame)
{
    size_t length = frame->message.length;
    if (!length)
        return true;
    if (frame->message.kind != TB_SYMBOL_MESSAGE_REQUEST && length <= sizeof(frame->control)) {
        frame->payload = frame->control;
        frame->message.payload = frame->payload;
        frame->inline_payload = true;
        return true;
    }
    if (length > wire->max_bytes - wire->allocated)
        return false;

    if (wire->aggregate && !tb_symbol_budget_reserve(wire->aggregate, length))
        return false;
    frame->payload = malloc(length);
    if (!frame->payload) {
        if (wire->aggregate)
            (void)tb_symbol_budget_release(wire->aggregate, length);
        return false;
    }
    wire->allocated += length;
    frame->message.payload = frame->payload;
    return true;
}


static bool decode_header(Frame_t *frame)
{
    const unsigned char *header = frame->header;
    if (memcmp(header, wire_magic, sizeof(wire_magic)) != 0 || get_uint(header + 4, 2) != WIRE_VERSION ||
        header[12] > 1 || header[14] || header[15]) {
        return false;
    }

    frame->message = (TbSymbolMessage_t){
        .kind            = (TbSymbolMessageKind_e)get_uint(header + 6, 2),
        .request         = get_uint(header + 16, 8),
        .consumer        = get_uint(header + 24, 8),
        .revision        = get_uint(header + 32, 8),
        .generation      = get_uint(header + 40, 8),
        .length          = (size_t)get_uint(header + 8, 4),
        .descriptor      = -1,
        .descriptor_role = (TbSymbolDescriptorRole_e)header[13]
    };
    return kind_valid(frame->message.kind) && frame->message.length <= TB_SYMBOL_WIRE_MAX_PAYLOAD &&
           (header[12] ? header[13] == TB_SYMBOL_DESCRIPTOR_BACKING
                       : header[13] == TB_SYMBOL_DESCRIPTOR_NONE);
}


TbSymbolWire_t *tb_symbol_wire_create(int socket_fd, size_t max_bytes, TbSymbolBudget_t *aggregate)
{
    int       type;
    int       domain;
    int       flags    = fcntl(socket_fd, F_GETFL);
    int       fd_flags = fcntl(socket_fd, F_GETFD);
    socklen_t length   = sizeof(type);
    if (flags < 0 || !((unsigned)flags & (unsigned)O_NONBLOCK) || fd_flags < 0 ||
        !((unsigned)fd_flags & (unsigned)FD_CLOEXEC) ||
        getsockopt(socket_fd, SOL_SOCKET, SO_TYPE, &type, &length) != 0 || type != SOCK_STREAM) {
        return NULL;
    }
    length = sizeof(domain);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_DOMAIN, &domain, &length) != 0 || domain != AF_UNIX ||
        max_bytes < sizeof(TbSymbolWire_t)) {
        return NULL;
    }

    if (aggregate && !tb_symbol_budget_reserve(aggregate, sizeof(TbSymbolWire_t)))
        return NULL;
    TbSymbolWire_t *wire = calloc(1, sizeof(*wire));
    if (!wire) {
        if (aggregate)
            (void)tb_symbol_budget_release(aggregate, sizeof(TbSymbolWire_t));
        return NULL;
    }

    wire->aggregate     = aggregate;
    wire->socket_fd     = socket_fd;
    wire->max_bytes     = max_bytes;
    wire->allocated     = sizeof(*wire);
    wire->tx.descriptor = -1;
    wire->rx.descriptor = -1;
    return wire;
}


void tb_symbol_wire_destroy(TbSymbolWire_t *wire)
{
    if (!wire)
        return;

    frame_clear(wire, &wire->tx);
    frame_clear(wire, &wire->rx);
    close_fd(&wire->socket_fd);
    TbSymbolBudget_t *aggregate = wire->aggregate;
    free(wire);
    if (aggregate)
        (void)tb_symbol_budget_release(aggregate, sizeof(TbSymbolWire_t));
}


bool tb_symbol_wire_send(TbSymbolWire_t *wire, const TbSymbolMessage_t *message)
{
    if (!wire || wire->status != TB_SYMBOL_WIRE_OK || wire->tx.active || !message ||
        !kind_valid(message->kind) || message->length > TB_SYMBOL_WIRE_MAX_PAYLOAD ||
        (message->length && !message->payload) ||
        !role_valid(message->descriptor, message->descriptor_role)) {
        return false;
    }

    Frame_t frame = {
        .descriptor = -1,
        .message    = *message,
        .active     = true
    };
    if (!reserve_payload(wire, &frame))
        return false;
    if (message->length)
        memcpy(frame.payload, message->payload, message->length);
    if (message->descriptor >= 0) {
        frame.descriptor = fcntl(message->descriptor, F_DUPFD_CLOEXEC, 3);
        if (frame.descriptor < 0) {
            frame_clear(wire, &frame);
            return false;
        }
    }
    encode_header(&frame);
    wire->tx = frame;
    if (wire->tx.inline_payload) {
        wire->tx.payload = wire->tx.control;
        wire->tx.message.payload = wire->tx.payload;
    }
    return true;
}


bool tb_symbol_wire_sending(const TbSymbolWire_t *wire)
{
    return wire && wire->tx.active;
}


static TbSymbolWireStatus_e receive_bytes(TbSymbolWire_t *wire, unsigned char *bytes, size_t length,
                                          bool trailer, size_t *received)
{
    union {
        struct cmsghdr alignment;
        unsigned char  bytes[CMSG_SPACE(sizeof(int) * WIRE_MAX_RECEIVED_FDS)];
    } control = {0};
    struct iovec vector = {
        .iov_base = bytes,
        .iov_len  = length
    };
    struct msghdr message = {
        .msg_iov        = &vector,
        .msg_iovlen     = 1,
        .msg_control    = control.bytes,
        .msg_controllen = sizeof(control.bytes)
    };
    ssize_t count = recvmsg(wire->socket_fd, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    *received = 0;
    if (count < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? TB_SYMBOL_WIRE_OK
                                                                     : TB_SYMBOL_WIRE_IO;
    }
    bool bad = ((unsigned)message.msg_flags & ((unsigned)MSG_CTRUNC | (unsigned)MSG_TRUNC)) != 0;
    int  descriptor = -1;
    size_t descriptors = 0;
    for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
            header->cmsg_len < CMSG_LEN(0)) {
            bad = true;
            continue;
        }
        size_t offset = (size_t)((unsigned char *)header - control.bytes);
        if (header->cmsg_len > message.msg_controllen - offset) {
            bad = true;
            break;
        }
        size_t bytes_count = header->cmsg_len - CMSG_LEN(0);
        if (bytes_count % sizeof(int))
            bad = true;
        for (size_t i = 0; i < bytes_count / sizeof(int); ++i) {
            int current;
            memcpy(&current, CMSG_DATA(header) + i * sizeof(int), sizeof(current));
            ++descriptors;
            if (descriptor >= 0)
                (void)close(current);
            else
                descriptor = current;
        }
    }
    size_t expected = trailer && count ? wire->rx.header[12] : 0;
    int    fd_flags = descriptor >= 0 ? fcntl(descriptor, F_GETFD) : 0;
    if (bad || descriptors != expected ||
        (descriptor >= 0 && (!backing_valid(descriptor) || fd_flags < 0 ||
                             !((unsigned)fd_flags & (unsigned)FD_CLOEXEC)))) {
        close_fd(&descriptor);
        return TB_SYMBOL_WIRE_PROTOCOL;
    }
    if (!count)
        return !wire->rx.offset ? TB_SYMBOL_WIRE_EOF : TB_SYMBOL_WIRE_PROTOCOL;
    if (descriptor >= 0) {
        wire->rx.descriptor         = descriptor;
        wire->rx.message.descriptor = descriptor;
    }
    *received = (size_t)count;
    return TB_SYMBOL_WIRE_OK;
}


static ssize_t send_trailer(TbSymbolWire_t *wire)
{
    unsigned char byte = WIRE_TRAILER;
    struct iovec vector = {
        .iov_base = &byte,
        .iov_len  = 1
    };
    union {
        struct cmsghdr alignment;
        unsigned char  bytes[CMSG_SPACE(sizeof(int))];
    } control = {0};
    struct msghdr message = {
        .msg_iov    = &vector,
        .msg_iovlen = 1
    };
    if (wire->tx.descriptor >= 0) {
        message.msg_control    = control.bytes;
        message.msg_controllen = sizeof(control.bytes);
        struct cmsghdr *header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type  = SCM_RIGHTS;
        header->cmsg_len   = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(header), &wire->tx.descriptor, sizeof(int));
    }
    return sendmsg(wire->socket_fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
}


TbSymbolWireStatus_e tb_symbol_wire_pump(TbSymbolWire_t *wire)
{
    if (!wire)
        return TB_SYMBOL_WIRE_IO;
    if (wire->status != TB_SYMBOL_WIRE_OK)
        return wire->status;

    size_t   budget      = TB_SYMBOL_WIRE_IO_BYTES;
    size_t   send_budget = TB_SYMBOL_WIRE_IO_BYTES / 2;
    unsigned steps       = 0;
    for (; steps < TB_SYMBOL_WIRE_IO_STEPS / 2 && send_budget && wire->tx.active;) {
        ++steps;
        Frame_t *frame = &wire->tx;
        size_t   end   = TB_SYMBOL_WIRE_HEADER_BYTES + frame->message.length;
        ssize_t  count;
        if (frame->offset == end) {
            count = send_trailer(wire);
        } else {
            bool header = frame->offset < TB_SYMBOL_WIRE_HEADER_BYTES;
            size_t length = header ? TB_SYMBOL_WIRE_HEADER_BYTES - frame->offset : end - frame->offset;
            if (length > send_budget)
                length = send_budget;
            const unsigned char *bytes = header ? frame->header + frame->offset
                                               : frame->payload + frame->offset - TB_SYMBOL_WIRE_HEADER_BYTES;
            count = send(wire->socket_fd, bytes, length, MSG_DONTWAIT | MSG_NOSIGNAL);
        }
        if (count < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return fail(wire, TB_SYMBOL_WIRE_IO);
        }
        if (!count)
            return fail(wire, TB_SYMBOL_WIRE_IO);

        budget      -= (size_t)count;
        send_budget -= (size_t)count;
        if (frame->offset == end)
            frame_clear(wire, frame);
        else
            frame->offset += (size_t)count;
    }

    for (; steps < TB_SYMBOL_WIRE_IO_STEPS && budget && !wire->rx.complete;) {
        ++steps;
        Frame_t *frame = &wire->rx;
        size_t   end   = TB_SYMBOL_WIRE_HEADER_BYTES + frame->message.length;
        bool     header = frame->offset < TB_SYMBOL_WIRE_HEADER_BYTES;
        bool     trailer = !header && frame->offset == end;
        unsigned char marker = 0;
        unsigned char *bytes = header ? frame->header + frame->offset
                             : trailer ? &marker
                                       : frame->payload + frame->offset - TB_SYMBOL_WIRE_HEADER_BYTES;
        size_t length = header ? TB_SYMBOL_WIRE_HEADER_BYTES - frame->offset
                      : trailer ? 1 : end - frame->offset;
        if (length > budget)
            length = budget;
        size_t received;
        TbSymbolWireStatus_e status = receive_bytes(wire, bytes, length, trailer, &received);
        if (status != TB_SYMBOL_WIRE_OK)
            return fail(wire, status);
        if (!received)
            break;
        frame->offset += received;
        budget        -= received;
        if (header && frame->offset == TB_SYMBOL_WIRE_HEADER_BYTES) {
            if (!decode_header(frame))
                return fail(wire, TB_SYMBOL_WIRE_PROTOCOL);
            if (!reserve_payload(wire, frame))
                return fail(wire, TB_SYMBOL_WIRE_RESOURCE);
        }
        if (trailer) {
            if (marker != WIRE_TRAILER)
                return fail(wire, TB_SYMBOL_WIRE_PROTOCOL);
            frame->complete = true;
        }
    }
    return wire->rx.complete ? TB_SYMBOL_WIRE_MESSAGE : TB_SYMBOL_WIRE_OK;
}


const TbSymbolMessage_t *tb_symbol_wire_message(const TbSymbolWire_t *wire)
{
    return wire && wire->rx.complete ? &wire->rx.message : NULL;
}


int tb_symbol_wire_take_descriptor(TbSymbolWire_t *wire)
{
    if (!wire || !wire->rx.complete)
        return -1;

    int descriptor = wire->rx.descriptor;
    wire->rx.descriptor         = -1;
    wire->rx.message.descriptor = -1;
    return descriptor;
}


void tb_symbol_wire_consume(TbSymbolWire_t *wire)
{
    if (wire && wire->rx.complete)
        frame_clear(wire, &wire->rx);
}


size_t tb_symbol_wire_overhead(void)
{
    return sizeof(TbSymbolWire_t);
}


size_t tb_symbol_wire_allocated(const TbSymbolWire_t *wire)
{
    return wire ? wire->allocated : 0;
}


struct pollfd tb_symbol_wire_pollfd(const TbSymbolWire_t *wire)
{
    return (struct pollfd){
        .fd     = wire ? wire->socket_fd : -1,
        .events = (short)((wire && !wire->rx.complete ? (unsigned)POLLIN : 0U) |
                          (wire && wire->tx.active ? (unsigned)POLLOUT : 0U))
    };
}
