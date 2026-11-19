#ifndef THE_BUGGER_SYMBOL_WORKER_H
#define THE_BUGGER_SYMBOL_WORKER_H

#include <stddef.h>

size_t tb_symbol_worker_minimum_ipc(void);

/* Private early executable entry. Adopts the inherited UNIX stream socket;
 * performs no frontend, debugger, locale or terminal initialization. */
int tb_symbol_worker_main(int socket_fd);

#endif // THE_BUGGER_SYMBOL_WORKER_H
