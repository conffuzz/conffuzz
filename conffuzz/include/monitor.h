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

#include <utility> /* std::pair */
#include <list> /* std::list */
#include <vector> /* std::vector */
#include <string> /* std::string */
#include "conffuzz.h"

/* ==========================================================================
 * Corpus storage data structures
 * ========================================================================== */

/* conffuzzAction: represent the monitor answer to a worker event */
typedef std::pair<
        enum ConfFuzzOpcode /* opcode of answer triggered */,
        std::vector<char> /* final answer buffer */
> ConfFuzzMsg;

/* conffuzzEvent: represent one worker event and the corresponding
 * monitor answer */
typedef std::pair<
        ConfFuzzMsg /* opcode of the event */,
        std::list<ConfFuzzMsg> /* final response */
> ConfFuzzEvent;

/* conffuzzCorpus: represent a full fuzzing cycle of consecutive events
 * and answers */
typedef std::list<ConfFuzzEvent> ConfFuzzCorpus;

/* ==========================================================================
 * Corpus management helpers
 * ========================================================================== */

enum ConfFuzzOpcode getEventOpcode(ConfFuzzEvent e)
{
        return e.first.first;
}
