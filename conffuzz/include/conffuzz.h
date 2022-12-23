/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2022, Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdarg.h> /* va_arg */
#include <sys/select.h> /* select */
#include <cstring>

/* upper limit of function argc supported
 * "ought to be enough for anybody" haha
 * If you increase this, you also need to update the instrumentation
 * (LibFuncBefore() and Image())
 */
#define LIBAPI_ARG_UPPER_LIMIT 17

/* copy of the worker's mappings */
#define WORKER_MAPPINGS_COPY_PATH "/tmp/conffuzz_child_mappings.txt"
#define WORKER_MAPPINGS_COPY_PATH_OLD "/tmp/conffuzz_child_mappings.txt.old"

/* cache results of function argument lookups */
#define WORKER_CALLBACK_FILE "/tmp/worker_callback_file"

#define CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE 66

/* monitor/worker communication protocol specification */

enum ConfFuzzOpcode {
    /* represent an invalid operation, not to be used for communication */
    INVALID_OPCODE = 0,

    /* ALL opcodes */

    /* description: do nothing in particular.
     * arguments: no arguments.
     * triggers an answer: no */
    NOP_OPCODE,

    /* WORKER opcodes */

    /* description: signal that a worker is up and running.
     * arguments: no arguments.
     * triggers an answer: monitor acks with MONITOR_UP_ACK */
    WORKER_UP,

    /* description: signal a library call.
     * arguments:
     *   arg 1: function call site (64 bit)
     *   arg 2: function name size (64 bit)
     *   arg 3: function name (size given previously)
     *   arg 4: argument list size (64 bit)
     *   arg 5: argument list (size given by previously)
     * NOTE: argument list is in the form of ... size_argumentN argumentN ...
     * triggers an answer: monitor acks with MONITOR_EXEC_ACK. */
    WORKER_LIBRARY_CALL,

    /* description: signal a callback call.
     * arguments:
     *   arg 1: function call site (64 bit)
     *   arg 2: function name size (64 bit)
     *   arg 3: function name (size given previously)
     *   arg 4: argument list size (64 bit)
     *   arg 5: argument list (size given by previously)
     * triggers an answer: monitor acks with MONITOR_EXEC_ACK. */
    WORKER_CALLBACK_CALL,

    /* description: signal a library call return with a return value.
     * arguments:
     *   arg 1: return value (64 bit)
     * triggers an answer: monitor changes the return value
     * with MONITOR_RETURN_ORDER or confirms it with NOP_OPCODE. */
    WORKER_LIBRARY_RETURN,

    /* description: signal a library call return without return value.
     * arguments: no arguments.
     * triggers an answer: the monitor replies NOP_OPCODE */
    WORKER_LIBRARY_RETURN_NO_RETVAL,

    /* description: signal a callback call return with a return value.
     * arguments:
     *   arg 1: return value (64 bit)
     * triggers an answer: monitor confirms or changes the return value
     * with MONITOR_RETURN_ORDER. */
    WORKER_CALLBACK_RETURN,

    /* description: signal a callback call return without a return value.
     * arguments: no arguments.
     * triggers an answer: the monitor replies NOP_OPCODE */
    WORKER_CALLBACK_RETURN_NO_RETVAL,

    /* MONITOR opcodes */

    /* description: ack a worker's WORKER_UP.
     * arguments: no arguments. */
    MONITOR_UP_ACK,

    /* description: tell worker to instrument to a given address. This MUST
     *    come before MONITOR_EXEC_ACK.
     * arguments:
     *   arg 1: function address (64 bit) */
    MONITOR_INSTRUMENT_ORDER,

    /* description: ack a worker's WORKER_LIBRARY_CALL.
     * arguments: no arguments. */
    MONITOR_EXEC_ACK,

    /* description: tell worker to write to a given address. For now
     *   the monitor cannot specify a particular value. Write are guaranteed
     *   to be safe, i.e., even if the monitor makes a mistakes and orders
     *   a write to an invalid area of memory the worker will not crash.
     *   All write orders MUST come before the return order.
     * arguments:
     *   arg 1: address (64 bit)
     *   arg 2: size    (64 bit)
     *   arg 3: value   (64 bit but only $size bytes will be considered) */
    MONITOR_WRITE_ORDER,

    /* description: tell worker to modify a given argument.
     *   All write argument orders MUST come before the return order.
     * arguments:
     *   arg 1: argument number (64 bit)
     *   arg 2: value to write  (64 bit) */
    MONITOR_WRITEARG_ORDER,

    /* description: tell worker to use a given return value for the last
     *   WORKER_LIBRARY_RETURN emitted. No other order may come after a
     *   return order.
     * arguments:
     *   arg 1: return value (64 bit) */
    MONITOR_RETURN_ORDER
};

/* ==========================================================================
 * shared helpers
 * ========================================================================== */

bool isInstanceOf(void *buf, enum ConfFuzzOpcode e)
{
        return memcmp(buf, &e, sizeof(enum ConfFuzzOpcode)) == 0 ? true : false;
}

// standard says these should fit in an int - hopefully won't clash with any
// existing errno value. And yes, we shouldn't use errno in the first place.
#define ERRNO_CONFFUZZ_PIPE_TIMEOUT     1000
#define ERRNO_CONFFUZZ_INVALID_OPCODE   1001
#define ERRNO_CONFFUZZ_PIPE_READ_BUG_ON 1002

/* read one opcode from the FIFO pointed to by passed FD.
 * timeout can be passed via the second argument, 0 means no timeout, in which
 * case this function is blocking until input from the pipe is received.
 *
 * return ConfFuzzOpcode::INVALID_OPCODE if the read failed.
 *
 * if the read failed (i.e., if this method returns ConfFuzzOpcode::INVALID_OPCODE)
 * this method return an error code via errno:
 *  - any errno possibly set by select() or read()
 *  - ERRNO_CONFFUZZ_PIPE_TIMEOUT if the timeout was reached
 *  - ERRNO_CONFFUZZ_INVALID_OPCODE if ConfFuzzOpcode::INVALID_OPCODE was actually
 *    received from the pipe
 */
enum ConfFuzzOpcode _readOpcodeWithTimeout(int fd, int timeout /* in seconds */)
{
        enum ConfFuzzOpcode e = ConfFuzzOpcode::INVALID_OPCODE;

        fd_set set;
        struct timeval timeout_tv;

        /* set timeout */
        timeout_tv.tv_sec  = timeout;
        timeout_tv.tv_usec = 0;

        FD_ZERO(&set);
        FD_SET(fd, &set);

        /* Select here to be able to timeout.
         * Loop to handle EINTR, as select doesn't handle it automatically:
         * https://www.man7.org/linux/man-pages/man7/signal.7.html */
        int rv, retry = 5;
        do {
            /* in Linux, the timeout is updated by select()
             * https://www.man7.org/linux/man-pages/man2/select.2.html */
            if (timeout)
                rv = select(fd + 1, &set, NULL, NULL, &timeout_tv);
            else
                rv = select(fd + 1, &set, NULL, NULL, NULL);
            retry--;
        } while (rv == -1 && errno == EINTR && retry);

        if (rv == -1) {
            /* select() failed */
            e = ConfFuzzOpcode::INVALID_OPCODE;
        } else if (rv == 0) {
            /* select() reached a timeout */
            errno = ERRNO_CONFFUZZ_PIPE_TIMEOUT;
        } else if (!timeout || rv != 0) {
            /* select succeeded */
            auto nread = read(fd, &e, sizeof(enum ConfFuzzOpcode));
            if (!nread) {
                /* If we get EINTR, try another few times.
                 * Getting EINTR is possible because we are operating on pipes. */

                retry = 5;
                while (errno == EINTR && !nread && retry) {
                    nread = read(fd, &e, sizeof(enum ConfFuzzOpcode));
                    if (nread == sizeof(enum ConfFuzzOpcode)) {
                        /* success! */
                        return e;
                    }
                    retry--;
                }

                if (nread == 0) {
                    errno = ERRNO_CONFFUZZ_PIPE_READ_BUG_ON;
                } else {
                    /* here errno is set by read() already */
                }

                e = ConfFuzzOpcode::INVALID_OPCODE;
            } else if (nread != sizeof(enum ConfFuzzOpcode)) {
                /* here errno is set by read() already */
                e = ConfFuzzOpcode::INVALID_OPCODE;
            } else if (e == ConfFuzzOpcode::INVALID_OPCODE) {
                errno = ERRNO_CONFFUZZ_INVALID_OPCODE;
            }
        }

        return e;
}

/* defined by the monitor/worker depending on logging needs */
enum ConfFuzzOpcode readOpcodeWithTimeout(int fd, int timeout /* in seconds */);
int _writeToFIFO(int fd, enum ConfFuzzOpcode e, void *buf, long int len);

enum ConfFuzzOpcode readOpcode(int fd)
{
        return readOpcodeWithTimeout(fd, 0);
}

#define WRITETOFIFO_SUCCESS 0
#define WRITETOFIFO_SYS_FAILURE 1
#define WRITETOFIFO_PARTIAL_FAILURE 2

int performWrite(int fd, void *buf, long int len)
{
        auto nrw = write(fd, buf, len);
        if (nrw != len) {
            // error code is in errno
            if (nrw == -1)
                return WRITETOFIFO_SYS_FAILURE;
            else
                return WRITETOFIFO_PARTIAL_FAILURE;
        }

        return WRITETOFIFO_SUCCESS;
}

/* pass ConfFuzzOpcode::INVALID_OPCODE as opcode `e` to write only
 * the payload and not the opcode (e.g., if you already wrote the
 * opcode separately via a raw performWrite). */
int writeToFIFO(int fd, enum ConfFuzzOpcode e, int payloadCount, ...)
{
        long int tLength = payloadCount * sizeof(uint64_t);
        if (e != ConfFuzzOpcode::INVALID_OPCODE)
            tLength += sizeof(enum ConfFuzzOpcode);

        char buffer[tLength];
        memset(buffer, 0, tLength);

        uintptr_t bufferStart = (uintptr_t) &buffer[0];

        if (e != ConfFuzzOpcode::INVALID_OPCODE) {
            memcpy(buffer, &e, sizeof(enum ConfFuzzOpcode));
            bufferStart += sizeof(enum ConfFuzzOpcode);
        }

        if (payloadCount) {
            va_list vl;

            va_start(vl, payloadCount);
            for (int i = 0; i < payloadCount; i++)
            {
                uint64_t arg = va_arg(vl, uint64_t);
                memcpy((void *) (bufferStart + i * sizeof(uint64_t)),
                       (const void *) &arg, sizeof(arg));
            }

            va_end(vl);
        }

        return _writeToFIFO(fd, e, buffer, tLength);
}
