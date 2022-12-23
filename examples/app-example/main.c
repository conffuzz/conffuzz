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

#include <stdio.h> /* printf */
#include <string.h> /* memset */
#include <example.h>

int main(int argc, char *argv[])
{
	/* this is just to test argument passing */
	if (argc > 1) {
	    printf("Called with arguments: ");
	    for (int i = 0; i < argc; i++) {
	        printf(argv[i]);
		if (i != argc - 1)
	            printf(", ");
	    }
	    printf("\n");
	} else {
	    printf("Called without arguments.\n");
	}

	fflush(stdout);

	char *string = get_string(4);

	/* this here is just meant to showcase the ability of ConfFuzz to
	 * detect call sites. no exploitable vulnerability. */
	for (int i = 0; i < 4; i++) {
	    char *e = get_string(4);
	    /* do something with these strings */
	    (void) e;
	}

	manyargs_func(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);

	printf("Here is a 4 character string from libexample: ");
	/* XXX [1] potential arbitrary read, mostly a crasher? */
	printf("%s\n", string);

	printf("Modifying the string...");
	/* XXX [2] arbitrary write but only specific chars
	 * can be access with a carefuly crafted pointer (to mapped area) */
	string[0] = 'b';
	string[1] = 'o';
	string[2] = 'o';
	string[3] = 'm';

	printf("\nAnd here it is: ");
	printf("%s\n", string);

        struct exampleStruct *obj = get_object();
	/* XXX [3] execution of library controlled pointer */
	char *a = obj->funcptr(obj->integer);

	printf("Here is a final string from the lib: ");
	/* XXX [4] duplicate of vulnerability [1] */
	printf("%s\n", a);

	/* zero out the strings for some more stuff */
	size_t l = get_string_length(string);
	/* XXX [5] arbitrary write of any number of zeros */
	memset(string, 0, l);

	/* do more stuff */

	return 0;
}
