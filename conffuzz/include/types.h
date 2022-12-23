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

/* Returns whether or not passed string `value` ends with passed
 * string `ending`. */
inline bool endsWith(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

/* Return the size of a primitive type, or 0 if the type is not primitive */
static inline ssize_t primitiveTypeSize(std::string type)
{
    ssize_t ret = 0;

    /* Note: some of these are not really primitives, but that's fine. */
    if (type == "int" || type == "signedint" || type == "signed") {
        ret = sizeof(int);
    } else if (type == "longint"    || type == "long" ||
               type == "signedlong" || type == "signedlongint") {
        ret = sizeof(long int);
    } else if (type == "longlongint"    || type == "longlong" ||
               type == "signedlonglong" || type == "signedlonglongint") {
        ret = sizeof(long long int);
    } else if (type == "shortint"    || type == "short" ||
               type == "signedshort" || type == "signedshortint" ||
               type == "shortsigned" || type == "shortsignedint") {
        ret = sizeof(short int);
    } else if (type == "shortunsignedint"    || type == "shortunsigned" ||
               type == "unsignedshortint" || type == "unsignedshort") {
        ret = sizeof(unsigned short int);
    } else if (type == "float") {
        ret = sizeof(float);
    } else if (type == "double") {
        ret = sizeof(double);
    } else if (type == "longdouble") {
        ret = sizeof(long double);
    } else if (type == "longunsignedint" || type == "unsignedlongint" ||
               type == "longunsigned"    || type == "unsignedlong") {
        ret = sizeof(long unsigned int);
    } else if (type == "unsignedint" || type == "unsigned") {
        ret = sizeof(unsigned int);
    } else if (type == "int32"  || type == "int32_t") {
        ret = sizeof(int32_t);
    } else if (type == "uint32" || type == "uint32_t") {
        ret = sizeof(uint32_t);
    } else if (type == "int64"  || type == "int64_t") {
        ret = sizeof(uint64_t);
    } else if (type == "uint64" || type == "uint64_t") {
        ret = sizeof(int64_t);
    } else if (type == "size_t") {
        ret = sizeof(size_t);
    } else if (type == "ssize_t") {
        ret = sizeof(ssize_t);
    } else if (type == "time_t") {
        ret = sizeof(time_t);
    } else if (type == "uint8_t"      || type == "char" ||
               type == "unsignedchar" || type == "signedchar" ||
               type == "void") {
        ret = sizeof(uint8_t);
    } else if (endsWith(type,"*")) {
        ret = sizeof(void*);
    } /* etc. add more fundamental types as we go */

    return ret;
}
