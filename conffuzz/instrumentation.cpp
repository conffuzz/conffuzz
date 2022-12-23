/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * Copyright (C) 2022 Hugo Lefeuvre (The University of Manchester).
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <list>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include "include/conffuzz.h"
#include "include/types.h"

using std::cerr;
using std::endl;
using std::hex;
using std::ios;
using std::string;

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;

/* FIFOs used for monitor / instrumentation communication,
 * see include/conffuzz.h */
int workerFIFOfd;
int monitorFIFOfd;

/* FIFO used to safely write to potentially invalid memory addresses,
 * see writeSafe */
int safewriteFIFOfd;

/* custom type table, here we store a mapping between custom
 * library/application types and their size. This is populated
 * at startup using the file from KnobLibraryTypes, and
 * by parseType as we process function signatures. */
std::map<std::string, int> typeMap;

/* custom type list that stores types for which we are unable to
 * determine the size via DWARF */
std::list<std::string> unknownTypeSizeList;

/* this table is used to store arguments from LibFuncBefore() to
 * LibFuncAfter(). This is a hack, mostly. We need it to be able
 * to remember the type of the object we are writing to, because
 * the monitor doesn't tell us. */
std::map<THREADID, std::map<CHAR*, std::pair<uintptr_t, uintptr_t>>> argumentMap;

/* necessary to synchronize monitor FIFO accesses across app threads */
PIN_MUTEX monitorLock;

/* Reentrance state variables.
 *
 * We need to keep track of whether we are in the library (libraryBeacon),
 * and whether we are currently processing a callback (appBeacon).
 *
 * These need to be vectors, because a callback can recurse into the library.
 * reentranceIndex keeps track of the current list index.
 *
 * All of these are thread local, so no need to sync.
 *
 * We are forced to use Pin's threading API for that.
 */
TLS_KEY reentranceIndex;
TLS_KEY libraryBeacon;
TLS_KEY appBeacon;

/* worker scratch file, just something we use to write temporary stuff */
#define WORKER_SCRATCH_PATH "/tmp/worker_scratch"

/* worker scratch file, just something we use to write temporary stuff */
#define SAFEWRITE_FIFO_PATH "/tmp/worker_safewrite_fifo"

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< BOOL > KnobInstrumentRetCB(KNOB_MODE_WRITEONCE,  "pintool", "instrRetCB",  "0", "instrument callback returns Y/N");
KNOB< BOOL >   KnobNoMonitor(KNOB_MODE_WRITEONCE,      "pintool", "noMonitor",   "0", "no monitor benchmarking mode Y/N");
KNOB< BOOL >   KnobSuperVerbose(KNOB_MODE_WRITEONCE,   "pintool", "Verbose",     "0", "enable debug verbosity Y/N");
KNOB< string > KnobOutputFile(KNOB_MODE_WRITEONCE,     "pintool", "o",           "",  "trace file name");
KNOB< string > KnobLibrarySymbols(KNOB_MODE_WRITEONCE, "pintool", "symbols",     "",  "library symbols to instrument");
KNOB< string > KnobLibraryTypes(KNOB_MODE_WRITEONCE,   "pintool", "typesPath",   "",  "path to type information text db");
KNOB< string > KnobSymbolTool(KNOB_MODE_WRITEONCE,     "pintool", "symboltool",  "",  "path to the symbol tool");
KNOB< string > KnobTypeTool(KNOB_MODE_WRITEONCE,       "pintool", "typetool",    "",  "path to the type tool");
KNOB< string > KnobFifoMonitor(KNOB_MODE_WRITEONCE,    "pintool", "fifoMonitor", "",  "fifo path of the monitor");
KNOB< string > KnobFifoWorker(KNOB_MODE_WRITEONCE,     "pintool", "fifoWorker",  "",  "fifo path of this worker");
KNOB< string > KnobLibPaths(KNOB_MODE_APPEND,          "pintool", "libPath",     "",
                                                       "target lib path (can be passed several times)");

/* this weak symbol is overridden by the monitor to log actions */
enum ConfFuzzOpcode readOpcodeWithTimeout(
    int fd, int timeout /* in seconds */)
{
        return _readOpcodeWithTimeout(fd, timeout);
}

int _writeToFIFO(int fd, enum ConfFuzzOpcode e, void *buf, long int len)
{
        return performWrite(fd, buf, len);
}

/* ===================================================================== */
/* Helper routines                                                       */
/* ===================================================================== */

#define cerrPrefix "[E] {" << __FILE__ << ":" << __LINE__ << "} "

#define writeToFIFOchecked(...)                                                \
do {                                                                           \
       int _wtfc_ret = writeToFIFO(__VA_ARGS__);                               \
       if (_wtfc_ret > WRITETOFIFO_SUCCESS) {                                  \
               cerr << cerrPrefix << "Failed worker FIFO write" << endl;       \
               if (_wtfc_ret == WRITETOFIFO_SYS_FAILURE)                       \
                       cerr << cerrPrefix << "Reason: system error, "          \
                               "errno: " << errno << endl;                     \
               else                                                            \
                       cerr << cerrPrefix << "Reason: partial write. "         \
                               "That was unlikely!" << endl;                   \
               exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);                    \
       }                                                                       \
} while (0);

/* shamelessly taken from a stackoverflow post. C++ god, be merciful!
 *
 * first snow-
 *   coating the bridge
 *      under construction
 *
 * (Basho)
 */
int nthOccurrence(const std::string& str, const std::string& findMe, int nth)
{
    size_t  pos = 0;
    int     cnt = 0;

    while( cnt != nth )
    {
        pos+=1;
        pos = str.find(findMe, pos);
        if ( pos == std::string::npos )
            return -1;
        cnt++;
    }
    return pos;
}

/* Use the kernel to try and write somewhere without crashing if the
 * position is not mapped. */
bool writeSafe(void *p, uint64_t size, void *val, void *before)
{
    /* 1. Get value before overwriting */
    int done = write(safewriteFIFOfd, p, size);
    if (done < 0 || (uint64_t) done != size) {
        return false;
    }

    done = read(safewriteFIFOfd, before, size);
    if (done < 0 || (uint64_t) done != size) {
        return false;
    }

    /* 2. Overwrite safely */
    done = write(safewriteFIFOfd, val, size);
    if (done < 0 || (uint64_t) done != size) {
        return false;
    }

    done = read(safewriteFIFOfd, p, size);
    if (done < 0 || (uint64_t) done != size) {
        return false;
    }

    return true;
}

void pipeOperationError(string err)
{
    if (!err.empty())
        cerr << err << endl;
    exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
}

void replaceFirst(std::string& s, std::string const& toReplace,
                  std::string const& replaceWith)
{
    std::size_t pos = s.find(toReplace);
    if (pos == std::string::npos) return;
    s.replace(pos, toReplace.length(), replaceWith);
}

void normalizeType(std::string& s)
{
    /* remove pointer * or reference & at the end */
    if (endsWith(s, "*") || endsWith(s, "&")) {
        s.pop_back();
    }

    /* convert to lowercase */
    std::transform(s.begin(), s.end(), s.begin(), std::tolower);
}

/* Helper function to spawn the type analysis script. Running this at too high
 * a frequency (however that is defined) seems to cause issues, so try to be
 * gentle with it please. */
extern char **environ;
void spawnTypeScript(char *type)
{
    if (!strlen(type)) {
        cerr << cerrPrefix << "type cannot be empty!" << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    char **argV = (char **) malloc(sizeof(char *) * (
                    + 1 /* tool path    */
                    + 1 /* type db      */
                    + 1 /* scratch path */
                    + 1 /* type name    */
                    + KnobLibPaths.NumberOfValues() /* lib paths */
                    + 1 /* NULL char    */));

    int i = 0;
    argV[i++] = const_cast<char*>(KnobTypeTool.Value().c_str());
    argV[i++] = const_cast<char*>(KnobLibraryTypes.Value().c_str());
    argV[i++] = const_cast<char*>(WORKER_SCRATCH_PATH);
    argV[i++] = type;

    for (UINT32 j = 0; j < KnobLibPaths.NumberOfValues(); j++) {
        argV[i++] = const_cast<char*>(KnobLibPaths.Value(j).c_str());
        if (!strlen(argV[i - 1])) {
            cerr << cerrPrefix << "library path cannot be empty!" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

    }

    argV[i++] = NULL;

    cerr << "[I] Retrieving information from DWARF for type " << type << endl;

    pid_t pid = fork();

    if (pid == -1) {
        cerr << cerrPrefix << "Failed to spawn type analysis script, fork errno: " << errno << endl;

        /* In this case, exorcize the daemons:
         *   Il faut que son dédain, las !
         *   Me livre au plus prompt trépas !
         */

        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    if (pid == 0) {
        int r = execvp(argV[0], argV);
        int errno_backup = errno;

        if (r == -1) {
            cerr << cerrPrefix << "Failed to exec type analysis script, execvp errno: " << errno_backup << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }
    }

    if (waitpid(pid, NULL, 0) == -1) {
        int errno_backup = errno;
        cerr << cerrPrefix << "Failed to waitpid on type analysis script, errno: " << errno_backup << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }
}

ssize_t fetchFromDWARF(std::string type)
{
    auto ret = 0;

    if (std::find(unknownTypeSizeList.begin(), unknownTypeSizeList.end(), type)
            != unknownTypeSizeList.end()) {
        /* we already tried and DWARF couldn't find it */
        return ret;
    }

    remove(WORKER_SCRATCH_PATH);

    spawnTypeScript(const_cast<char*>(type.c_str()));

    std::ifstream infile(WORKER_SCRATCH_PATH);
    std::stringstream ss;
    if (infile.good()) {
        std::string sLine;
        getline(infile, sLine);
        if (!sLine.empty()) {
            /* we found it in DWARF */
            ss << sLine;
            ss >> ret;
        }

        /* add to the table anyways, if we failed we don't want to
         * redo the search, it's not going to change anything... */
        typeMap.insert({type, ret});
    }

    return ret;
}

int parseType(std::string type)
{
    /* default on 'not a pointer' */
    int ret = 0;

    /* only consider pointers */
    if (!endsWith(type, "*") && !endsWith(type, "&")) {
        return ret;
    }

    /* default on something safe */
    ret = 1;

    normalizeType(type);

    if (type.empty()) {
        /* this is a special case where we only got "*" or "&" */
        return ret;
    }

    if (type.find(')') != std::string::npos) {
        /* another special case: function pointers like
         * "int(uint32_t,*)*". Our tooling doesn't really
         * like them, so let's stay conservative for now. */
        return ret; /* FIXME */
    }

    auto primitiveSize = primitiveTypeSize(type);

    if (primitiveSize) {
        ret = primitiveSize;
    } else {
        /* otherwise, can we grab the size information from DWARF? */
        bool found = false;

        /* check if we have this type in the custom type table */
        for (auto itr = typeMap.begin(); itr != typeMap.end(); itr++)
        {
            if (type == itr->first) {
                /* we found it in the table */
                found = true;
                ret = itr->second;
                    break;
            }
        }

        /* if not, try to fetch it from the library's DWARF */
        if (!found) {
            auto dwarfValue = fetchFromDWARF(type);
            if (dwarfValue) {
                ret = dwarfValue;
            } else {
                unknownTypeSizeList.push_back(type);
            }
        }
    }

    return ret;
}

void parseTypeList(std::string line, int argc, UINT64 types[], char *typeNames[])
{
    std::string sub = line, typeString;
    replaceFirst(line, " ", "");
    if (KnobSuperVerbose)
        std::cout << "Type list: " << line << endl;
    for (int i = 0; i < argc; i++) {
        /* always (_, _, 1) as we modify sub as we go */
        auto nextPos = nthOccurrence(sub, " ", 1);
        if (nextPos == -1) {
            /* last one in the list */
            typeString = sub.substr(1);
            if (i != argc - 1) {
                cerr << cerrPrefix << "Malformed type list (" << i
                     << "!=" << argc - 1 << ")." << endl;
                cerr << cerrPrefix << "Line: " << line << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }
        } else {
            typeString = sub.substr(1, nextPos - 1);
            sub = sub.substr(nextPos);
        }

        types[i] = parseType(typeString);

        /* make the single star (standard DWARF) more readable */
        if (typeString == "*")
            typeString = "void*";

        // long lived string, never freed
        typeNames[i] = (char *) malloc(strlen(typeString.c_str()) + 1);

        if (!typeNames[i]) {
            cerr << cerrPrefix << "Fatal: OOM" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        strcpy(typeNames[i], typeString.c_str());
        if (KnobSuperVerbose)
            std::cout << "   " << typeString << "->" << types[i] << endl;
    }
}

int _getFuncArgumentNumberByName(std::string fname, UINT64 types[],
  char *typeNames[], char **returnType, bool recurse)
{
    int narg = -1;

    /* 1. Try to retrieve it from our cache */
    std::ifstream cbFile(WORKER_CALLBACK_FILE);
    for (std::string line; getline(cbFile, line);)
    {
        if (endsWith(line, "*/")) {
            /* this is a comment */
            continue;
        }
        auto _libName = line.substr(0, line.find(' '));
        auto _libLinkageNamePos = nthOccurrence(line, " ", 1);
        auto _funcArgcPos = nthOccurrence(line, " ", 2);
        auto _retTypePos = nthOccurrence(line, " ", 3);
        auto _libArg1TypePos = nthOccurrence(line, " ", 4);

        /* these should just always be there */
        if (_libLinkageNamePos == -1 || _funcArgcPos == -1 || _retTypePos == -1) {
            continue;
        }

        auto _libLinkageName = line.substr(_libLinkageNamePos + 1,
                                           _funcArgcPos - _libLinkageNamePos - 1);

        if (fname == _libLinkageName) {
            narg = atoi(line.substr(_funcArgcPos + 1,
                                    _retTypePos - _funcArgcPos - 1).c_str());

            std::string _returnType;
            if (_libArg1TypePos == -1) {
                if (narg != 0) {
                    cerr << cerrPrefix << "Malformed callback symbol file "
                         << "(number of arguments)." << endl;
                    exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
                }
                _returnType = line.substr(_retTypePos + 1);
            } else {
                _returnType = line.substr(_retTypePos + 1,
                                          _libArg1TypePos - _retTypePos - 1);
            }

            if (_returnType == "*")
                _returnType = "void*";

            if (_returnType.size() == 0) {
                cerr << cerrPrefix << "Malformed callback symbol file "
                     << "(size of return value type)." << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }

            *returnType = (char *) malloc(strlen(_returnType.c_str()) + 1);
            strcpy(*returnType, _returnType.c_str());

            /* grab argument sizes */
            if (_libArg1TypePos != -1) {
                parseTypeList(line.substr(_libArg1TypePos), narg, types, typeNames);
            }

            cerr << cerrPrefix << "Got function argument numbers ("
                 << narg << ") from cache!" << endl;
            return narg;
        }
    }

    if (!recurse) {
        cerr << cerrPrefix << "Failed to retrieve information about "
             << fname << endl;
        return 0;
    }

    /* 2. Not found in the cache, compute it and add to the cache. */
    std::ostringstream oss;
    oss << KnobSymbolTool.Value().c_str() << " " << fname << " "
        << WORKER_MAPPINGS_COPY_PATH << " >> " << WORKER_CALLBACK_FILE;

    FILE *FP = popen(oss.str().c_str(), "r");

    if (!FP) {
        cerr << cerrPrefix << "Failed to spawn symbol analysis tool, errno: " << errno << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    int r = pclose(FP); /* clean way would be to read directly from this */

    if (r == -1) {
        cerr << cerrPrefix << "Failed to pclose symbol analysis tool, errno: " << errno << endl;
        /* should be non fatal? */
    }

    cerr << "[I] Acquired data about " << fname << ", recursing." << endl;
    return _getFuncArgumentNumberByName(fname, types, typeNames, returnType, false);
}

int getFuncArgumentNumberByName(std::string fname, UINT64 types[],
  char *typeNames[], char **returnType)
{
    return _getFuncArgumentNumberByName(fname, types, typeNames, returnType, true);
}

/* Best-effort attempt to determine the type of passed pointer using
 * the knowledge of function arguments that we have. It should actually
 * succeed in most cases since this is a pointer passed by the monitor
 * derived from argument pointers.
 *
 * This MUST be called while holding the monitor lock.
 *
 * I’ve hit the bottom
 *  of my bag of discretion:
 *    year’s end
 */
void PRINT_TYPE_ADDR(THREADID TID, uintptr_t ptr)
{
    auto m = argumentMap[TID];
    for (auto it = m.begin(); it != m.end(); ++it){
        if (ptr >= it->second.first && ptr < it->second.second) {
            TraceFile << it->first;
            return;
        }
    }
    TraceFile << "unknown";
}

/* ===================================================================== */
/* Analysis routines                                                     */
/* ===================================================================== */

VOID LibFuncAfter(THREADID TID, CHAR* name, BOOL isCB, ADDRINT *addr,
                  CHAR* retType)
{
    if (KnobNoMonitor) {
        return; /* we just want to measure the cost of Pin */
    }

    int* index = static_cast<int*>(PIN_GetThreadData(reentranceIndex, TID));
    if (isCB) {
        /* set the callback indicator */
        std::vector<int>* p = static_cast<std::vector<int>*>(PIN_GetThreadData(appBeacon, TID));
        p->at(*index)--;

        /* sanity check callback state */
        if (p->at(*index) < 0) {
            cerr << cerrPrefix << "Corrupted callback state, callback beacon = " << p->at(*index)
                 << " < 0, this is a bug." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* was it the application calling its own callback? */
        std::vector<int>* tdata = static_cast<std::vector<int>*>(PIN_GetThreadData(libraryBeacon, TID));
        if (tdata->at(*index) == 0)
            return; /* yes, let's not mutate anything */

        /* sanity check reentrance beacon */
        if (tdata->at(*index) < 0) {
            cerr << cerrPrefix << "Corrupted reentrance beacon, *tdata = " << tdata->at(*index)
                 << " < 0, this is a bug." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* was it the callback recursively calling itself? */
        if (p->at(*index) > 0)
            return; /* yes, let's not mutate anything */
    } else {
        std::vector<int>* p = static_cast<std::vector<int>*>(PIN_GetThreadData(appBeacon, TID));
        std::vector<int>* tdata = static_cast<std::vector<int>*>(PIN_GetThreadData(libraryBeacon, TID));

        /* set the reentrance beacon */
        tdata->at(*index)--;

        /* are we leaving a library internal call? */
        if (tdata->at(*index) > 0)
            return; /* yes, let's not mutate */

        /* are we returning from a deeply nested API call? */
        if (tdata->at(*index) == 0 && *index != 0) {
            cerr << cerrPrefix << "Returning from a deeply nested call." << endl;

            /* sanity check callback state */
            if (p->at(*index) != 0) {
                cerr << cerrPrefix << "Corrupted callback state, callback beacon = " << p->at(*index)
                     << " != 0, this is a bug." << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }

            (*index)--;
            p->pop_back();
            tdata->pop_back();

            /* sanity check callback state */
            if (p->at(*index) < 1) {
                cerr << cerrPrefix << "Corrupted callback state, callback beacon = " << p->at(*index)
                     << " < 1, this is a bug." << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }

            /* sanity check library call state */
            if (tdata->at(*index) < 1) {
                cerr << cerrPrefix << "Corrupted reentrance beacon, library call beacon = " << tdata->at(*index)
                     << " < 1, this is a bug." << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }
        }

        /* sanity check callback state */
        if (p->at(*index) < 0) {
            cerr << cerrPrefix << "Corrupted callback state, callback beacon = " << p->at(*index)
                 << " < 0, this is a bug." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* sanity check library call state */
        if (tdata->at(*index) < 0) {
            cerr << cerrPrefix << "Corrupted reentrance beacon, library call beacon = " << tdata->at(*index)
                 << " < 0, this is a bug." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }
    }

    PIN_MutexLock(&monitorLock);

    /* send return value to monitor */
    uint64_t ret = (uint64_t) *addr;
    if (strcmp(retType, "unknown_or_void") != 0) {
        auto e = ConfFuzzOpcode::WORKER_LIBRARY_RETURN;
        if (isCB) {
            e = ConfFuzzOpcode::WORKER_CALLBACK_RETURN;
        }
        writeToFIFOchecked(workerFIFOfd, e, 1, ret);
    } else {
        auto e = ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL;
        if (isCB) {
            e = ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL;
        }
        write(workerFIFOfd, &e, sizeof(enum ConfFuzzOpcode));
    }

    /* now we may receive a number of write orders (possibly zero)
     * followed by a return order */
    enum ConfFuzzOpcode op = readOpcode(monitorFIFOfd);

    while (op == ConfFuzzOpcode::MONITOR_WRITE_ORDER) {
        /* retrieve address */
        uint64_t ptr = 0;
        int nread = read(monitorFIFOfd, &ptr, sizeof(ptr));
        if (nread != sizeof(ptr)) {
            cerr << cerrPrefix << "Failed to read from monitor FIFO" << endl;
        }

        /* retrieve size */
        uint64_t size = 0;
        nread = read(monitorFIFOfd, &size, sizeof(size));
        if (nread != sizeof(size)) {
            pipeOperationError("Failed to read from monitor FIFO");
        } else if (size > sizeof(uint64_t) || !size) {
            cerr << size << " ?> " << sizeof(uint64_t) << " (sizeof(uint64_t))" << endl;
            pipeOperationError("Monitor is sending garbage write size information");
        }

        /* retrieve value */
        char val[size];
        nread = read(monitorFIFOfd, &val[0], size);

        if (nread < 0 || (uint64_t) nread != size) {
            pipeOperationError("Failed to read from monitor FIFO");
        }

        uint64_t rest = sizeof(uint64_t) - size, tmp;
        nread = read(monitorFIFOfd, &tmp, rest);
        if (rest && nread > 0 && (uint64_t) nread != rest) {
            pipeOperationError("Failed to read from monitor FIFO");
        }

        /* perform write */
        uint64_t bef = 0, written = 0;

        if (writeSafe((void *) ptr, size, (void*) &val[0], &bef)) {
            memcpy((void*) (((uintptr_t) &written) + rest), &val[0], size);

            TraceFile << "~> write {";
            PRINT_TYPE_ADDR(TID, ptr);
            TraceFile << "} " << bef << " -> " << written << " at shared buffer "
                      << (void *) ptr << endl;
        } else {
            /* important note: this is non-fatal, in fight this is part
             * of the normal execution flow if pointers modified by the
             * instrumentation are given back to us */
            TraceFile << "~> failure to write " << size
                      << " bytes at address " << (void *) ptr << endl;
        }

        /* read next order */
        op = readOpcode(monitorFIFOfd);
    }

    ADDRINT prevRet = *addr;

    /* set the return value to whatever the monitor tells */
    if (op == ConfFuzzOpcode::MONITOR_RETURN_ORDER) {
        ret = 0;
        int nread = read(monitorFIFOfd, &ret, sizeof(ret));
        if (nread == sizeof(uint64_t)) {
            *addr = ret;
        }
    }

    if (prevRet != *addr) {
        TraceFile << "~> change ret {" << retType << "} from "
                  << prevRet << " to " << *addr << endl;
    }

    /* flush the argument map */
    argumentMap.erase(TID);

    PIN_MutexUnlock(&monitorLock);
}

/* Note: this instrumentation function is shared by both callbacks and normal
 * library calls */
VOID LibFuncBefore(THREADID TID, CHAR* mangledName, CHAR* name, void *callSite,
    BOOL isCB,      UINT32 argc,
    ADDRINT *arg1,  UINT64 arg1size,  CHAR* arg1TypeName,
    ADDRINT *arg2,  UINT64 arg2size,  CHAR* arg2TypeName,
    ADDRINT *arg3,  UINT64 arg3size,  CHAR* arg3TypeName,
    ADDRINT *arg4,  UINT64 arg4size,  CHAR* arg4TypeName,
    ADDRINT *arg5,  UINT64 arg5size,  CHAR* arg5TypeName,
    ADDRINT *arg6,  UINT64 arg6size,  CHAR* arg6TypeName,
    ADDRINT *arg7,  UINT64 arg7size,  CHAR* arg7TypeName,
    ADDRINT *arg8,  UINT64 arg8size,  CHAR* arg8TypeName,
    ADDRINT *arg9,  UINT64 arg9size,  CHAR* arg9TypeName,
    ADDRINT *arg10, UINT64 arg10size, CHAR* arg10TypeName,
    ADDRINT *arg11, UINT64 arg11size, CHAR* arg11TypeName,
    ADDRINT *arg12, UINT64 arg12size, CHAR* arg12TypeName,
    ADDRINT *arg13, UINT64 arg13size, CHAR* arg13TypeName,
    ADDRINT *arg14, UINT64 arg14size, CHAR* arg14TypeName,
    ADDRINT *arg15, UINT64 arg15size, CHAR* arg15TypeName,
    ADDRINT *arg16, UINT64 arg16size, CHAR* arg16TypeName,
    ADDRINT *arg17, UINT64 arg17size, CHAR* arg17TypeName)
{
    /* IMPORTANT NOTE: arguments are only valid depending on argc! */

    enum ConfFuzzOpcode op;
    int errnoBak;

    if (KnobNoMonitor) {
        return; /* we just want to measure the cost of Pin */
    }

    /* Start with some important checks:
     * - if we are a callback, make sure that the library is calling it, and not
     *   the app itself. It could be the app calling its own callback directly,
     *   or the callback recursively calling itself. In these cases we don't want
     *   to mutate anything as we're not crossing a security boundary. In all
     *   cases we want to set the callback indicator though.
     * - for each library API call, make sure that we aren't being called by an
     *   application callback, as this is not yet supported.
     * - for each library API call, check the reentrance beacon. If it is > 0,
     *   this means that we are doing a library internal call and we don't want
     *   to mutate anything. In all cases we set the reentrance beacon though.
     */

    int* index = static_cast<int*>(PIN_GetThreadData(reentranceIndex, TID));
    if (isCB) {
        /* set the callback indicator */
        std::vector<int>* p = static_cast<std::vector<int>*>(PIN_GetThreadData(appBeacon, TID));
        p->at(*index)++;

        /* sanity check callback state */
        if (p->at(*index) <= 0) {
            cerr << cerrPrefix << "Corrupted callback state, callback beacon = " << p->at(*index)
                 << " <= 0, this is a bug." << endl;
            cerr << cerrPrefix << "Function was: " << mangledName << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* is the application calling its own callback? */
        std::vector<int>* tdata = static_cast<std::vector<int>*>(PIN_GetThreadData(libraryBeacon, TID));
        if (tdata->at(*index) == 0)
            return; /* yes, let's not mutate anything */

        /* sanity check reentrance beacon */
        if (tdata->at(*index) < 0) {
            cerr << cerrPrefix << "Corrupted reentrance beacon, *tdata = " << tdata->at(*index)
                 << " < 0, this is a bug." << endl;
            cerr << cerrPrefix << "Function was: " << mangledName << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* is this a callback recursively calling itself? */
        if (tdata->at(*index) - 1 > 0)
            return; /* yes, let's not mutate anything */

        op = ConfFuzzOpcode::WORKER_CALLBACK_CALL;
    } else {
        std::vector<int>* p = static_cast<std::vector<int>*>(PIN_GetThreadData(appBeacon, TID));
        std::vector<int>* tdata = static_cast<std::vector<int>*>(PIN_GetThreadData(libraryBeacon, TID));

        /* are we currently processing a callback? */
        if (p->at(*index) > 0) {
            cerr << cerrPrefix << "Application is re-entering the library from "
                 << "a callback, function is: " << mangledName << endl;

            (*index)++;
            p->push_back(0);
            tdata->push_back(0);

            /* sanity check callback state */
            if (p->at(*index) != 0) {
                cerr << cerrPrefix << "Corrupted callback state, callback beacon = " << p->at(*index)
                     << " != 0, this is a bug." << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }

            /* sanity check library call state */
            if (tdata->at(*index) != 0) {
                cerr << cerrPrefix << "Corrupted reentrance beacon, library call beacon = " << tdata->at(*index)
                     << " != 0, this is a bug." << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }
        }

        /* sanity check callback state */
        if (p->at(*index) < 0) {
            cerr << cerrPrefix << "Corrupted callback state, callback beacon = " << p->at(*index)
                 << " < 0, this is a bug." << endl;
            cerr << cerrPrefix << "Function was: " << mangledName << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* set the reentrance beacon */
        tdata->at(*index)++;

        /* sanity check reentrance beacon */
        if (tdata->at(*index) <= 0) {
            cerr << cerrPrefix << "Corrupted reentrance beacon, *tdata = " << tdata->at(*index)
                 << " <= 0, this is a bug." << endl;
            cerr << cerrPrefix << "Function was: " << mangledName << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* are we entering a library internal call? */
        if (tdata->at(*index) - 1 > 0)
            return; /* yes, let's not mutate anything */

        op = ConfFuzzOpcode::WORKER_LIBRARY_CALL;
    }

    PIN_MutexLock(&monitorLock);

    if (isCB) {
        TraceFile << "~> detected cb call ";
    }

#define PRINTARG(n)                                                                 \
    do {                                                                            \
        if (n == 1 && argc > 0) TraceFile << *arg ## n                              \
                                          << " {" << arg ## n ## TypeName << "}"    \
                                          << " [size: " << arg ## n ## size << "]"; \
        else if (argc > n - 1)  TraceFile << ", " << *arg ## n                      \
                                          << " {" << arg ## n ## TypeName << "}"    \
                                          << " [size: " << arg ## n ## size << "]"; \
    } while (0)

    TraceFile << name << "(";
    PRINTARG(1);  PRINTARG(2);  PRINTARG(3);  PRINTARG(4); PRINTARG(5);
    PRINTARG(6);  PRINTARG(7);  PRINTARG(8);  PRINTARG(9); PRINTARG(10);
    PRINTARG(11); PRINTARG(12); PRINTARG(13); PRINTARG(14); PRINTARG(15);
    PRINTARG(16); PRINTARG(17);
    TraceFile << ") @ " << (void*) callSite << endl;

    /* create the argument map */
    argumentMap[TID] = {};

    /* send call site and function name */
    int ret = writeToFIFO(workerFIFOfd, op, 2, (uint64_t) callSite,
                          (uint64_t) strlen(mangledName));

    // don't use writeToFIFOchecked here because we want some custom logs
    if (ret > WRITETOFIFO_SUCCESS) {
        cerr << cerrPrefix << "Failed worker FIFO write" << endl;
        if (ret == WRITETOFIFO_SYS_FAILURE)
            cerr << cerrPrefix << "Reason: system error, errno: " << errno << endl;
        else
            cerr << cerrPrefix << "Reason: partial write." << endl;
        cerr << cerrPrefix << "Function was: " << mangledName << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    _writeToFIFO(workerFIFOfd, op, mangledName, strlen(mangledName));

#define REGISTER(n)                                                    \
    do {                                                               \
      if (arg ## n ## size)                                            \
          argumentMap[TID][arg ## n ## TypeName] =                     \
            std::make_pair<uintptr_t, uintptr_t>(                      \
              (uintptr_t) * arg ## n,                                  \
             ((uintptr_t) * (arg ## n)) + arg ## n ## size             \
            );                                                         \
    } while (0);

    /* send arguments. arguably not a clean solution, but it works.
     * PLUS: we use it to populate the argument map. */
    int narg = 1 + argc * 2 /* x2 because of size + actual arg */;
    switch (argc) {
    case 0:
        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc);
        break;
    case 1:
        REGISTER(1);
        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1);
        break;
    case 2:
        REGISTER(1); REGISTER(2);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2);
        break;
    case 3:
        REGISTER(1); REGISTER(2); REGISTER(3);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3);
        break;
    case 4:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4);
        break;
    case 5:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5);
        break;
    case 6:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5); REGISTER(6);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6);
        break;
    case 7:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5); REGISTER(6); REGISTER(7);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7);
        break;
    case 8:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8);
        break;
    case 9:
       REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
       REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
       REGISTER(9);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9);
        break;
    case 10:
       REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
       REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
       REGISTER(9); REGISTER(10);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10);
        break;
    case 11:
       REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
       REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
       REGISTER(9); REGISTER(10); REGISTER(11);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10,
                (uint64_t) arg11size, (uint64_t) *arg11);
        break;
    case 12:
       REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
       REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
       REGISTER(9); REGISTER(10); REGISTER(11); REGISTER(12);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10,
                (uint64_t) arg11size, (uint64_t) *arg11,
                (uint64_t) arg12size, (uint64_t) *arg12);
        break;
    case 13:
       REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
       REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
       REGISTER(9); REGISTER(10); REGISTER(11); REGISTER(12);
       REGISTER(13);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10,
                (uint64_t) arg11size, (uint64_t) *arg11,
                (uint64_t) arg12size, (uint64_t) *arg12,
                (uint64_t) arg13size, (uint64_t) *arg13);
        break;
    case 14:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
        REGISTER(9); REGISTER(10); REGISTER(11); REGISTER(12);
        REGISTER(13); REGISTER(14);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10,
                (uint64_t) arg11size, (uint64_t) *arg11,
                (uint64_t) arg12size, (uint64_t) *arg12,
                (uint64_t) arg13size, (uint64_t) *arg13,
                (uint64_t) arg14size, (uint64_t) *arg14);
        break;
    case 15:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
        REGISTER(9); REGISTER(10); REGISTER(11); REGISTER(12);
        REGISTER(13); REGISTER(14); REGISTER(15);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10,
                (uint64_t) arg11size, (uint64_t) *arg11,
                (uint64_t) arg12size, (uint64_t) *arg12,
                (uint64_t) arg13size, (uint64_t) *arg13,
                (uint64_t) arg14size, (uint64_t) *arg14,
                (uint64_t) arg15size, (uint64_t) *arg15);
        break;
    case 16:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
        REGISTER(9); REGISTER(10); REGISTER(11); REGISTER(12);
        REGISTER(13); REGISTER(14); REGISTER(15); REGISTER(16);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10,
                (uint64_t) arg11size, (uint64_t) *arg11,
                (uint64_t) arg12size, (uint64_t) *arg12,
                (uint64_t) arg13size, (uint64_t) *arg13,
                (uint64_t) arg14size, (uint64_t) *arg14,
                (uint64_t) arg16size, (uint64_t) *arg15,
                (uint64_t) arg15size, (uint64_t) *arg16);
        break;
    case 17:
        REGISTER(1); REGISTER(2); REGISTER(3); REGISTER(4);
        REGISTER(5); REGISTER(6); REGISTER(7); REGISTER(8);
        REGISTER(9); REGISTER(10); REGISTER(11); REGISTER(12);
        REGISTER(13); REGISTER(14); REGISTER(15); REGISTER(16);
        REGISTER(17);

        writeToFIFOchecked(workerFIFOfd, ConfFuzzOpcode::INVALID_OPCODE,
                narg, (uint64_t) argc,
                (uint64_t) arg1size, (uint64_t) *arg1,
                (uint64_t) arg2size, (uint64_t) *arg2,
                (uint64_t) arg3size, (uint64_t) *arg3,
                (uint64_t) arg4size, (uint64_t) *arg4,
                (uint64_t) arg5size, (uint64_t) *arg5,
                (uint64_t) arg6size, (uint64_t) *arg6,
                (uint64_t) arg7size, (uint64_t) *arg7,
                (uint64_t) arg8size, (uint64_t) *arg8,
                (uint64_t) arg9size, (uint64_t) *arg9,
                (uint64_t) arg10size, (uint64_t) *arg10,
                (uint64_t) arg11size, (uint64_t) *arg11,
                (uint64_t) arg12size, (uint64_t) *arg12,
                (uint64_t) arg13size, (uint64_t) *arg13,
                (uint64_t) arg14size, (uint64_t) *arg14,
                (uint64_t) arg15size, (uint64_t) *arg15,
                (uint64_t) arg16size, (uint64_t) *arg16,
                (uint64_t) arg17size, (uint64_t) *arg17);
        break;
    default:
        cerr << cerrPrefix << "Bug in LibFuncBefore! Default case reached." << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    /* now we may receive a number of instrumentation orders and write argument
     * orders (possibly zero) followed by an execution order */
    op = readOpcode(monitorFIFOfd);
    errnoBak = errno;

    while (op == ConfFuzzOpcode::MONITOR_INSTRUMENT_ORDER) {
        /* retrieve function address */
        uint64_t ptr = 0;
        int nread = read(monitorFIFOfd, &ptr, sizeof(ptr));

        if (nread != sizeof(ptr)) {
            cerr << cerrPrefix << "Failed to read from monitor FIFO" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        auto name = RTN_FindNameByAddress(ptr);
        UINT64 types[LIBAPI_ARG_UPPER_LIMIT];
        char  *typeNames[LIBAPI_ARG_UPPER_LIMIT];
        char *returnType;
        cerr << "[I] Attempting to instrument callback " << name << endl;
        auto callbackArgs = getFuncArgumentNumberByName(name, types, typeNames, &returnType);

        if (callbackArgs == -1) {
            cerr << cerrPrefix << "Could not find argument number for callback " << name << endl;
            op = readOpcode(monitorFIFOfd);
            break;
        }

        if (callbackArgs > LIBAPI_ARG_UPPER_LIMIT) {
            cerr << cerrPrefix << "Callback " << name << " reaches argument upper limit of "
                 << LIBAPI_ARG_UPPER_LIMIT << ", this is an easy fix, contact "
                 << "maintainers." << endl;
            op = readOpcode(monitorFIFOfd);
            break;
        }

        PIN_LockClient();
        RTN libFuncRtn = RTN_FindByAddress(ptr);
        PIN_UnlockClient();

        /* we have to do this for scope issues; don't care about freeing, this
         * should live as long as the program is up */
        char *libName = (char *) malloc(strlen(name.c_str()) + 1);
        strcpy(libName, name.c_str());

        if (RTN_Valid(libFuncRtn))
        {
            TraceFile << "~> instrumenting " << (void *) ptr << " (" << name <<
                         ", " << callbackArgs << " arg(s))" << endl;

            RTN_Open(libFuncRtn);

            /* instrument entry block of function */
            RTN_InsertCall(libFuncRtn, IPOINT_BEFORE, (AFUNPTR)LibFuncBefore,
                IARG_THREAD_ID, /* thread id */
                /* mangled name of the function, unmangled name of the function */
                /* note: both same here, unfortunately we don't have the unmangled
                 * name... */
                IARG_ADDRINT, libName, IARG_ADDRINT, libName,
                /* call site, yes this is a callback */
                IARG_RETURN_IP, IARG_BOOL, true,
                /* argument count */
                IARG_UINT32, callbackArgs,
                /* arguments and their sizes; only valid depending on funcArgc */
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 0,  IARG_UINT64, types[0], IARG_ADDRINT, typeNames[0],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 1,  IARG_UINT64, types[1], IARG_ADDRINT, typeNames[1],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 2,  IARG_UINT64, types[2], IARG_ADDRINT, typeNames[2],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 3,  IARG_UINT64, types[3], IARG_ADDRINT, typeNames[3],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 4,  IARG_UINT64, types[4], IARG_ADDRINT, typeNames[4],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 5,  IARG_UINT64, types[5], IARG_ADDRINT, typeNames[5],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 6,  IARG_UINT64, types[6], IARG_ADDRINT, typeNames[6],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 7,  IARG_UINT64, types[7], IARG_ADDRINT, typeNames[7],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 8,  IARG_UINT64, types[8], IARG_ADDRINT, typeNames[8],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 9,  IARG_UINT64, types[9], IARG_ADDRINT, typeNames[9],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 10, IARG_UINT64, types[10], IARG_ADDRINT, typeNames[10],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 11, IARG_UINT64, types[11], IARG_ADDRINT, typeNames[11],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 12, IARG_UINT64, types[12], IARG_ADDRINT, typeNames[12],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 13, IARG_UINT64, types[13], IARG_ADDRINT, typeNames[13],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 14, IARG_UINT64, types[14], IARG_ADDRINT, typeNames[14],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 15, IARG_UINT64, types[15], IARG_ADDRINT, typeNames[15],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 16, IARG_UINT64, types[16], IARG_ADDRINT, typeNames[16],
                IARG_END);

            /* we only really need to instrument the return if we are in a safebox
             * scenario */
            if (KnobInstrumentRetCB) {
                /* instrument return block of function to intercept return value */
                RTN_InsertCall(libFuncRtn, IPOINT_AFTER, (AFUNPTR)LibFuncAfter,
                    IARG_THREAD_ID, /* thread id */
                    /* name of the function */
                    IARG_ADDRINT, libName,
                    /* yes this is a callback return */
                    IARG_BOOL, true,
                    /* reference to the return value */
                    IARG_FUNCRET_EXITPOINT_REFERENCE,
                    /* type of the return value */
                    IARG_ADDRINT, returnType, IARG_END);
            }

            RTN_Close(libFuncRtn);
        }

        /* read next order */
        op = readOpcode(monitorFIFOfd);
        errnoBak = errno;
    }

    while (op == ConfFuzzOpcode::MONITOR_WRITEARG_ORDER) {
        /* retrieve argument number */
        uint64_t num = 0;
        uint64_t newval = 0;

        int nread = read(monitorFIFOfd, &num, sizeof(num));
        num++; /* we start with 1 here */

        if (nread != sizeof(num)) {
            perror("read failed");
            cerr << cerrPrefix << "Failed to read from monitor FIFO, got " << nread
                 << " bytes, expected " << sizeof(num) << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        if (num > argc) {
            cerr << cerrPrefix << "Monitor is sending garbage" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        nread = read(monitorFIFOfd, &newval, sizeof(newval));

        if (nread != sizeof(newval)) {
            perror("read failed");
            cerr << cerrPrefix << "Failed to read from monitor FIFO, got " << nread
                 << " bytes, expected " << sizeof(num) << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

#define WRITE_ARG(n)  do { if (num == n) * arg ## n = newval; } while (0)
#define PRINT2_ARG_F(n)                                                \
       do {                                                            \
               if (num == n)                                           \
                       TraceFile << "{" << arg ## n ## TypeName        \
                                 << "}" << " from " << * arg ## n;     \
       } while (0)
#define PRINT2_ARG(n)                                                  \
       do {                                                            \
               if (num == n) TraceFile << * arg ## n;                  \
       } while (0)

        TraceFile << "~> change arg #" << num << " ";
        PRINT2_ARG_F(1); PRINT2_ARG_F(2);  PRINT2_ARG_F(3);  PRINT2_ARG_F(4);
        PRINT2_ARG_F(5); PRINT2_ARG_F(6);  PRINT2_ARG_F(7);  PRINT2_ARG_F(8);
        PRINT2_ARG_F(9); PRINT2_ARG_F(10); PRINT2_ARG_F(11); PRINT2_ARG_F(12);
        PRINT2_ARG_F(13); PRINT2_ARG_F(14); PRINT2_ARG_F(15); PRINT2_ARG_F(16);
        PRINT2_ARG_F(17);

        /* perform write */
        WRITE_ARG(1); WRITE_ARG(2);  WRITE_ARG(3); WRITE_ARG(4);
        WRITE_ARG(5); WRITE_ARG(6);  WRITE_ARG(7); WRITE_ARG(8);
        WRITE_ARG(9); WRITE_ARG(10); WRITE_ARG(11); WRITE_ARG(12);
        WRITE_ARG(13); WRITE_ARG(14); WRITE_ARG(15); WRITE_ARG(16);
        WRITE_ARG(17);

        TraceFile << " to ";
        PRINT2_ARG(1); PRINT2_ARG(2);  PRINT2_ARG(3);  PRINT2_ARG(4);
        PRINT2_ARG(5); PRINT2_ARG(6);  PRINT2_ARG(7);  PRINT2_ARG(8);
        PRINT2_ARG(9); PRINT2_ARG(10); PRINT2_ARG(11); PRINT2_ARG(12);
        PRINT2_ARG(13); PRINT2_ARG(14); PRINT2_ARG(15); PRINT2_ARG(16);
        PRINT2_ARG(17);
        TraceFile << endl;

        /* read next order */
        op = readOpcode(monitorFIFOfd);
        errnoBak = errno;
    }

    PIN_MutexUnlock(&monitorLock);

    /* Wait for execution ack */
    if (op == ConfFuzzOpcode::INVALID_OPCODE) {
        cerr << cerrPrefix << "Fatal, error while reading from monitor FIFO, errno: " << errnoBak << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    } else if (op != ConfFuzzOpcode::MONITOR_EXEC_ACK) {
        cerr << cerrPrefix << "Fatal, monitor seemed to ACK with garbage" << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }
}

/* ===================================================================== */
/* Instrumentation routines                                              */
/* ===================================================================== */

VOID SyscallInterceptor(THREADID t, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v)
{
    ADDRINT s = PIN_GetSyscallNumber(ctxt, std);

    // std::cout << "Intercepted system call no. " << s << endl;

    /* Detect when the application closes the instrumentation's file descriptors */
    if (s == SYS_close) {
        int fd = static_cast<int>(PIN_GetSyscallArgument(ctxt, std, 0));
        if (fd == monitorFIFOfd || fd == workerFIFOfd) {
            cerr << cerrPrefix << "Application tries to close() the "
                 << "instrumentation's file descriptors!" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }
    }
}

/* threadStart / threadFini: handle creation and destruction of the reentrance
 * beacon and callback state indicator */
VOID threadStart(THREADID TID, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    int *i = new int;
    *i = 0;
    if (PIN_SetThreadData(reentranceIndex, i, TID) == FALSE)
    {
        cerr << cerrPrefix << "PIN_SetThreadData failed" << endl;
        PIN_ExitProcess(1);
    }

    std::vector<int> *b, *p;

    b = new std::vector<int>;
    b->push_back(0);
    if (PIN_SetThreadData(libraryBeacon, b, TID) == FALSE)
    {
        cerr << cerrPrefix << "PIN_SetThreadData failed" << endl;
        PIN_ExitProcess(1);
    }

    p = new std::vector<int>;
    p->push_back(0);
    if (PIN_SetThreadData(appBeacon, p, TID) == FALSE)
    {
        cerr << cerrPrefix << "PIN_SetThreadData failed" << endl;
        PIN_ExitProcess(1);
    }
}

VOID threadFini(THREADID TID, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    delete static_cast<int*>(PIN_GetThreadData(reentranceIndex, TID));
    delete static_cast<std::vector<int>*>(PIN_GetThreadData(libraryBeacon, TID));
    delete static_cast<std::vector<int>*>(PIN_GetThreadData(appBeacon, TID));
}

VOID Image(IMG img, VOID* v)
{
    string symbolsFilePath = string(KnobLibrarySymbols.Value().c_str());
    if(symbolsFilePath.length() == 0)
    {
        cerr << cerrPrefix << "No list of symbols provided, please pass -symbols or "
                "-help for help." << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    string typesFilePath = string(KnobLibraryTypes.Value().c_str());
    if (typesFilePath.length() == 0)
    {
        cerr << cerrPrefix << "No path to type information provided, please pass "
                "-typesPath or -help for help." << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    /* populate type map */
    std::ifstream typesFile(typesFilePath.c_str());
    for (string line; getline(typesFile, line);)
    {
        std::string typeName = line.substr(0, line.find(' '));
        if (!typeName.length()) {
             cerr << cerrPrefix << "Malformed types file." << endl;
             exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        normalizeType(typeName);
        auto sizePos = nthOccurrence(line, " ", 1);

        if (sizePos == -1) {
            unknownTypeSizeList.push_back(typeName);
        } else {
            int s = atoi(line.substr(sizePos + 1, string::npos).c_str());
            typeMap.insert({typeName, s});
        }
    }

    /* Install system call interceptor */
    // PIN_AddSyscallEntryFunction(SyscallInterceptor, 0);

    /* Go through each symbol in the symbol file.
     * Each line in the symbol file looks like the following:
     *   symbolname symbollinkagename argc arg1type arg2type ... argNtype
     * with N = argc */
    std::ifstream symbolsFile(symbolsFilePath.c_str());
    for (string line; getline(symbolsFile, line);)
    {
        /* grab symbol name */
        auto _libName = line.substr(0, line.find(' '));

        /* grab symbol linkage name */
        auto _libLinkageNamePos = nthOccurrence(line, " ", 1);
        auto _funcArgcPos = nthOccurrence(line, " ", 2);
        auto _retTypePos = nthOccurrence(line, " ", 3);
        if (_libLinkageNamePos == -1 || _funcArgcPos == -1 || _retTypePos == -1) {
            cerr << cerrPrefix << "Malformed symbol file." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        auto _libLinkageName = line.substr(_libLinkageNamePos + 1,
                                           _funcArgcPos - _libLinkageNamePos - 1);

        /* grab argc */
        int funcArgc = atoi(line.substr(_funcArgcPos + 1,
                            _retTypePos - _funcArgcPos - 1).c_str());

        if (funcArgc > LIBAPI_ARG_UPPER_LIMIT) {
            cerr << cerrPrefix << "Function " << _libName << " ("
                 << _libLinkageName
                 << ") reaches argument upper limit of "
                 << LIBAPI_ARG_UPPER_LIMIT << ", this is an easy fix, "
                 << "contact maintainers." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* grab return value type */
        std::string _returnType;

        auto _libArg1TypePos = nthOccurrence(line, " ", 4);
        if (_libArg1TypePos == -1) {
            if (funcArgc != 0) {
                cerr << cerrPrefix << "Malformed symbol file." << endl;
                exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
            }

            _returnType = line.substr(_retTypePos + 1);
        } else {
            _returnType = line.substr(_retTypePos + 1,
            _libArg1TypePos - _retTypePos - 1);
        }

        if (_returnType.size() == 0) {
            cerr << cerrPrefix << "Malformed callback symbol file (size of "
                 << "return value type)." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        if (_returnType == "*")
            _returnType = "void*";

        /* grab argument sizes */
        UINT64 types[LIBAPI_ARG_UPPER_LIMIT];
        char  *typeNames[LIBAPI_ARG_UPPER_LIMIT];
        if (_libArg1TypePos != -1) {
            parseTypeList(line.substr(_libArg1TypePos), funcArgc, types, typeNames);
        }

        /* instrument the function */
        RTN libFuncRtn = RTN_FindByName(img, _libLinkageName.c_str());

        /* we have to do this for scope issues; don't care about freeing, this
         * should live as long as the program is up */
        char *mangledLibName = (char *) malloc(strlen(_libLinkageName.c_str()) + 1);
        strcpy(mangledLibName, _libLinkageName.c_str());
        char *libName = (char *) malloc(strlen(_libName.c_str()) + 1);
        strcpy(libName, _libName.c_str());
        char *returnType = (char *) malloc(strlen(_returnType.c_str()) + 1);
        strcpy(returnType, _returnType.c_str());

        if (RTN_Valid(libFuncRtn))
        {
            RTN_Open(libFuncRtn);

            /* instrument entry block of function */
            RTN_InsertCall(libFuncRtn, IPOINT_BEFORE, (AFUNPTR)LibFuncBefore,
                IARG_THREAD_ID, /* thread id */
                /* mangled name of the function, unmangled name of the function */
                IARG_ADDRINT, mangledLibName, IARG_ADDRINT, libName,
                /* call site, this is not a callback */
                IARG_RETURN_IP, IARG_BOOL, false,
                /* argument count */
                IARG_UINT32, funcArgc,
                /* arguments and their sizes; only valid depending on funcArgc */
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 0,  IARG_UINT64, types[0],  IARG_ADDRINT, typeNames[0],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 1,  IARG_UINT64, types[1],  IARG_ADDRINT, typeNames[1],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 2,  IARG_UINT64, types[2],  IARG_ADDRINT, typeNames[2],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 3,  IARG_UINT64, types[3],  IARG_ADDRINT, typeNames[3],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 4,  IARG_UINT64, types[4],  IARG_ADDRINT, typeNames[4],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 5,  IARG_UINT64, types[5],  IARG_ADDRINT, typeNames[5],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 6,  IARG_UINT64, types[6],  IARG_ADDRINT, typeNames[6],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 7,  IARG_UINT64, types[7],  IARG_ADDRINT, typeNames[7],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 8,  IARG_UINT64, types[8],  IARG_ADDRINT, typeNames[8],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 9,  IARG_UINT64, types[9],  IARG_ADDRINT, typeNames[9],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 10, IARG_UINT64, types[10], IARG_ADDRINT, typeNames[10],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 11, IARG_UINT64, types[11], IARG_ADDRINT, typeNames[11],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 12, IARG_UINT64, types[12], IARG_ADDRINT, typeNames[12],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 13, IARG_UINT64, types[13], IARG_ADDRINT, typeNames[13],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 14, IARG_UINT64, types[14], IARG_ADDRINT, typeNames[14],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 15, IARG_UINT64, types[15], IARG_ADDRINT, typeNames[15],
                IARG_FUNCARG_ENTRYPOINT_REFERENCE, 16, IARG_UINT64, types[16], IARG_ADDRINT, typeNames[16],
                IARG_END);

            /* instrument return block of function to intercept return value */
            RTN_InsertCall(libFuncRtn, IPOINT_AFTER, (AFUNPTR)LibFuncAfter,
                IARG_THREAD_ID, /* thread id */
                /* name of the function */
                IARG_ADDRINT, libName,
                /* this is not a callback return */
                IARG_BOOL, false,
                /* reference to the return value */
                IARG_FUNCRET_EXITPOINT_REFERENCE,
                /* type of the return value */
                IARG_ADDRINT, returnType, IARG_END);

            RTN_Close(libFuncRtn);
        }
    }
}

VOID Fini(INT32 code, VOID* v)
{
    TraceFile.close();
    if (KnobNoMonitor) {
        exit(1);
    }
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << cerrPrefix << "This tool is the underlying instrumentation of ConfFuzz."
         << endl << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    /* initialize pin & symbol manager */
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    /* sanitize arguments */
    string outputFilePath = string(KnobOutputFile.Value().c_str());
    if(outputFilePath.length() == 0)
    {
        cerr << cerrPrefix << "No output file path provided, please pass -o or "
                "-help for help." << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    string symbolToolFilePath = string(KnobSymbolTool.Value().c_str());
    if(symbolToolFilePath.length() == 0)
    {
        cerr << cerrPrefix << "No path to the symbol tool provided, "
             << "please pass -symboltool or -help for help." << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    if (!KnobNoMonitor) {
        string fifoMonitorFilePath = string(KnobFifoMonitor.Value().c_str());
        if(fifoMonitorFilePath.length() == 0)
        {
            cerr << cerrPrefix << "No fifo path for the monitor provided, "
                 << "please pass -fifoMonitor or -help for help." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        string fifoWorkerFilePath = string(KnobFifoWorker.Value().c_str());
        if(fifoWorkerFilePath.length() == 0)
        {
            cerr << cerrPrefix << "No fifo path for the worker provided, "
                 << "please pass -fifoWorker or -help for help." << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* tell the monitor we're up and running */
        auto e = ConfFuzzOpcode::WORKER_UP;
        workerFIFOfd = open(fifoWorkerFilePath.c_str(), O_WRONLY | O_NONBLOCK);
        if (workerFIFOfd <= 0) {
            cerr << cerrPrefix << "Failed to open worker FIFO (code "
                 << errno << ")" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        auto r = write(workerFIFOfd, &e, sizeof(enum ConfFuzzOpcode));

        if (r == -1 || (size_t) r < sizeof(enum ConfFuzzOpcode)) {
            cerr << cerrPrefix << "Failed to write to worker FIFO" << endl;
            if (r == -1)
                cerr << cerrPrefix << "Reason: system error, errno " << errno << endl;
            else
                cerr << cerrPrefix << "Reason: incomplete write" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        /* wait for the monitor's answer... */
        monitorFIFOfd = open(fifoMonitorFilePath.c_str(), O_RDONLY);
        if (monitorFIFOfd <= 0) {
            cerr << cerrPrefix << "Failed to open monitor FIFO (code "
                 << errno << ")" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }

        int nread = read(monitorFIFOfd, &e, sizeof(enum ConfFuzzOpcode));
        if (nread != sizeof(enum ConfFuzzOpcode) ||
                    !isInstanceOf(&e, ConfFuzzOpcode::MONITOR_UP_ACK)) {
            cerr << cerrPrefix << "Fatal, monitor seemed to ACK with garbage" << endl;
            exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
        }
    }

    /* allocate TLS key for reentrance beacons */
    reentranceIndex = PIN_CreateThreadDataKey(NULL);
    libraryBeacon = PIN_CreateThreadDataKey(NULL);
    appBeacon = PIN_CreateThreadDataKey(NULL);

    /* register callbacks for initialization / destruction of the beacon */
    PIN_AddThreadStartFunction(threadStart, NULL);
    PIN_AddThreadFiniFunction(threadFini, NULL);

    /* register Image to be called to instrument functions */
    IMG_AddInstrumentFunction(Image, 0);
    PIN_AddFiniFunction(Fini, 0);

    remove(SAFEWRITE_FIFO_PATH);

    std::ostringstream oss;
    oss << "mknod " << SAFEWRITE_FIFO_PATH << " p";
    FILE *FP = popen(oss.str().c_str(), "r");

    if (!FP) {
        cerr << cerrPrefix << "Failed to mknod, errno: " << errno << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    int rPclose = pclose(FP); /* clean way would be to use mknod() but it seems to conflict with Pin */

    if (rPclose == -1) {
        cerr << cerrPrefix << "Failed to pclose mknod, errno: " << errno << endl;
        /* should be non fatal? */
    }

    safewriteFIFOfd = open(SAFEWRITE_FIFO_PATH, O_RDWR);
    if (safewriteFIFOfd <= 0) {
        cerr << cerrPrefix << "Failed to open safe writer FIFO (code "
             << errno << ")" << endl;
        exit(CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE);
    }

    /* write to a file since cout and cerr maybe closed by the application */
    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << hex;
    TraceFile.setf(ios::showbase);

    TraceFile << "# format:" << endl;
    TraceFile << "#   [function name](arguments) @ [call site]" << endl;
    TraceFile << "#   ~> [action taken]" << endl << endl;

    /* never returns */
    PIN_StartProgram();

    return 0;
}
