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

#include <stdlib.h> /* malloc */
#include <string.h> /* strlen */
#include "include/example.h"

char *get_string(int len)
{
	char *string = malloc(len + 1);
	for (int i = 0; i < len; i++) {
		string[i] = 'x';
	}
	string[len] = '\0';
	return string;
}

size_t get_string_length(char *s)
{
	return strlen(s) + 1;
}

void set_ptr(char **ptr)
{
	*ptr = malloc(10);
}

void set_ptr_struct(struct exampleStruct *s)
{
	s->something = 1;
}

struct exampleStruct *get_object(void)
{
	struct exampleStruct *obj = malloc(sizeof(struct exampleStruct));
	obj->funcptr = &get_string;
	obj->integer = 6;
	return obj;
}

void call_me_back(void (*callback)(char *))
{
	char *string = malloc(1);
	callback(string);
}

/* These functions are meant to test the symbol detection
 * features of ConfFuzz. */

/* variable number of arguments; only argument 1 will be considered */
int vararg_func(int, ... )
{
        return 21;
}

/* no arguments */
int noarg_func(void)
{
        /* test reentrance */
        char *string = get_string(3);
        if (string[1] == 'a') {
                return 1;
        }
        return 0;
}

/* ten arguments */
int manyargs_func(int arg1, int arg2, int arg3, int arg4, int arg5,
                  int arg6, int arg7, int arg8, int arg9, int arg10)
{
        if (arg1 == 187) {
            void (*fun_ptr)(void) = (void*) 0xdeadbeef;
            fun_ptr();
        }
        return 84;
}

/* five arguments of different types */
int manyargs_func2(int arg1, char *arg2, void *arg3, long unsigned arg4,
                   int *arg5)
{
        return 84;
}
