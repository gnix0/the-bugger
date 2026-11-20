#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Atomic UNIX socket and descriptor flags. */
#include "symbol/wire.h"
#include "../support/check.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void *__real_malloc(size_t length);
void *__real_calloc(size_t count, size_t length);
void *__wrap_malloc(size_t length);
void *__wrap_calloc(size_t count, size_t length);
ssize_t __real_recvmsg(int fd, struct msghdr *message, int flags);
ssize_t __real_sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t __real_send(int fd, const void *bytes, size_t length, int flags);
ssize_t __wrap_recvmsg(int fd, struct msghdr *message, int flags);
ssize_t __wrap_sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t __wrap_send(int fd, const void *bytes, size_t length, int flags);

static bool   fail_allocation;
static size_t fragment;
static size_t interrupted_receives;
static size_t receive_attempts;
static size_t interrupted_trailers;
static size_t successful_trailers;

void *__wrap_malloc(size_t length)
{
    return fail_allocation ? NULL : __real_malloc(length);
}


void *__wrap_calloc(size_t count, size_t length)
{
    return fail_allocation ? NULL : __real_calloc(count, length);
}


ssize_t __wrap_recvmsg(int fd, struct msghdr *message, int flags)
{
    ++receive_attempts;
    CHECK(((unsigned)flags & (unsigned)MSG_CMSG_CLOEXEC) != 0);
    CHECK(((unsigned)flags & (unsigned)MSG_DONTWAIT) != 0);
    if (interrupted_receives) {
        --interrupted_receives;
        errno = EINTR;
        return -1;
    }
    CHECK(message->msg_iovlen == 1);
    size_t length = message->msg_iov[0].iov_len;
    if (fragment && length > fragment)
        message->msg_iov[0].iov_len = fragment;
    ssize_t result = __real_recvmsg(fd, message, flags);
    message->msg_iov[0].iov_len = length;
    return result;
}


ssize_t __wrap_sendmsg(int fd, const struct msghdr *message, int flags)
{
    CHECK(message->msg_iovlen == 1 && message->msg_iov[0].iov_len == 1);
    if (interrupted_trailers) {
        --interrupted_trailers;
        errno = EINTR;
        return -1;
    }
    ssize_t result = __real_sendmsg(fd, message, flags);
    if (result == 1)
        ++successful_trailers;
    return result;
}


ssize_t __wrap_send(int fd, const void *bytes, size_t length, int flags)
{
    if (fragment && length > fragment)
        length = fragment;
    return __real_send(fd, bytes, length, flags);
}


static void socket_pair(int sockets[2])
{
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0);
}


static size_t fd_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    CHECK(directory);
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            ++count;
    }
    CHECK(closedir(directory) == 0);
    return count;
}


static int backing(void)
{
    char path[] = "/tmp/the-bugger-wire-XXXXXX";
    int writer = mkstemp(path);
    CHECK(writer >= 0 && write(writer, "retained", 8) == 8);
    int reader = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(reader >= 0 && unlink(path) == 0 && close(writer) == 0);
    return reader;
}


static void raw_send(int fd, const void *bytes, size_t length)
{
    CHECK(__real_send(fd, bytes, length, MSG_NOSIGNAL) == (ssize_t)length);
}


static void raw_descriptors(int fd, unsigned char byte, const int *descriptors, size_t count)
{
    union {
        struct cmsghdr alignment;
        unsigned char  bytes[CMSG_SPACE(sizeof(int) * 32)];
    } control = {0};
    CHECK(count && count <= 32);
    struct iovec vector = {
        .iov_base = &byte,
        .iov_len  = 1
    };
    struct msghdr message = {
        .msg_iov        = &vector,
        .msg_iovlen     = 1,
        .msg_control    = control.bytes,
        .msg_controllen = CMSG_SPACE(sizeof(int) * count)
    };
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type  = SCM_RIGHTS;
    header->cmsg_len   = CMSG_LEN(sizeof(int) * count);
    memcpy(CMSG_DATA(header), descriptors, sizeof(int) * count);
    CHECK(__real_sendmsg(fd, &message, MSG_NOSIGNAL) == 1);
}


static void header_bytes(unsigned char header[TB_SYMBOL_WIRE_HEADER_BYTES], size_t length, bool descriptor)
{
    memset(header, 0, TB_SYMBOL_WIRE_HEADER_BYTES);
    header[0] = 'T';
    header[1] = 'B';
    header[2] = 'S';
    header[3] = 'E';
    header[5] = 2;
    header[7] = TB_SYMBOL_MESSAGE_RESULT;
    for (unsigned i = 0; i < 4; ++i)
        header[8 + i] = (unsigned char)(length >> ((3 - i) * 8));
    header[12] = descriptor ? 1 : 0;
    header[13] = descriptor ? TB_SYMBOL_DESCRIPTOR_BACKING : TB_SYMBOL_DESCRIPTOR_NONE;
}


static void transfer(TbSymbolWire_t *sender, TbSymbolWire_t *receiver)
{
    for (size_t i = 0; i < 20000; ++i) {
        CHECK(tb_symbol_wire_pump(sender) == TB_SYMBOL_WIRE_OK);
        TbSymbolWireStatus_e status = tb_symbol_wire_pump(receiver);
        CHECK(status == TB_SYMBOL_WIRE_OK || status == TB_SYMBOL_WIRE_MESSAGE);
        if (status == TB_SYMBOL_WIRE_MESSAGE && !tb_symbol_wire_sending(sender))
            return;
    }
    CHECK(false);
}


static void round_trips(void)
{
    int sockets[2];
    socket_pair(sockets);
    TbSymbolWire_t *left  = tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, NULL);
    TbSymbolWire_t *right = tb_symbol_wire_create(sockets[1], TB_SYMBOL_WIRE_DEFAULT_MAX, NULL);
    CHECK(left && right);
    size_t empty = tb_symbol_wire_allocated(left);
    CHECK(empty && empty < 4096);
    int descriptor = backing();
    static unsigned char bytes[TB_SYMBOL_WIRE_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)i;

    const size_t lengths[] = {0, 1, 13, TB_SYMBOL_WIRE_MAX_PAYLOAD};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        TbSymbolMessage_t message = {
            .kind            = TB_SYMBOL_MESSAGE_RESULT,
            .request         = UINT64_MAX,
            .consumer        = 123,
            .revision        = 456,
            .generation      = UINT64_C(0x8877665544332211),
            .payload         = bytes,
            .length          = lengths[i],
            .descriptor      = descriptor,
            .descriptor_role = TB_SYMBOL_DESCRIPTOR_BACKING
        };
        fragment = i == 3 ? 0 : 1;
        interrupted_trailers = 1;
        successful_trailers  = 0;
        CHECK(tb_symbol_wire_send(left, &message));
        CHECK(!tb_symbol_wire_send(left, &message));
        size_t retained = lengths[i] > TB_SYMBOL_WIRE_CONTROL_BYTES ? lengths[i] : 0;
        CHECK(tb_symbol_wire_allocated(left) == empty + retained);
        transfer(left, right);
        CHECK(successful_trailers == 1);
        const TbSymbolMessage_t *received = tb_symbol_wire_message(right);
        CHECK(received && received->kind == message.kind && received->request == message.request);
        CHECK(received->consumer == 123 && received->revision == 456);
        CHECK(received->generation == message.generation && received->length == lengths[i]);
        CHECK(!lengths[i] || memcmp(received->payload, bytes, lengths[i]) == 0);
        CHECK(received->descriptor >= 0 && (fcntl(received->descriptor, F_GETFD) & FD_CLOEXEC));
        char content[8];
        CHECK(pread(received->descriptor, content, sizeof(content), 0) == 8);
        CHECK(memcmp(content, "retained", 8) == 0);
        CHECK(tb_symbol_wire_pump(right) == TB_SYMBOL_WIRE_MESSAGE);
        CHECK(tb_symbol_wire_allocated(right) == empty + retained);
        CHECK(tb_symbol_wire_allocated(left) == empty);
        int owned = tb_symbol_wire_take_descriptor(right);
        CHECK(owned >= 0 && tb_symbol_wire_take_descriptor(right) == -1);
        tb_symbol_wire_consume(right);
        CHECK(tb_symbol_wire_allocated(right) == empty && close(owned) == 0);
    }
    fragment = 0;
    CHECK(close(descriptor) == 0);
    tb_symbol_wire_destroy(left);
    CHECK(tb_symbol_wire_pump(right) == TB_SYMBOL_WIRE_EOF);
    CHECK(tb_symbol_wire_pollfd(right).fd == -1);
    tb_symbol_wire_destroy(right);
}


static void adjacent_frames(void)
{
    int sockets[2];
    socket_pair(sockets);
    TbSymbolWire_t *wire = tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, NULL);
    CHECK(wire);
    int descriptor = backing();
    unsigned char header[TB_SYMBOL_WIRE_HEADER_BYTES];
    header_bytes(header, 3, true);
    for (unsigned i = 0; i < 2; ++i) {
        header[23] = (unsigned char)(i + 1);
        raw_send(sockets[1], header, sizeof(header));
        raw_send(sockets[1], "abc", 3);
        raw_descriptors(sockets[1], 0xa5, &descriptor, 1);
    }
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_MESSAGE);
        CHECK(tb_symbol_wire_message(wire)->request == i + 1);
        CHECK(tb_symbol_wire_message(wire)->descriptor >= 0);
        tb_symbol_wire_consume(wire);
    }
    CHECK(close(descriptor) == 0 && close(sockets[1]) == 0);
    tb_symbol_wire_destroy(wire);
}


static void rejected_frames(void)
{
    for (unsigned scenario = 0; scenario < 18; ++scenario) {
        size_t original = fd_count();
        int sockets[2];
        socket_pair(sockets);
        TbSymbolWire_t *wire = tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, NULL);
        CHECK(wire);
        int descriptor = backing();
        unsigned char header[TB_SYMBOL_WIRE_HEADER_BYTES];
        header_bytes(header, scenario == 8 ? TB_SYMBOL_WIRE_MAX_PAYLOAD + 1 : scenario == 16 ? 1 : 0,
                     scenario != 17);
        if (scenario == 0)
            header[0] = 'X';
        if (scenario == 1)
            header[5] = 1;
        if (scenario == 2)
            header[7] = 0xff;
        if (scenario == 3)
            header[14] = 1;
        if (scenario == 4)
            header[12] = 2;
        if (scenario == 5)
            header[13] = 0;
        if (scenario == 6) {
            raw_descriptors(sockets[1], header[0], &descriptor, 1);
            raw_send(sockets[1], header + 1, sizeof(header) - 1);
        } else {
            raw_send(sockets[1], header, sizeof(header));
        }
        if (scenario == 7) {
            raw_send(sockets[1], "\245", 1);
        } else if (scenario == 9 || scenario == 10) {
            int extras[32];
            size_t count = scenario == 9 ? 2 : 32;
            for (size_t i = 0; i < count; ++i)
                extras[i] = descriptor;
            raw_descriptors(sockets[1], 0xa5, extras, count);
        } else if (scenario == 13 || scenario == 14) {
            char path[64];
            CHECK(snprintf(path, sizeof(path), "/proc/self/fd/%d", descriptor) > 0);
            int invalid = open(path, (scenario == 13 ? O_PATH : O_RDWR) | O_CLOEXEC);
            CHECK(invalid >= 0);
            raw_descriptors(sockets[1], 0xa5, &invalid, 1);
            CHECK(close(invalid) == 0);
        } else if (scenario == 15) {
            int invalid[2];
            CHECK(pipe2(invalid, O_CLOEXEC) == 0);
            raw_descriptors(sockets[1], 0xa5, invalid, 1);
            CHECK(close(invalid[0]) == 0 && close(invalid[1]) == 0);
        } else if (scenario == 16 || scenario == 17) {
            raw_descriptors(sockets[1], 0xa5, &descriptor, 1);
        } else if (scenario == 11) {
            int invalid = open("/dev/null", O_RDONLY | O_CLOEXEC);
            CHECK(invalid >= 0);
            raw_descriptors(sockets[1], 0xa5, &invalid, 1);
            CHECK(close(invalid) == 0);
        } else {
            raw_descriptors(sockets[1], 0, &descriptor, 1);
        }
        CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_PROTOCOL);
        CHECK(!tb_symbol_wire_message(wire));
        CHECK(tb_symbol_wire_allocated(wire) < 4096);
        CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_PROTOCOL);
        CHECK(close(descriptor) == 0 && close(sockets[1]) == 0);
        tb_symbol_wire_destroy(wire);
        CHECK(fd_count() == original);
    }
}


static void failures_and_budgets(void)
{
    size_t original = fd_count();
    int sockets[2];
    socket_pair(sockets);
    CHECK(!tb_symbol_wire_create(sockets[0], 1, NULL));
    CHECK(fcntl(sockets[0], F_GETFD) >= 0);
    fail_allocation = true;
    CHECK(!tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, NULL));
    fail_allocation = false;
    TbSymbolWire_t *wire = tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, NULL);
    CHECK(wire);
    TbSymbolMessage_t message = {
        .kind       = TB_SYMBOL_MESSAGE_REQUEST,
        .payload    = (const unsigned char *)"payload",
        .length     = 7,
        .descriptor = -1
    };
    size_t empty = tb_symbol_wire_allocated(wire);
    fail_allocation = true;
    CHECK(!tb_symbol_wire_send(wire, &message) && tb_symbol_wire_allocated(wire) == empty);
    fail_allocation = false;
    CHECK(tb_symbol_wire_send(wire, &message));
    CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_OK);
    receive_attempts     = 0;
    interrupted_receives = 1000;
    CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_OK);
    CHECK(receive_attempts > 0 && receive_attempts <= TB_SYMBOL_WIRE_IO_STEPS);
    interrupted_receives = 0;
    unsigned char drain[TB_SYMBOL_WIRE_HEADER_BYTES + 8];
    CHECK(recv(sockets[1], drain, sizeof(drain), 0) == (ssize_t)sizeof(drain));
    CHECK(close(sockets[1]) == 0);
    CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_EOF);
    tb_symbol_wire_destroy(wire);
    CHECK(fd_count() == original);

    socket_pair(sockets);
    wire = tb_symbol_wire_create(sockets[0], empty + 3, NULL);
    CHECK(wire);
    message.length = 4;
    CHECK(!tb_symbol_wire_send(wire, &message));
    unsigned char header[TB_SYMBOL_WIRE_HEADER_BYTES];
    header_bytes(header, 4, false);
    header[7] = TB_SYMBOL_MESSAGE_REQUEST;
    raw_send(sockets[1], header, sizeof(header));
    CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_RESOURCE);
    CHECK(tb_symbol_wire_allocated(wire) == empty);
    CHECK(close(sockets[1]) == 0);
    tb_symbol_wire_destroy(wire);
    CHECK(fd_count() == original);

    for (size_t prefix = 1; prefix < TB_SYMBOL_WIRE_HEADER_BYTES + 4; ++prefix) {
        socket_pair(sockets);
        wire = tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, NULL);
        CHECK(wire);
        header_bytes(header, 3, false);
        size_t count = prefix < sizeof(header) ? prefix : sizeof(header);
        raw_send(sockets[1], header, count);
        if (prefix > sizeof(header))
            raw_send(sockets[1], "abc", prefix - sizeof(header));
        CHECK(close(sockets[1]) == 0);
        CHECK(tb_symbol_wire_pump(wire) == TB_SYMBOL_WIRE_PROTOCOL);
        tb_symbol_wire_destroy(wire);
        CHECK(fd_count() == original);
    }
}


static void shared_budget(void)
{
    TbSymbolBudget_t aggregate;
    CHECK(!tb_symbol_budget_init(NULL, 1));
    CHECK(!tb_symbol_budget_init(&aggregate, 0));
    CHECK(tb_symbol_budget_init(&aggregate, SIZE_MAX));
    CHECK(tb_symbol_budget_reserve(&aggregate, SIZE_MAX));
    CHECK(!tb_symbol_budget_reserve(&aggregate, 1) && aggregate.used == SIZE_MAX);
    CHECK(tb_symbol_budget_release(&aggregate, SIZE_MAX));
    CHECK(!tb_symbol_budget_release(&aggregate, 1) && !aggregate.used);
    CHECK(tb_symbol_budget_init(&aggregate, TB_SYMBOL_WIRE_DEFAULT_MAX));

    int sockets[2];
    socket_pair(sockets);
    fail_allocation = true;
    CHECK(!tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, &aggregate));
    CHECK(!aggregate.used);
    fail_allocation = false;
    TbSymbolWire_t *left = tb_symbol_wire_create(sockets[0], TB_SYMBOL_WIRE_DEFAULT_MAX, &aggregate);
    TbSymbolWire_t *right = tb_symbol_wire_create(sockets[1], TB_SYMBOL_WIRE_DEFAULT_MAX, &aggregate);
    CHECK(left && right);
    size_t fixed = aggregate.used;
    CHECK(fixed == tb_symbol_wire_allocated(left) + tb_symbol_wire_allocated(right));
    TbSymbolMessage_t message = {
        .kind       = TB_SYMBOL_MESSAGE_REQUEST,
        .payload    = (const unsigned char *)"abc",
        .length     = 3,
        .descriptor = -1
    };
    aggregate.limit = fixed + 3;
    fail_allocation = true;
    CHECK(!tb_symbol_wire_send(left, &message) && aggregate.used == fixed);
    fail_allocation = false;
    CHECK(tb_symbol_wire_send(left, &message) && aggregate.used == fixed + 3);
    CHECK(!tb_symbol_wire_send(right, &message) && aggregate.used == fixed + 3);
    CHECK(tb_symbol_wire_pump(left) == TB_SYMBOL_WIRE_OK);
    CHECK(aggregate.used == fixed);
    CHECK(tb_symbol_wire_pump(right) == TB_SYMBOL_WIRE_MESSAGE);
    CHECK(aggregate.used == fixed + 3);
    tb_symbol_wire_consume(right);
    CHECK(aggregate.used == fixed);
    aggregate.limit = fixed;
    message.kind = TB_SYMBOL_MESSAGE_CANCEL;
    fail_allocation = true;
    CHECK(tb_symbol_wire_send(left, &message));
    transfer(left, right);
    CHECK(tb_symbol_wire_message(right)->length == message.length);
    CHECK(memcmp(tb_symbol_wire_message(right)->payload, "abc", message.length) == 0);
    CHECK(aggregate.used == fixed);
    fail_allocation = false;
    tb_symbol_wire_consume(right);
    tb_symbol_wire_destroy(left);
    tb_symbol_wire_destroy(right);
    CHECK(!aggregate.used);
}


static void nested_accounts(void)
{
    TbSymbolBudget_t root;
    TbSymbolBudget_t child;
    TbSymbolBudget_t sibling;
    TbSymbolBudget_t grandchild;
    TbSymbolBudget_t fourth;
    TbSymbolBudget_t fifth;
    CHECK(tb_symbol_budget_init(&root, 10));
    CHECK(tb_symbol_budget_init_child(&child, 8, &root));
    CHECK(tb_symbol_budget_init_child(&sibling, 8, &root));
    CHECK(tb_symbol_budget_init_child(&grandchild, 6, &child));
    CHECK(tb_symbol_budget_init_child(&fourth, 4, &grandchild));
    CHECK(!tb_symbol_budget_init_child(&fifth, 2, &fourth));
    CHECK(!tb_symbol_budget_init_child(&root, 10, &child));
    CHECK(!tb_symbol_budget_init_child(&fifth, 2, NULL));
    CHECK(tb_symbol_budget_reserve(&grandchild, 6));
    CHECK(root.used == 6 && child.used == 6 && grandchild.used == 6);
    CHECK(tb_symbol_budget_reserve(&sibling, 4));
    CHECK(root.used == 10 && sibling.used == 4);
    CHECK(!tb_symbol_budget_reserve(&child, 1));
    CHECK(root.used == 10 && child.used == 6);
    CHECK(!tb_symbol_budget_release(&grandchild, 7));
    CHECK(root.used == 10 && child.used == 6 && grandchild.used == 6);
    CHECK(tb_symbol_budget_release(&sibling, 1));
    CHECK(tb_symbol_budget_reserve(&child, 1));
    CHECK(root.used == 10 && child.used == 7);
    CHECK(tb_symbol_budget_release(&grandchild, 6));
    CHECK(tb_symbol_budget_release(&child, 1));
    CHECK(tb_symbol_budget_release(&sibling, 3));
    CHECK(!root.used && !child.used && !sibling.used && !grandchild.used);
}


int main(void)
{
    size_t descriptors = fd_count();
    nested_accounts();
    shared_budget();
    round_trips();
    adjacent_frames();
    rejected_frames();
    failures_and_budgets();
    CHECK(fd_count() == descriptors);
    puts("PASS symbol wire: fragmented frames, descriptor association/CLOEXEC, "
         "budgets, failures and bounded EINTR");
    return 0;
}
