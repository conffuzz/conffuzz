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

#include <random> /* std::default_random_engine */
#include <iostream> /* cout, cerr */
#include <fstream> /* ifstream */
#include <filesystem> /* path */
#include <unistd.h> /* readlink */
#include <linux/limits.h> /* PATH_MAX */
#include <spawn.h> /* posix_spawn */
#include <sys/wait.h> /* waitpid */
#include <cstdlib> /* system */
#include <sys/stat.h> /* mkfifo */
#include <fcntl.h> /* open */
#include <signal.h> /* signal */
#include <regex> /* std::regex */
#include <mutex> /* std::mutex */
#include <map> /* std::map */
#include <set> /* std::set */
#include <atomic> /* std::atomic */
#include <climits> /* INT_MAX */
#include <list> /* std::list */
#include <sys/personality.h> /* personality() */
#include <cstring>

#include "include/conffuzz.h"
#include "include/monitor.h"

using std::cout;
using std::cerr;
using std::endl;

/* ==========================================================================
 * Low level configuration options & constants
 * ========================================================================== */

static const int REPRO_MAX_RETRIES = 30;
/* minimize should be higher, as we already know that the bug *can* be
 * reproduced. Let's try to go for
 *     MINIMIZE_MAX_RETRIES_FACTOR * (n# of attempts to reproduce)
 */
static const int MINIMIZE_MAX_RETRIES_FACTOR = 3;

static std::string coutPrefix = "[+] ";
static std::string _cerrPrefix = "[E] ";
#define cerrPrefix _cerrPrefix << "{" << __FILE__ << ":" << __LINE__ << "} "

static int WORKER_TIMEOUT = 30; /* in seconds */
static bool USE_SE_V2 = false;

/* return values for reproduction/minimization related procedures */

/* we managed to reproduce this run */
#define REPRODUCE_SUCCESS         0
/* we didn't manage to reproduce this run due to a system error */
#define REPRODUCE_ERROR          -1
#define REPRODUCE_CRIT_ERROR     -2
/* we didn't manage to reproduce this run */
#define REPRODUCE_FAILURE        -3
/* this run is definitely not reproducible */
#define REPRODUCE_UNREPRODUCIBLE -4

/* ==========================================================================
 * File name magic values
 * ========================================================================== */

#define INSTRUMENTATION_NAME     "instrumentation.so"
#define SYMBOL_EXTRACTER_NAME    "interface-extracter.sh"
#define SYMBOL_EXTRACTER_NAME_V2 "interface-extracter-v2.sh"
#define SYMBOL_ANALYZER_NAME     "find-symbol-from-mappings.sh"
#define TYPE_ANALYZER_NAME       "analyze-type-wrapper.sh"
#define TYPE_ALL_ANALYZER_NAME   "analyze-all-types.sh"
#define STATIC_ANALYZER_NAME     "static-analyze-entry-points.py"

#define SYMBOLS_FILE_PATH      "/tmp/conffuzz_functions.txt"
#define TYPES_FILE_PATH        "/tmp/conffuzz_types.txt"

/* TODO in the future we need to adapt quite a bit of this if we want to
 * concurrently fuzz multiple instances of an application. */

#define MONITOR_FIFO_PATH      "/tmp/conffuzz_monitor.fifo"
#define WORKER_FIFO_PATH       "/tmp/conffuzz_worker.fifo"

/* for now, worker output is redirected to this file for logging purposes AND
 * to collect ASan crash reports. */
#define WORKER_OUTPUT_PATH     "/tmp/conffuzz_child_out.txt"
#define WORKER_OUTPUT_PATH_OLD "/tmp/conffuzz_child_out.txt.old"

#define WORKER_FUZZING_SEQ_LOG "/tmp/conffuzz_child_fuzzseq.txt"

/* monitor scratch file, just something we use to write temporary stuff */
#define MONITOR_SCRATCH_FILE   "/tmp/monitor_scratch"

/* ==========================================================================
 * Global state
 * ========================================================================== */

bool SAFEBOX_MODE_ENABLED = false;

/* enforce certain ASan options */
char ASAN_ENV[] = "ASAN_OPTIONS=detect_leaks=0 detect_odr_violation=0";

bool DEBUG_ENABLED = false;
bool HEAVY_DEBUG_ENABLED = false;
bool COLOURING_ENABLED = true;
bool STATICALLY_COUNT_EPOINT_ENABLED = false;
bool FPOSITIVE_MINIMIZATION_ENABLED = false;
bool ONLY_EXTRACT_API_ENABLED = false;

/* "sticky" list of environment variables/application args that we use
 * for all runs */
extern char **environ; /* workload scripts */
char **appEnvp = NULL; /* application */
char **appArgv;

std::filesystem::path selfPath;
std::filesystem::path workloadScriptPath;
std::filesystem::path symbolExtracterPath;
std::filesystem::path symbolExtracter2Path;
std::filesystem::path symbolAnalyzerPath;
std::filesystem::path typeAnalyzerPath;
std::filesystem::path typeAllAnalyzerPath;
std::filesystem::path staticAnalyzerPath;
std::filesystem::path instrumentationPath;
std::list<std::filesystem::path> libraryPaths;
std::list<std::filesystem::path> analysisLibraryPaths;
std::filesystem::path symbolsPath;
std::filesystem::path typesPath;
std::filesystem::path appPath;
std::filesystem::path pinPath;
std::filesystem::path fifoMonitorPath;
std::filesystem::path fifoWorkerPath;
std::filesystem::path fuzzingLogPath;
std::filesystem::path baseCrashPath;
std::filesystem::path existingAPIFilePath;
std::filesystem::path existingTypesFilePath;

/* Random engines and seed */
time_t seed = 0;

std::default_random_engine rand_engine;
std::uniform_real_distribution<> uniform_zero_to_one(0.0, 1.0);

/* regex that describes the text mappings entry for the target library */
std::regex libTextMappingsRegex;

/* how many apps started, how many unique crashes detected */
int appCounter, crashCounter;

/* how many times we tried to reproduce/minimize the current crash */
int rpCounter;

/* ==========================================================================
 * Worker and workload script state
 * ========================================================================== */

/* set that contains all known interface call sites */
std::set<uint64_t> knownCallSites;
long unsigned int maxKnownCallSitesSize = 0;

std::set<std::string> criticalAPIelements;
std::set<std::string> observedAPIelements;

/* set that contains all instrumented callbacks */
std::set<uint64_t> instrumentedCallbacks;

/* FD of currently open worker and monitor FIFOs */
int workerFIFOfd, monitorFIFOfd;

/* PID of the worker.
 *
 * We need to record this information to allow the SIGCHLD handler to
 * differenciate handling between worker, workload scripts, and system()
 * calls we are absolutely not interested in. */
pid_t workerPid;

std::atomic<int> worker_si_code = 0;
std::atomic<int> worker_si_status = 0;

/* map that contains stack traces from all crashes discovered; used
 * for deduplication */
std::map<int, std::string> stackTraceMap;

/* map that contains impact information for all crashes discovered */
std::map<int, std::list<std::string>> impactMap;

/* Unfortunately we cannot use this information to make sense of IP values
 * in the binary (e.g., get line numbers) because the rewritings of Intel
 * Pin mess everything up. But we can use to determine what is a pointer. */
uintptr_t workerStartMapping, workerHeapMapping, workerStackMapping;

std::map<std::string, std::pair<uintptr_t,uintptr_t>> workerTextMappings;
std::map<std::string, std::pair<uintptr_t,uintptr_t>> stdlibMappings;
std::map<std::string, std::pair<uintptr_t,uintptr_t>> libMappings;

/* ==========================================================================
 * Fuzzing strategy state
 * ========================================================================== */

/* Note: these interesting values are partly populated at runtime */
/* interesting pointer values */
std::set<uintptr_t> interestingPointers = {0x0};
/* interesting integer values */
std::set<int64_t> interestingIntegers = {
        /* low values */
        -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0,
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        /* values more interesting if we're facing an offset */
        -10000, -3000, -1000, -100, 100, 1000, 3000, 10000,
        /* usual limits */
        SCHAR_MIN, CHAR_MIN, SHRT_MIN, INT_MIN, LONG_MIN, LLONG_MIN,
        CHAR_BIT, SCHAR_MAX, UCHAR_MAX, CHAR_MAX, MB_LEN_MAX, SHRT_MAX,
        USHRT_MAX, INT_MAX, UINT_MAX, LONG_MAX
};

std::map<uint64_t /* pointer */, uint64_t /* object size */> currentPointers;

/* If we don't find crashes for a while (runsSinceLastUniqueCrash >
 * fuzzingStrategyBumpThreshhold), we are probably fuzzing too shallow.
 *
 * In this case, adapt mutation probabilities to fuzz 'deeper'. Do this by
 * increasing probabilityTurningPoint.
 *
 * How does this work? At each API call, we increment mutationsCounter. When
 * fuzzing, if mutationsCounter < probabilityTurningPoint, mutate with 'low'
 * probability, otherwise with 'high' probability.
 */
static const int fuzzingStrategyBumpThreshhold = 45;
int runsSinceLastUniqueCrash = 0;
int probabilityTurningPoint = 0;

/* Note: mutationsCounter is not accessed concurrently */
int mutationsCounter = 0;

/* low = mutate 10% of the time */
static const double lowMutationProbability = 0.10;

/* high = mutate 60% of the time */
static const double highMutationProbability = 0.60;

/* replay storage data structure */
ConfFuzzCorpus corpus;
ConfFuzzCorpus referenceCorpus;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

void cleanupCorpus()
{
        corpus = ConfFuzzCorpus();
}

/* probability between 0.0 and 1.0 */
bool random_bool_with_prob(double prob)
{
        return uniform_zero_to_one(rand_engine) >= (1 - prob);
}

/* return a natural number in [low, high] */
int random_int_range(int low, int high)
{
        std::uniform_int_distribution<> dist(low, high);
        return dist(rand_engine);
}

std::ostream& bold_on(std::ostream& os)
{
        if (!COLOURING_ENABLED)
            return os << "";
        return os << "\e[1m";
}

std::ostream& colouring_off(std::ostream& os)
{
        if (!COLOURING_ENABLED)
            return os << "";
        return os << "\e[0m";
}

std::ostream& red_on(std::ostream& os)
{
        if (!COLOURING_ENABLED)
            return os << "";
        return os << "\x1b[1;31m";
}

std::ostream& green_on(std::ostream& os)
{
        if (!COLOURING_ENABLED)
            return os << "";
        return os << "\x1b[1;32m";
}

std::ostream& brown_on(std::ostream& os)
{
        if (!COLOURING_ENABLED)
            return os << "";
        return os << "\x1b[1;33m";
}

std::ostream& blue_on(std::ostream& os)
{
        if (!COLOURING_ENABLED)
            return os << "";
        return os << "\x1b[1;34m";
}

std::ostream& purple_on(std::ostream& os)
{
        if (!COLOURING_ENABLED)
            return os << "";
        return os << "\x1b[1;35m";
}

std::string matchFileWithLimit(const char *logFile, std::regex STRegex, int limit)
{
        std::ostringstream stackTrace;
        std::smatch sm;

        FILE* fp = fopen(logFile, "r");
        if (fp == NULL)
            return stackTrace.str();

        char* line = NULL;
        size_t len = 0;
        int i = 0, read;

        while ((read = getline(&line, &len, fp)) != -1 && (limit == -1 || i < limit)) {
            std::string str(line, line + read);
            regex_search(str, sm, STRegex);
            if (!sm.empty()) {
                stackTrace << str;
            }
            i++;
        }

        fclose(fp);
        if (line)
            free(line);
        return stackTrace.str();
}

std::string matchFile(const char *logFile, std::regex STRegex)
{
        return matchFileWithLimit(logFile, STRegex, -1);
}

void killWorker(int pid)
{
        kill(pid, SIGKILL);
}

/* Copy file from `from` to `to`, entirely overwriting `to` if it already
 * exists. */
void copyFile(const std::filesystem::path& from, const std::filesystem::path& to)
{
        if (from == to) {
            return;
        }

        /* note the loop; we tend to have "Interrupted system call"
         * errors with this copy_file, simply retry three times in case it fails */
        for (int i = 0; i < 3 /* max tries */; i++) {
            try {
                std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
                break;
            } catch (std::filesystem::filesystem_error& e) {
            }
        }
}

/* ==========================================================================
 * Corpus logging overrides
 * ========================================================================== */

enum ConfFuzzOpcode readOpcodeWithTimeout(int fd, int timeout /* in seconds */)
{
        auto e = _readOpcodeWithTimeout(fd, timeout);
        /* input vector empty for now, will be populated while processing event */
        ConfFuzzMsg m = std::make_pair(e, std::vector<char>());

        std::list<ConfFuzzMsg> l;

        /* create an entry for this action */
        corpus.push_back(std::make_pair(m, l));

        return e;
}

int _writeToFIFO(int fd, enum ConfFuzzOpcode e, void *buf, long int len)
{
        /* build action */
        std::vector<char> v(static_cast<char*>(buf),
                            static_cast<char*>(buf) + len);
        ConfFuzzMsg a = make_pair(e, v);

        /* log this action into the store */
        corpus.back().second.push_back(a);

        return performWrite(fd, buf, len);
}

/* ==========================================================================
 * AS Mapping routines
 * ========================================================================== */

/* textMappingsChanged: tells if mappings changed since our last copy.
 * Probably terribly slow. */
bool textMappingsChanged(std::string procMappings, bool existed)
{
        bool changed = true;

        if (existed) {
            remove(WORKER_MAPPINGS_COPY_PATH_OLD);
            std::filesystem::rename(WORKER_MAPPINGS_COPY_PATH,
                                    WORKER_MAPPINGS_COPY_PATH_OLD);
        }

        /* copy first in case the worker dies during this function */
        std::ifstream src(procMappings, std::ios::binary);
        std::ofstream dst(MONITOR_SCRATCH_FILE,
                          std::ios::binary);
        dst << src.rdbuf();

        if (existed && std::filesystem::file_size(MONITOR_SCRATCH_FILE) == 0) {
            /* this new mappings file looks empty, ignore it */
            copyFile(WORKER_MAPPINGS_COPY_PATH_OLD,
                     WORKER_MAPPINGS_COPY_PATH);
            return false;
        }

        std::filesystem::rename(MONITOR_SCRATCH_FILE,
                                WORKER_MAPPINGS_COPY_PATH);

        if (!existed) {
            return changed;
        }

        std::ostringstream ss;
        /* Note the r-xp here. We're not interested in changes that do not
         * affect executable mappings. */
        ss << "diff " << WORKER_MAPPINGS_COPY_PATH_OLD << " " << WORKER_MAPPINGS_COPY_PATH
           << "| grep \"r.xp\" > " << MONITOR_SCRATCH_FILE;
        system(ss.str().c_str());

        std::ifstream in_file2(MONITOR_SCRATCH_FILE, std::ifstream::binary);
        in_file2.seekg(0, std::ifstream::end);
        if (!in_file2.tellg()) {
            changed = false;
        }

        return changed;
}

bool isPointerValue(uint64_t value)
{
        /* terribly simply heuristic, might not always work */
        return value > workerStartMapping ? true : false;
}

bool isLibPointerValue(uint64_t value)
{
        for (auto itr = libMappings.begin(); itr != libMappings.end(); itr++)
        {
            if (value > itr->second.first && value < itr->second.second)
                return true;
        }
        return false;
}

bool isStdlibPointerValue(uint64_t value)
{
        for (auto itr = stdlibMappings.begin(); itr != stdlibMappings.end(); itr++)
        {
            if (value > itr->second.first && value < itr->second.second)
                return true;
        }
        return false;
}

bool isCodePointerValue(uint64_t value)
{
        for (auto itr = workerTextMappings.begin(); itr != workerTextMappings.end(); itr++)
        {
            if (value > itr->second.first && value < itr->second.second)
                return true;
        }
        return false;
}

std::string getLibNameForPointer(uint64_t value)
{
        for (auto itr = workerTextMappings.begin(); itr != workerTextMappings.end(); itr++)
        {
            if (value > itr->second.first && value < itr->second.second)
                return itr->first;
        }
        return "";
}

bool isNonCodePointerValue(uint64_t value)
{
        return isPointerValue(value) && !isCodePointerValue(value);
}

std::map<std::string, std::pair<uintptr_t,uintptr_t>>
_determineMappingWithBlocker(const std::regex regex, int limit)
{
        std::map<std::string, std::pair<uintptr_t,uintptr_t>> result;

        FILE* fp = fopen(WORKER_MAPPINGS_COPY_PATH, "r");
        if (fp == NULL)
            return result;

        char* line = NULL;
        size_t len = 0;
        int i = 0, read;

        static const std::regex AddrRegex(R"~(^([0-9a-z]+)-([0-9a-z]+).* (.+)$)~");
        std::ssub_match addrMatch;
        std::smatch sm;

        /* first match with given regex, then grab addresses and library path
         * with our address regex */
        while ((read = getline(&line, &len, fp)) != -1 && (limit == -1 || i < limit)) {
            uintptr_t mb = 0, me = 0;

            std::string str(line, line + read);
            regex_search(str, sm, regex);

            if (!sm.empty()) {
                str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
                regex_search(str, sm, AddrRegex);

                addrMatch = sm[1];
                std::stringstream ss;
                ss << std::hex << addrMatch.str();
                ss >> mb;

                addrMatch = sm[2];
                std::stringstream ss2;
                ss2 << std::hex << addrMatch.str();
                ss2 >> me;

                addrMatch = sm[3];
                std::string libPath = addrMatch.str();

                /* if there is no library path (can happen), we will get an integer;
                 * since an integer cannot be a valid library path, simply check for
                 * integers and handle accordingly */
                bool isNotPath = false;
                try {
                    volatile int converted [[maybe_unused]] = stoi(libPath);
                } catch (std::invalid_argument const&) {
                    result.insert({libPath, std::make_pair(mb, me)});
                    isNotPath = true;
                }

                if (!isNotPath) {
                    /* that was not a library path, simply go for "" */
                    result.insert({"", std::make_pair(mb, me)});
                }

                i++;
            }
        }

        fclose(fp);
        if (line)
            free(line);

        return result;
}

std::map<std::string, std::pair<uintptr_t,uintptr_t>>
_determineMapping(const std::regex regex)
{
        return _determineMappingWithBlocker(regex, -1);
}

std::pair<uintptr_t,uintptr_t>
getSmallestMapping(std::map<std::string, std::pair<uintptr_t,uintptr_t>> m)
{
        std::pair<uintptr_t,uintptr_t> ret = std::make_pair(0, 0);
        for (auto itr = m.begin(); itr != m.end(); itr++)
        {
            if (itr->second.first < ret.first || ret.first == 0) {
                ret = itr->second;
            }
        }

        return ret;
}

/* determineWorkerMappings: determine worker mappings. Returns true if something
 * changed. */
bool determineWorkerMappings(pid_t pid)
{
        bool existed = false;

        if (std::filesystem::exists(WORKER_MAPPINGS_COPY_PATH)) {
           existed = true;
        }

        std::ostringstream r;
        r << "/proc/" << pid << "/maps";

        if(!textMappingsChanged(r.str(), existed)) {
           /* no need to recompute anything */
           return false;
        }

        bool success = true;

        static const std::regex startMappingsRegex = std::regex(R"~(r--p 0+ .*)~");
        auto workerStartMappings = _determineMappingWithBlocker(startMappingsRegex, 1);
        if (workerStartMappings.empty()) {
            success = false;
        } else {
            workerStartMapping = getSmallestMapping(workerStartMappings).first;
        }

        static const std::regex heapMappingsRegex = std::regex(R"~(\[heap\])~");
        auto workerHeapMappings = _determineMappingWithBlocker(heapMappingsRegex, 1);
        if (workerHeapMappings.size() == 1) {
            workerHeapMapping = workerHeapMappings["[heap]"].first;
        }

        static const std::regex stackMappingsRegex = std::regex(R"~(\[stack\])~");
        auto workerStackMappings = _determineMappingWithBlocker(stackMappingsRegex, 1);
        if (workerStackMappings.size() != 1) {
            success = false;
        } else {
            workerStackMapping = workerStackMappings["[stack]"].first;
        }

        static const std::regex textMappingsRegex = std::regex(R"~(r.xp .*)~");
        auto workerTextMappingsTmp = _determineMapping(textMappingsRegex);
        if (workerTextMappingsTmp.empty()) {
            success = false;
        } else {
            workerTextMappings = workerTextMappingsTmp;
        }

        auto libMappingsList = _determineMapping(libTextMappingsRegex);
        if (libMappingsList.size() != libraryPaths.size()) {
            if (!existed)
                libMappings = libMappingsList;
            success = false;
        } else {
            libMappings = libMappingsList;
        }

        /* this is a rough list of libs that should be considered "standard" in the sense that
         * a crash there doesn't really allow us to determine whether the crash is a false
         * positive or not */
        static const std::regex stdlibMappingsRegex =
                std::regex(R"~(r.xp .*((libc(-.*)?\.so)|(libgobject(-.*)?\.so)|(libstdc++)|(libgcc)|(libasan)|(libpthread)))~");
        auto stdlibMappingsTmp = _determineMapping(stdlibMappingsRegex);
        if (!stdlibMappingsTmp.empty()) {
            stdlibMappings = stdlibMappingsTmp;
        }

        if (!existed && DEBUG_ENABLED) {
            cout << coutPrefix << "Determined worker start mapping: "
                 << (void *) workerStartMapping << endl;
            cout << coutPrefix << "Determined worker heap mapping: "
                 << (void *) workerHeapMapping << endl;
            cout << coutPrefix << "Determined worker stack mapping: "
                 << (void *) workerStackMapping << endl;
            cout << coutPrefix << "Instrumented library mappings:" << endl;
            for (auto itr = libMappings.begin();
                      itr != libMappings.end(); itr++)
            {
                cout << coutPrefix << "   " << (void*) itr->second.first << " - "
                     << (void *) itr->second.second << " " << itr->first << endl;
            }
            cout << coutPrefix << "Text mappings : " << endl;
            for (auto itr = workerTextMappings.begin();
                      itr != workerTextMappings.end(); itr++)
            {
                cout << coutPrefix << "   " << (void*) itr->second.first << " - "
                     << (void *) itr->second.second << " " << itr->first << endl;
            }

            if (STATICALLY_COUNT_EPOINT_ENABLED) {
                cout << coutPrefix << "Statically determining API call sites... "
                     << "This can be very slow." << endl;

                int nbCallSites = 0, nbCallerComponents = 0, nbEndPoints = 0;
                for (auto itr = workerTextMappings.begin();
                          itr != workerTextMappings.end(); itr++)
                {
                        /* only consider those with debug symbols */
                        std::stringstream ss;
                        ss << "file $(readlink -f " << itr->first
                           << ") | grep \"with debug_info\"" << "> " << MONITOR_SCRATCH_FILE;
                        system(ss.str().c_str());
                        ss.str("");

                        std::ifstream in_file4(MONITOR_SCRATCH_FILE, std::ifstream::binary);
                        in_file4.seekg(0, std::ifstream::end);
                        if (!in_file4.tellg()) {
                            /* no debug symbols, don't analyze */
                            continue;
                        }

                        if (itr->first.find("asan") != std::string::npos) {
                            /* this one is obviously a special one */
                            continue;
                        }

                        if (libMappings.find(itr->first) != libMappings.end()) {
                            /* also ignore instrumented libs... */
                            continue;
                        }

                        int n = 0, m = 0;
                        if (DEBUG_ENABLED)
                            cout << coutPrefix << "  " << itr->first << std::flush;

                        ss << "LD_PRELOAD=/usr/lib/gcc/x86_64-linux-gnu/11/libasan.so "
                           << staticAnalyzerPath.c_str() << " -f " << SYMBOLS_FILE_PATH
                           << " -b " << itr->first << " 2> /dev/null >"
                           << MONITOR_SCRATCH_FILE;
                        system(ss.str().c_str());
                        ss.str("");

                        std::ifstream f(MONITOR_SCRATCH_FILE);
                        f >> n;
                        f >> m;

                        if (DEBUG_ENABLED)
                            cout << ": " << n << " (endpoints: " << m << ")" << endl;

                        if (n)
                            nbCallerComponents++;

                        nbCallSites += n;
                        nbEndPoints += m;
                }

                std::filesystem::path infoPath = baseCrashPath;
                infoPath += "/crashes/session_info.txt";

                std::ofstream ofs(infoPath.c_str(), std::ios_base::app);
                ofs << "Statically detected call sites: " << nbCallSites << endl;
                ofs << "Statically detected called API endpoints: " << nbEndPoints << endl;
                ofs << "Statically detected caller components: " << nbCallerComponents << endl;

                cout << coutPrefix << "Done! " << nbCallSites
                     << " entry points detected (" << nbCallerComponents
                     << " caller components)." << endl;
            }
        }

        if (!existed) {
            if (!success) {
                cerr << cerrPrefix << "Failed to parse worker mappings for worker "
                     << pid << "! " << "Look up in the logs, they're invalid." << endl;
                cerr << cerrPrefix << "Copy of the mappings in "
                     << WORKER_MAPPINGS_COPY_PATH << endl;
                exit(EXIT_FAILURE);
            }

            /* populate interesting values a bit more */
            interestingPointers.insert(workerStartMapping);
            interestingPointers.insert(workerStartMapping + 10);
            interestingPointers.insert(workerStartMapping + 200);
            interestingPointers.insert(workerStartMapping + 400);

            auto workerTextMapping = getSmallestMapping(workerTextMappings).first;
            interestingPointers.insert(workerTextMapping);
            interestingPointers.insert(workerTextMapping + 10);
            interestingPointers.insert(workerTextMapping + 200);
            interestingPointers.insert(workerTextMapping + 400);

            interestingPointers.insert(workerStackMapping);
            interestingPointers.insert(workerStackMapping + 10);
            interestingPointers.insert(workerStackMapping + 200);
            interestingPointers.insert(workerStackMapping + 400);

            if (workerHeapMapping) {
                interestingPointers.insert(workerHeapMapping);
                interestingPointers.insert(workerHeapMapping + 10);
                interestingPointers.insert(workerHeapMapping + 200);
                interestingPointers.insert(workerHeapMapping + 400);
            }
        }

        return true;
}

/* ==========================================================================
 * Worker / Workload script spawning routines
 * ========================================================================== */

pid_t spawnWorkloadScript(void)
{
        pid_t pid;
        int status;

        char *argV[] = {const_cast<char*>(workloadScriptPath.c_str()), NULL};

        /* redirect all workload script output to /dev/null */
        posix_spawn_file_actions_t action;
        posix_spawn_file_actions_init(&action);
        posix_spawn_file_actions_addopen(&action, 1, "/dev/null", O_WRONLY|O_APPEND, 0);
        posix_spawn_file_actions_adddup2(&action, 1, 2);

        status = posix_spawn(&pid, argV[0], &action, NULL, argV, environ);
        posix_spawn_file_actions_destroy(&action);

        if (status != 0) {
            cerr << cerrPrefix << "Failed to spawn workload script." << endl;
            return -1;
        }

        return pid;
}

pid_t spawnApp(void)
{
        pid_t pid;
        int status;

        /* capture output of children to get ASan reports */
        posix_spawn_file_actions_t action;
        posix_spawn_file_actions_init(&action);
        posix_spawn_file_actions_addopen(&action, 1, WORKER_OUTPUT_PATH,
                                                     O_CREAT|O_WRONLY|O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&action, 1, 2);

        if (appEnvp == NULL) {
            /* first time we're starting an app, populate our custom envp */
            int i = 1;
            /* environ is a NULL-terminated list of char* */
            while (environ[i]) { i++; }
            appEnvp = (char **) malloc((1 + i) * sizeof(char*));
            appEnvp[0] = &ASAN_ENV[0];
            for (int j = 1; j <= i; j++) { appEnvp[j] = environ[j]; }
        }

        status = posix_spawn(&pid, appArgv[0], &action, NULL, appArgv, appEnvp);
        posix_spawn_file_actions_destroy(&action);

        if (status != 0) {
            cerr << cerrPrefix << "Failed to spawn application." << endl;
            return -1;
        }

        return pid;
}

/* ==========================================================================
 * Crash handling routines
 * ========================================================================== */

bool isInstrumentationCrash(const char *logFile)
{
        static const std::regex r(R"~(Tool \(or Pin\) caused signal 11)~");
        return !matchFile(logFile, r).empty();
}

std::string getASanStackTrace(const char *logFile)
{
        static const std::regex STRegex(R"~(^\s+.\d+ 0x)~");
        return matchFile(logFile, STRegex);
}

bool isASanCrash(const char *logFile)
{
        /* a variety of heuristics to detect ASan crashes */
        static const std::regex r1(R"~(AddressSanitizer:DEADLYSIGNAL)~");
        static const std::regex r2(R"~(ERROR: AddressSanitizer:)~");
        std::string stackTrace = getASanStackTrace(logFile);
        return !matchFile(logFile, r1).empty() || !matchFile(logFile, r2).empty()
                                               || !stackTrace.empty();
}

/* isFalsePositive: go through the stack trace, grab addresses of
 * the X first frames (X given by the caller), and walk up. Ignore
 * frames from standard libraries. If the first non-stdlib frame found
 * is from the attacker, then it's a false positive, otherwise
 * it isn't ("the attacker" being defined by the mode we're running in).
 */
bool isFalsePositive(const char *logFile, int limit, std::string &location)
{
        bool found = false;

        static const std::regex AddrRegex(R"~(^\s+.\d+ 0x([0-9a-z]+))~");
        static const std::regex STRegex(R"~(^\s+.\d+ 0x)~");

        std::istringstream f(matchFile(logFile, STRegex));
        std::string line;
        std::smatch sm;

        int i = 0;
        while(std::getline(f, line) && (limit == -1 || i < limit)) {
            uintptr_t addr;

            regex_search(line, sm, AddrRegex);

            std::ssub_match addrMatch = sm[1];
            std::stringstream ss;
            ss << std::hex << addrMatch.str();
            ss >> addr;

            location = getLibNameForPointer(addr);

            if (SAFEBOX_MODE_ENABLED) {
                if (isLibPointerValue(addr)) {
                    /* this is a 100% in the lib, it is a legit crash */
                    break;
                } else if (!isStdlibPointerValue(addr)) {
                    /* this is a not in the lib, it is a false positive */
                    found = true;
                    break;
                } else {
                    /* well, in this case we don't know, continue to walk up */
                }
            } else /* sandbox mode */ {
                if (isLibPointerValue(addr)) {
                    /* this is a 100% in the lib, it must be a false positive */
                    found = true;
                    break;
                } else if (!isStdlibPointerValue(addr)) {
                    /* this is a not in the lib, it cannot be a false positive */
                    break;
                } else {
                    /* well, in this case we don't know, continue to walk up */
                }
            }

            i++;
        }

        if (!i) {
            /* this is a no-stacktrace crash, usually due to PC corruption */
            /* use the last information that we had: were we "in the library",
             * from the perspective of the fuzzer state? */

            ConfFuzzOpcode lastEvent;
            /* grab the last worker event, skip potential invalid opcodes at
             * the end */
            for (auto itEv =  corpus.crbegin();
                      itEv != corpus.crend(); ++itEv) {
                lastEvent = getEventOpcode(*itEv);
                if (lastEvent != ConfFuzzOpcode::INVALID_OPCODE) {
                    break;
                }
            }

            if (SAFEBOX_MODE_ENABLED) {
                if (lastEvent == ConfFuzzOpcode::WORKER_LIBRARY_RETURN ||
                    lastEvent == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL ||
                    lastEvent == ConfFuzzOpcode::WORKER_CALLBACK_RETURN ||
                    lastEvent == ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL) {
                    found = true;
                }
            } else {
                if (lastEvent == ConfFuzzOpcode::WORKER_LIBRARY_CALL ||
                    lastEvent == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL ||
                    lastEvent == ConfFuzzOpcode::WORKER_CALLBACK_RETURN ||
                    lastEvent == ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL) {
                    found = true;
                }
            }
        }

        return found;
}

void _registerImpact(int crashID, std::string impact, bool arbitrary,
                     std::filesystem::path infoPath)
{
        if (impactMap.find(crashID) == impactMap.end())
            impactMap[crashID] = std::list<std::string>();

        if (arbitrary)
            impact += "_arbitrary";

        bool found = (std::find(impactMap[crashID].begin(),
                                impactMap[crashID].end(), impact) !=
                                impactMap[crashID].end());

        if (!found) {
            impactMap[crashID].push_back(impact);
            std::ofstream ofs(infoPath.c_str(), std::ios_base::app);
            ofs << impact << endl;
        }
}

void addImpactAttributes(int crashID, const char *logFile,
    std::filesystem::path infoPath, std::string stackTrace)
{
        bool capREAD = false, capWRITE = false, capEXEC = false, capNULL = false, capALLOC_CORRUPTION = false;
        bool arbitrary = false;

        bool foundNegativeSize = false;

        FILE* fp = fopen(logFile, "r");
        if (fp == NULL)
            return;

        char* str = NULL;
        size_t len = 0;
        int read;

        if (stackTrace.empty())
            capEXEC = true;

        while ((read = getline(&str, &len, fp)) != -1) {
            std::string line(str, str + read);

            /* markers used in later analysis */
            if (line.find("negative-size-param") != std::string::npos) {
                foundNegativeSize = true;
            }

            /* READ markers */
            if (line.find("caused by a READ memory access") != std::string::npos) {
                capREAD = true;
            }
            /* e.g., READ of size 4 at 0x5555555ce0c8 thread T0 */
            if (line.find("READ of size") != std::string::npos) {
                capREAD = true;
            }
            /* WRITE markers */
            if (line.find("__interceptor_memcpy") != std::string::npos) {
                if (foundNegativeSize) {
                    capWRITE = true;
                }
            }
            if (line.find("stack-overflow on address") != std::string::npos) {
                capWRITE = true;
            }
            if (line.find("caused by a WRITE memory access") != std::string::npos) {
                capWRITE = true;
            }
            if (line.find("WRITE of size") != std::string::npos) {
                capWRITE = true;
            }
            /* EXEC markers */
            if (line.find("pc points to the zero page") != std::string::npos) {
                capEXEC = true;
            }
            if (line.find("Hint: PC is at a non-executable region") != std::string::npos) {
                capEXEC = true;
            }
            /* Allocator markers */
            if (line.find("AddressSanitizer: requested allocation size") != std::string::npos) {
                capALLOC_CORRUPTION = true;
            }
            if (line.find("attempting free on address which was not malloc()-ed") != std::string::npos) {
                capALLOC_CORRUPTION = true;
            }
            /* NULL markers */
            if (line.find("address points to the zero page") != std::string::npos) {
                capNULL = true;
            }
            if (line.find("on unknown address 0x000000000000 ") != std::string::npos) {
                capNULL = true;
            }
            /* arbitrary impact markers */
            if (line.find("caused by a dereference of a high value address") != std::string::npos) {
                arbitrary = true;
            }
        }

        fclose(fp);
        if (str)
            free(str);

        if (DEBUG_ENABLED)
            cout << coutPrefix << "Vulnerability has ";

        if (arbitrary && DEBUG_ENABLED)
            cout << bold_on << "arbitrary " << colouring_off;

        if (capEXEC) {
            if (DEBUG_ENABLED)
                cout << bold_on << "execution" << colouring_off;
            _registerImpact(crashID, "cap_exec", arbitrary, infoPath);
        } else if (capALLOC_CORRUPTION) {
            if (DEBUG_ENABLED)
                cout << bold_on << "allocator corruption" << colouring_off;
            _registerImpact(crashID, "cap_corrupt_allocator", arbitrary, infoPath);
        } else if (capNULL) {
            if (DEBUG_ENABLED)
                cout << bold_on << "NULL dereference" << colouring_off;
            _registerImpact(crashID, "cap_null_deref", arbitrary, infoPath);
        } else if (capWRITE) {
            if (DEBUG_ENABLED)
                cout << bold_on << "write" << colouring_off;
            _registerImpact(crashID, "cap_write", arbitrary, infoPath);
        } else if (capREAD) {
            if (DEBUG_ENABLED)
                cout << bold_on << "read" << colouring_off;
            _registerImpact(crashID, "cap_read", arbitrary, infoPath);
        } else {
            if (DEBUG_ENABLED)
                cout << bold_on << "???" << colouring_off;
        }

        if (DEBUG_ENABLED)
            cout << " capabilities." << endl;
}

std::string removeAddresses(std::string stackTrace)
{
        static const std::regex STRegex(R"~(0x[0-9a-f]+)~");
        return std::regex_replace(stackTrace, STRegex, "0xaddr");
}

/* sometimes, ASan outputs multiple traces: the first one is the proper
 * stack trace, and the following ones represent the allocation traces
 * for the memory that has been touched. We only want the first one. */
std::string keepOnlyFirstTrace(std::string stackTrace)
{
        std::string tmp = stackTrace;
        /* remove first line of tmp, contains #0 ... */
        tmp.erase(0, tmp.find("\n") + 1);
        /* get rid of additional stack traces */
        tmp = tmp.substr(0, tmp.find("#0 ", 0));
        /* re-add first line */
        tmp = stackTrace.substr(0, stackTrace.find("\n", 0)) + "\n" + tmp;
        /* remove trailing white spaces */
        tmp = std::regex_replace(tmp, std::regex(" +$"), "");
        /* remove blank lines */
        tmp.erase(std::unique(tmp.begin(), tmp.end(),
                  [] (char a, char b) {return a == '\n' && b == '\n';}), tmp.end());
        return tmp;
}

/* ASan detectors tend to vary depending on the faulty address, causing
 * different stack traces for the same issue. Simply ignore them. */
std::string removeIgnoredLibs(std::string stackTrace)
{
        static const std::regex AddrRegex(R"~(^\s+.\d+ 0x([0-9a-z]+))~");

        std::string line, ret;
        std::smatch sm;

        std::istringstream stream(stackTrace);
        while(std::getline(stream, line)) {
            uintptr_t addr;

            regex_search(line, sm, AddrRegex);

            std::ssub_match addrMatch = sm[1];
            std::stringstream ss;
            ss << std::hex << addrMatch.str();
            ss >> addr;

            if (getLibNameForPointer(addr).find("asan") == std::string::npos) {
                ret += line + "\n";
            }
        }

        return ret;
}

std::string normalizeTrace(std::string stackTrace)
{
        return removeAddresses(removeIgnoredLibs(keepOnlyFirstTrace(stackTrace)));
}

/* handleCrash: return true if the crash was new and non-false positive */
bool handleCrash(bool isASanCrash)
{
        std::string stackTrace, normalizedST;
        bool duplicate = false, falsePositive = false, firstTruePositiveEmptyST = false;
        std::string faultLocation;
        int crashID = -1;

        std::filesystem::path crashPath, runPath;
        crashPath += baseCrashPath;
        crashPath += "/crashes/";

        if (isASanCrash) {
            /* extract stack trace */
            stackTrace = getASanStackTrace(WORKER_OUTPUT_PATH);
            normalizedST = normalizeTrace(stackTrace);

            /* is this a duplicate? */
            for (auto it = stackTraceMap.begin(); it != stackTraceMap.end(); ++it)
                if (std::get<1>(*it) == normalizedST)
                    crashID = std::get<0>(*it);

            if (crashID != -1) {
                duplicate = true;
            } else {
                crashID = crashCounter;
            }

            /* is this a false positive? */
            falsePositive = isFalsePositive(WORKER_OUTPUT_PATH, -1, faultLocation);
        } else {
            /* No deduplication or check for false positives unfortunately,
             * because no stack trace */
            crashID = crashCounter;
            crashCounter += 1;
        }

        if (!falsePositive && isASanCrash) {
            crashPath += "bugs/";
        } else if (!falsePositive) {
            crashPath += "bugs-non-ASan/";
        } else {
            crashPath += "false-positives/";
        }

        std::ostringstream ss;
        ss << "crash" << crashID << "/";
        crashPath += ss.str();

        if (!std::filesystem::is_directory(crashPath) ||
            !std::filesystem::exists(crashPath)) {
            std::filesystem::create_directories(crashPath);

            if (stackTrace.empty() && !falsePositive && isASanCrash && duplicate) {
                /* very special case: we have two no-stack-trace crashes following
                 * each other, and the first one was a false positive */
                firstTruePositiveEmptyST = true;
            }
        }

        std::filesystem::path infoLogPath = crashPath;
        infoLogPath += "/crash_info.txt";

        if ((!duplicate && isASanCrash) || firstTruePositiveEmptyST) {
            /* save stack trace (even though it's already in the app log) */
            std::filesystem::path traceLogPath = crashPath;
            traceLogPath += "/crash_trace.txt";
            std::ofstream ofs(traceLogPath.c_str(), std::ios_base::binary);
            ofs << normalizedST;

            /* save additional crash information */
            if (faultLocation.empty())
                faultLocation = "unknown (wild jump?)";
            std::ofstream ofs2(infoLogPath.c_str(), std::ios_base::binary);
            ofs2 << "fault_location " << faultLocation << endl;
        }

        if (isASanCrash) {
            addImpactAttributes(crashID, WORKER_OUTPUT_PATH, infoLogPath, stackTrace);
        }

        /* create run folder */
        runPath += crashPath;
        ss.str("");
        ss << "run" << appCounter << "/";
        runPath += ss.str();
        if (!std::filesystem::is_directory(runPath) ||
            !std::filesystem::exists(runPath)) {
            std::filesystem::create_directory(runPath);
        }

        /* save fuzzing sequence */
        std::filesystem::path inputLogPath = runPath;
        inputLogPath += "/input.log";

        /* not a mv since these two might not be on the same device */
        copyFile(WORKER_FUZZING_SEQ_LOG, inputLogPath.c_str());
        remove(WORKER_FUZZING_SEQ_LOG);

        /* backup application logs */
        std::filesystem::path appLogPath = runPath;
        appLogPath += "/app.log";

        copyFile(WORKER_OUTPUT_PATH, appLogPath.c_str());

        /* backup application mappings */
        std::filesystem::path appMappingsPath = runPath;
        appMappingsPath += "/mappings.txt";

        copyFile(WORKER_MAPPINGS_COPY_PATH, appMappingsPath.c_str());

        /* TODO move this block earlier in the function */
        if (!duplicate || firstTruePositiveEmptyST) {
            if (isASanCrash && !duplicate) {
                /* remember this new crash now */
                stackTraceMap.insert(std::pair<int,std::string>(crashID, normalizedST));
            }

            if (!falsePositive) {
                cout << coutPrefix << blue_on << "New interesting crash (non-duplicate, "
                     << "non-false-positive) detected." << colouring_off << endl;
            } else if (DEBUG_ENABLED) {
                cout << coutPrefix << purple_on << "New false positive detected."
                     << colouring_off << endl;
            }

            if (!firstTruePositiveEmptyST) {
                crashCounter += 1;
            }

            if (!falsePositive && isASanCrash) {
                /* don't reset for non-asan crashes, because we cannot deduplicate them */
                runsSinceLastUniqueCrash = 0;

		if (HEAVY_DEBUG_ENABLED)
                    cout << coutPrefix << blue_on << "Resetting crash window."
                         << colouring_off << endl;

                /* exactly in this case we would like to reproduce and minimize */
                return true;
            }
        } else {
            runsSinceLastUniqueCrash++;

            if (runsSinceLastUniqueCrash > fuzzingStrategyBumpThreshhold) {
                probabilityTurningPoint++;
                runsSinceLastUniqueCrash = 0;
                cout << coutPrefix << brown_on << "No non-duplicate crash in "
                     << fuzzingStrategyBumpThreshhold << " runs. Adapt strategy ["
                     << probabilityTurningPoint << "]." << colouring_off << endl;
            }
        }

        if (!duplicate && falsePositive && isASanCrash && FPOSITIVE_MINIMIZATION_ENABLED) {
            /* we want to reproduce + minimize */
            return true;
        }

        return false;
}

std::filesystem::path getCrashPath(int crashID)
{
        std::filesystem::path crashPath;
        crashPath += baseCrashPath;
        crashPath += "/crashes/bugs/";

        std::ostringstream ss;
        ss << "crash" << crashID << "/";
        crashPath += ss.str();

        if (!std::filesystem::is_directory(crashPath) ||
            !std::filesystem::exists(crashPath)) {
            if (FPOSITIVE_MINIMIZATION_ENABLED) {
                crashPath = baseCrashPath;
                crashPath += "/crashes/false-positives/";
                crashPath += ss.str();
            }
        }

        return crashPath;
}

int handleCrashReplay(void)
{
        std::string stackTrace, normalizedST;
        int crashID = -1;

        /* extract stack trace */
        stackTrace = getASanStackTrace(WORKER_OUTPUT_PATH);
        normalizedST = normalizeTrace(stackTrace);

        /* is it the right one? */
        for (auto it = stackTraceMap.begin(); it != stackTraceMap.end(); ++it)
            if (std::get<1>(*it) == normalizedST)
                crashID = std::get<0>(*it);

        /* note: we refer to crashCounter - 1, as crashCounter itself
         * corresponds to the next, not yet found crash */
        if (crashID != crashCounter - 1) {
            /* not the right one */
            cout << coutPrefix << "Unsuccessful run, didn't get the right crash (";
            if (crashID == -1)
                cout << "unknown";
            else
                cout << crashID;
            cout << " != " << crashCounter - 1 << ")." << endl;
            return REPRODUCE_UNREPRODUCIBLE;
        }

        /* ok we're good, keep track of it */
        std::filesystem::path crashPath, runPath;
        crashPath = getCrashPath(crashID);

        /* create run folder */
        std::ostringstream ss;
        ss << "rp" << rpCounter << "/";
        crashPath += ss.str();
        if (!std::filesystem::is_directory(crashPath) ||
            !std::filesystem::exists(crashPath)) {
            std::filesystem::create_directory(crashPath);
        }

        /* save fuzzing sequence */
        std::filesystem::path inputLogPath = crashPath;
        inputLogPath += "/input.log";

        /* not a mv since these two might not be on the same device */
        copyFile(WORKER_FUZZING_SEQ_LOG, inputLogPath.c_str());
        remove(WORKER_FUZZING_SEQ_LOG);

        /* backup application logs */
        std::filesystem::path appLogPath = crashPath;
        appLogPath += "/app.log";

        copyFile(WORKER_OUTPUT_PATH, appLogPath.c_str());

        /* backup application mappings */
        std::filesystem::path appMappingsPath = crashPath;
        appMappingsPath += "/mappings.txt";

        copyFile(WORKER_MAPPINGS_COPY_PATH, appMappingsPath.c_str());

        return REPRODUCE_SUCCESS;
}

void markNonReproducible(int crashID)
{
        std::filesystem::path infoLogPath = getCrashPath(crashID);
        infoLogPath += "/crash_info.txt";
        std::ofstream ofs(infoLogPath.c_str(), std::ios_base::app);
        ofs << "non_reproducible" << endl;
}

void makeMinimalFolderFromRUN(int crashID, int runID)
{
        std::filesystem::path crashPath = getCrashPath(crashID);
        std::filesystem::path minimalPath = crashPath;

        std::ostringstream ss;
        ss << "run" << runID << "/";
        crashPath += ss.str();

        minimalPath += "minimal/";

        std::filesystem::copy(crashPath, minimalPath,
                              std::filesystem::copy_options::recursive);
}

void makeMinimalFolderFromRP(int crashID, int playID)
{
        std::filesystem::path crashPath = getCrashPath(crashID);
        std::filesystem::path minimalPath = crashPath;

        std::ostringstream ss;
        ss << "rp" << playID << "/";
        crashPath += ss.str();

        minimalPath += "minimal/";

        std::filesystem::rename(crashPath, minimalPath);
}

void cleanReproRuns(int crashID)
{
        std::ostringstream ss;
        ss << "rm -rf " << getCrashPath(crashID).c_str() << "/rp*";
        system(ss.str().c_str());
}

static inline void debugBackup(void)
{
        if (DEBUG_ENABLED) {
            /* keep a copy for debugging purposes */
            std::filesystem::remove(WORKER_OUTPUT_PATH_OLD);
            copyFile(WORKER_OUTPUT_PATH, WORKER_OUTPUT_PATH_OLD);
        }

        if (HEAVY_DEBUG_ENABLED) {
            /* output fuzzer logs in every case. It's sometimes really interesting
             * to see what the fuzzer did when it did *not* result in a crash. */
            cout << coutPrefix << "Fuzzer logs for this run:" << endl;

            std::string line;
            std::ifstream infile(WORKER_FUZZING_SEQ_LOG);
            while (getline(infile, line))
            {
                cout << "    " << line << endl;
            }
        }
}

/* handleTermination: analyze application exit status, return true if the crash
 * was new and non-false-positive. */
bool handleTermination(pid_t pid)
{
        bool isSIGSEVCrash = false, ASanCrash = false;
        bool instCrash = false, instBug= false;

        /* check for an ASan crash */
        if (worker_si_code == CLD_EXITED) {
            if (isASanCrash(WORKER_OUTPUT_PATH))
                    ASanCrash = true;
        }

        /* check for an instrumentation crash */
        if (worker_si_code == CLD_KILLED && isInstrumentationCrash(WORKER_OUTPUT_PATH))
            instCrash = true;

        cout << coutPrefix << "Death of worker " << pid << " detected (";
        if (worker_si_code == CLD_EXITED && !ASanCrash ) {
            cout << "exited, code " << worker_si_status;
            if (worker_si_status == CONFFUZZ_INSTRUMENTATION_ERREXIT_CODE) {
                cout << " = instrumentation bug";
                instBug = true;
            }
        } else if (ASanCrash) {
            cout << red_on << "ASan crash" << colouring_off;
        } else if (instCrash) {
            cout << "instrumentation crash";
        } else if (worker_si_code == CLD_KILLED && worker_si_status == SIGSEGV) {
            cout << "killed, SIGSEGV";
            isSIGSEVCrash = true;
        } else if (worker_si_code == CLD_KILLED) {
            cout << "killed, signal " << worker_si_status;
        } else if (worker_si_code == CLD_DUMPED) {
            cout << "dumped/crashed";
        } else {
            cout << "unknown?";
        }
        cout << ")" << endl;

        if (instBug && HEAVY_DEBUG_ENABLED) {
            cerr << cerrPrefix << "Instrumentation bug detected, aborting." << endl;
            exit(EXIT_FAILURE);
        }

        debugBackup();

        bool ret = false;
        if (ASanCrash) {
            ret = handleCrash(true);
        } else if (!instCrash && isSIGSEVCrash) {
            ret = handleCrash(false);
        }

        return ret;
}

int handleTerminationReplay(pid_t pid)
{
        int ret = REPRODUCE_SUCCESS;

        if (!isASanCrash(WORKER_OUTPUT_PATH)) {
            cout << coutPrefix << "Unsuccessful run, didn't get an ASan crash." << endl;
            ret = REPRODUCE_UNREPRODUCIBLE;
        }

        debugBackup();

        if (ret == REPRODUCE_SUCCESS)
            return handleCrashReplay();

        return ret;
}

/* ==========================================================================
 * Signal handlers
 * ========================================================================== */

void endSession(void)
{
    std::filesystem::path infoPath = baseCrashPath;
    infoPath += "/crashes/session_info.txt";

    if (std::filesystem::exists(infoPath)) {
        time_t t = time(NULL);
        struct tm* ptm = localtime(&t);
        char cur_time[128];

        std::ofstream ofs(infoPath.c_str(), std::ios_base::app);

        ofs << "Max number of call sites reached in a run: "
            << maxKnownCallSitesSize << endl;

        ofs << "Number of API endpoints reached: "
            << observedAPIelements.size() << endl;

        ofs << "Number of API endpoints that are vulnerability vectors: "
            << criticalAPIelements.size() << endl;

        if (!criticalAPIelements.empty()) {
            ofs << "List of these endpoints:" << endl;

            for (auto itr = criticalAPIelements.begin();
                 itr != criticalAPIelements.end(); itr++)
            {
                ofs << "  [api] " << *itr << endl;
            }
        }

        strftime(cur_time, 128, "%Y-%m-%d %H:%M:%S", ptm);
        ofs << "Ending time: " << cur_time << endl;
    }
}

void SIGINThandler(int signum)
{
    /* This function is not handler safe but should be fine
     * since this is SIGINT */
    endSession();

    /* TODO kill children */

    quick_exit(0);
}

void SIGCHLDhandler(int sig, siginfo_t *info, void *ucontext)
{
        /* Note here: SA_NOCLDWAIT is set, so no need to wait() on children to
         * reap them. */

        if (info->si_code == CLD_TRAPPED || info->si_code == CLD_STOPPED ||
            info->si_code == CLD_CONTINUED) {
            /* ignore these for now (they shouldn't come because of
             * SA_NOCLDSTOP though */
            return;
        }

        if (info->si_code == CLD_KILLED && info->si_status == SIGINT) {
            /* ignore these, they have probably been propagated from the monitor */
            return;
        }

        if (workerPid == info->si_pid) {
            worker_si_code = info->si_code;
            worker_si_status = info->si_status;
        }
}

/* ==========================================================================
 * Core fuzzing / mutation helpers
 * ========================================================================== */

void cleanupAppFuzzingState(void)
{
        mutationsCounter = 0;

        /* clear the set; we will have to reinstrument these when
         * we restart the application */
        instrumentedCallbacks.clear();

        if (knownCallSites.size() > maxKnownCallSitesSize)
            maxKnownCallSitesSize = knownCallSites.size();
        knownCallSites.clear();
}

/* tells you whether or not to attempt a mutation */
bool proceedWithMutation(void)
{
        /* determine mutation probability */
        double prob = highMutationProbability;

        if (mutationsCounter < probabilityTurningPoint) {
            /* we are not yet deep enough */
            prob = lowMutationProbability;
        }

        /* to mutate or not mutate? */
        if (random_bool_with_prob(1 - prob)) {
            /* don't mutate in this case */
            return false;
        }

        mutationsCounter++;
        return true;
}

uint64_t mutateValue(uint64_t value)
{
        uint64_t ret = value;

        if (!proceedWithMutation()) {
            /* don't mutate in this case */
            return ret;
        }

	/* we want to make sure not to mutate to the same thing */
	while (ret == value) {
            if (random_bool_with_prob(0.5)) {
                /* if we change, do a simple mutation half of the time */
                int mut = random_int_range(-1000, 1000);
                ret = value + mut;
            } else if (isPointerValue(value)) {
                /* otherwise return one of the "interesting" pointers/ints */
                auto it = interestingPointers.cbegin();
                int random = random_int_range(0, interestingIntegers.size() - 1);
                std::advance(it, random);
                ret = (uint64_t) *it;
            } else {
                auto it = interestingIntegers.cbegin();
                int random = random_int_range(0, interestingIntegers.size() - 1);
                std::advance(it, random);
                ret = (uint64_t) *it;
            }
	}

        return ret;
}

void handleInvalidOpcode(int errno_backup)
{
        int status;

        sleep(1); /* small sleep to let the child die properly */

        pid_t w = waitpid(workerPid, &status, WNOHANG);

        if (w == -1) {
            /* worker definitely died... */
            return;
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            /* worker probably died because of a crash, it's ok to stop fuzzing */
            return;
        }

        /* ok, worker didn't crash, something else happened? */
        cerr << cerrPrefix << "Read from worker " << workerPid << " FIFO failed unexpectedly "
             << "(code " << errno_backup << ")" << endl;

        return;
}

int readArguments(bool isCallback, bool mutate, bool print)
{
        uint64_t argCount = 0;

        /* 1. Read number of arguments */
        int nrw = read(workerFIFOfd, &argCount, sizeof(argCount));
        if (nrw != sizeof(argCount)) {
            cerr << cerrPrefix << "Reading call site from worker FIFO failed unexpectedly "
                 << "(code " << errno << ")" << endl;
            return 1;
        }

        if (argCount > LIBAPI_ARG_UPPER_LIMIT) {
            cerr << cerrPrefix << "Read garbage argc from worker FIFO" << endl;
            return 1;
        }

        for (unsigned int i = 0; i < argCount; i++) {
            /* 2. Read argument size */
            uint64_t argumentSize = 0;
            nrw = read(workerFIFOfd, &argumentSize, sizeof(argumentSize));
            if (nrw != sizeof(argumentSize)) {
                cerr << cerrPrefix << "Reading call site from worker FIFO failed "
                     << "unexpectedly (code " << errno << ")" << endl;
                return 1;
            }

            /* 3. Read argument value */
            uint64_t argument = 0;
            nrw = read(workerFIFOfd, &argument, sizeof(argument));
            if (nrw != sizeof(argument)) {
                cerr << cerrPrefix << "Reading call site from worker FIFO failed "
                     << "unexpectedly (code " << errno << ")" << endl;
                return 1;
            }

            /* 4. instrument potential callbacks */
            if (!isCallback && isCodePointerValue(argument) &&
                 instrumentedCallbacks.find(argument) == instrumentedCallbacks.end()) {
                if (writeToFIFO(monitorFIFOfd,
                        ConfFuzzOpcode::MONITOR_INSTRUMENT_ORDER,
                        1 /* address to instrument */, argument) > WRITETOFIFO_SUCCESS) {
                    cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly "
                         << "(code " << errno << ")" << endl;
                    return 1;
                }

                if ((DEBUG_ENABLED && print) || HEAVY_DEBUG_ENABLED)
                    cout << coutPrefix << "Instrumenting callback " << (void*) argument << endl;
                instrumentedCallbacks.insert(argument);
            }

            /* 5. Mutate argument if asked to */
            if (mutate) {
                uint64_t newval = mutateValue(argument);
                if (newval != argument) {
                    if (writeToFIFO(monitorFIFOfd,
                            ConfFuzzOpcode::MONITOR_WRITEARG_ORDER, 2,
                            i /* arg to modify */, newval /* value */) > WRITETOFIFO_SUCCESS) {
                        cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly "
                             << "(code " << errno << ")" << endl;
                        return 1;
                    } else if (DEBUG_ENABLED) {
                        if (isCallback) {
                            cout << coutPrefix << "Messing with callback arguments of worker ";
                        } else {
                            cout << coutPrefix << "Messing with library call arguments of worker ";
                        }
                        cout << workerPid << ", arg #" << i << " (" << (void*) argument
                             << " -> " << (void*) newval << ")" << endl;
                    }
                }
            }
        }

        return 0;
}

int readFuncName(void)
{
        uint64_t funcNameSize = 0;

        /* 1. Read function name size */
        int nrw = read(workerFIFOfd, &funcNameSize, sizeof(funcNameSize));
        if (nrw != sizeof(funcNameSize)) {
            cerr << cerrPrefix << "Reading function name size from worker FIFO failed unexpectedly "
                 << "(code " << errno << ")" << endl;
            return 1;
        }

        /* 2. Read function name */
        char fName[funcNameSize + 1];
        fName[funcNameSize] = 0;

        nrw = read(workerFIFOfd, fName, funcNameSize);
        if (nrw != (int) funcNameSize) {
            cerr << cerrPrefix << "Reading function name from worker FIFO failed "
                 << "unexpectedly (code " << errno << ")" << endl;
            return 1;
        }

        /* keep function name in the current replay entry
	 * NOTE: we don't need a NULL terminated string here. */
        corpus.back().first.second = std::vector<char>(
            static_cast<char*>(fName), static_cast<char*>(fName) + funcNameSize);

        observedAPIelements.insert(std::string(fName));

        return 0;
}

bool appHandshake(bool print)
{
        enum ConfFuzzOpcode op = readOpcode(workerFIFOfd);

        if (op == ConfFuzzOpcode::WORKER_UP) {
            op = ConfFuzzOpcode::MONITOR_UP_ACK;
            int nrw = write(monitorFIFOfd, &op, sizeof(enum ConfFuzzOpcode));
            if (nrw != sizeof(enum ConfFuzzOpcode)) {
                cerr << cerrPrefix << "Failed handshake, couldn't send monitor ACK." << endl;
                return false;
            } else if (print) {
                cout << coutPrefix << "Communication established with worker " << workerPid << endl;
            }
        } else {
            if (print) {
                cerr << cerrPrefix << "Failed handshake, worker " << workerPid
                     << " sent garbage [" << op << "]" << endl;
            }
            return false;
        }

        return true;
}

/* ==========================================================================
 * Crash reproduction / minimization routines
 * ========================================================================== */

static inline bool eventsMatch(ConfFuzzEvent e)
{
    if (getEventOpcode(e) != getEventOpcode(corpus.back())) {
        if (HEAVY_DEBUG_ENABLED) {
            cout << coutPrefix << getEventOpcode(e) << " != " << getEventOpcode(corpus.back()) << endl;
        }
        return false;
    }

    if (getEventOpcode(e) == ConfFuzzOpcode::WORKER_LIBRARY_CALL ||
        getEventOpcode(e) == ConfFuzzOpcode::WORKER_CALLBACK_CALL) {
        if (e.first != corpus.back().first) {
            if (HEAVY_DEBUG_ENABLED) {
                std::string s1 = std::string(e.first.second.begin(), e.first.second.end());
                std::string s2 = std::string(corpus.back().first.second.begin(), corpus.back().first.second.end());
                cout << coutPrefix << s1 << " != " << s2 << endl;
            }
            return false;
        }
    }

    return true;
}

int _applicationLoopReplay(int pid)
{
        int nrw;
        long unsigned int reproIndex = 0;
        enum ConfFuzzOpcode op;
        bool done = false;

        auto it = referenceCorpus.cbegin();
        ConfFuzzEvent currentEvent = *it;

        while (true) {
            int errno_backup = 0;
            bool replayed = false;

            done = (reproIndex == referenceCorpus.size());

            if (!done)
                currentEvent = *it;

            op = readOpcodeWithTimeout(workerFIFOfd, WORKER_TIMEOUT);
            errno_backup = errno;

            determineWorkerMappings(pid);

            if (op == ConfFuzzOpcode::WORKER_LIBRARY_CALL ||
                op == ConfFuzzOpcode::WORKER_CALLBACK_CALL) {
                /* 1. Read call site from the fifo */
                uint64_t callSite = 0;
                nrw = read(workerFIFOfd, &callSite, sizeof(callSite));
                if (nrw != sizeof(callSite)) {
                    cerr << cerrPrefix << "Reading call site from worker FIFO failed unexpectedly "
                         << "(code " << errno << ")" << endl;
                    return REPRODUCE_ERROR;
                }

                /* 2. Read function name */
                if (readFuncName() != 0) {
                    return REPRODUCE_ERROR;
                }

                /* 3. Process arguments */
                readArguments(op == ConfFuzzOpcode::WORKER_CALLBACK_CALL,
                              false /* do not mutate arguments */, false);

                if (!done && eventsMatch(currentEvent)) {
                    replayed = true;

                    if (HEAVY_DEBUG_ENABLED && op == ConfFuzzOpcode::WORKER_LIBRARY_CALL)
                        cout << coutPrefix << "Got library call signal from worker " << pid << " (replaying)" << endl;
                    else if (HEAVY_DEBUG_ENABLED && op == ConfFuzzOpcode::WORKER_CALLBACK_CALL)
                        cout << coutPrefix << "Got callback call signal from worker " << pid << " (replaying)" << endl;

                    for (auto it = currentEvent.second.begin();
                              it != currentEvent.second.end(); ++it) {
                        /* do not replay callback instrumentation orders, we'd be doing it twice
                         * as readArguments already took care of it; besides, callback addresses
                         * might have changed (even if we disabled randomization) */
                        if (it->first != MONITOR_INSTRUMENT_ORDER) {
                            if (_writeToFIFO(monitorFIFOfd, it->first,
                                     it->second.data(), it->second.size()) > WRITETOFIFO_SUCCESS) {
                                cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly "
                                     << "(code " << errno << ")" << endl;
                                return REPRODUCE_ERROR;
                            }
                        }
                    }
                } else if (HEAVY_DEBUG_ENABLED) {
                    if (op == ConfFuzzOpcode::WORKER_LIBRARY_CALL)
                        cout << coutPrefix << "Got library call signal from worker " << pid << " (ignoring)" << endl;
                    else if (op == ConfFuzzOpcode::WORKER_CALLBACK_CALL)
                        cout << coutPrefix << "Got callback call signal from worker " << pid << " (ignoring)" << endl;
                    if (done)
                        return REPRODUCE_UNREPRODUCIBLE;
                    else
                        return REPRODUCE_FAILURE;
                }

                /* 4. Resume execution */
                op = ConfFuzzOpcode::MONITOR_EXEC_ACK;
                nrw = write(monitorFIFOfd, &op, sizeof(enum ConfFuzzOpcode));
                if (nrw != sizeof(enum ConfFuzzOpcode)) {
                    cerr << cerrPrefix << "Writing to exec ack monitor FIFO failed unexpectedly (code "
                         << errno << ")" << endl;
                    return REPRODUCE_ERROR;
                }
            } else if (op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN ||
                       op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL ||
                       op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN ||
                       op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL) {
                uint64_t retval = 0;
                if (op != ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL &&
                    op != ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL) {
                    /* 1. Read return value from the fifo */
                    nrw = read(workerFIFOfd, &retval, sizeof(retval));
                    if (nrw != sizeof(retval)) {
                        cerr << cerrPrefix << "Reading return value from worker FIFO failed unexpectedly "
                             << "(code " << errno << ")" << endl;
                        return REPRODUCE_ERROR;
                    }
                }

                /* 2. Redo writes, and send final return value to fifo */
                if (!done && eventsMatch(currentEvent)) {
                    replayed = true;

                    if (HEAVY_DEBUG_ENABLED) {
                        if (op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN ||
                            op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL)
                            cout << coutPrefix << "Got library return signal from worker "
                                 << pid << " (replaying)" << endl;
                        else if (op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN ||
                                 op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL)
                            cout << coutPrefix << "Got callback return signal from worker "
                                 << pid << " (replaying)" << endl;
                    }


                    for (auto it = currentEvent.second.begin();
                              it != currentEvent.second.end(); ++it) {
                        if (_writeToFIFO(monitorFIFOfd, it->first,
                                 it->second.data(), it->second.size()) > WRITETOFIFO_SUCCESS) {
                            cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly "
                                 << "(code " << errno << ")" << endl;
                            return REPRODUCE_ERROR;
                        }
                    }

                    if (!currentEvent.second.size()) { /* in this case let's just confirm */
                        if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::NOP_OPCODE, 0) > WRITETOFIFO_SUCCESS) {
                            cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                                 << errno << ")" << endl;
                            return REPRODUCE_ERROR;
                        }
                    }
                } else {
                    if (HEAVY_DEBUG_ENABLED) {
                        if (op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN ||
                            op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL)
                            cout << coutPrefix << "Got library return signal from worker "
                                 << pid << " (ignoring)" << endl;
                        else if (op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN ||
                                 op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL)
                            cout << coutPrefix << "Got callback return signal from worker "
                                 << pid << " (ignoring)" << endl;
                    }

                    if (done)
                        return REPRODUCE_UNREPRODUCIBLE;
                    else
                        return REPRODUCE_FAILURE;
                }
            } else if (op == ConfFuzzOpcode::INVALID_OPCODE) { /* the read failed */
                handleInvalidOpcode(errno_backup);
                break;
            } else {
                cerr << cerrPrefix << "Worker " << pid << " is sending garbage [" << op << "]" << endl;
                return REPRODUCE_ERROR;
            }

            if (replayed) {
                reproIndex += 1;
                std::advance(it, 1);
            }
        }

        if (!done) {
            cout << coutPrefix << "Didn't reproduce; the application took another path?" << endl;
            cout << coutPrefix << "We replayed " << reproIndex << "/" << referenceCorpus.size() << " actions." << endl;
            /* we didn't replay everything */
            return REPRODUCE_FAILURE;
        }

        return REPRODUCE_SUCCESS;
}

int applicationLoopReplay(void)
{
        bool ready = true;
        int ret = REPRODUCE_ERROR;

        workerPid = spawnApp();
        if (!workerPid) {
            ready = false;
        }

        /* open now as this is blocking */
        workerFIFOfd = open(fifoWorkerPath.c_str(), O_RDONLY);

        /* do handshake with the worker */
        if (ready) {
            if (!appHandshake(false)) {
                ready = false;
            }
        }

        /* start workload script if provided */
        pid_t workloadScriptPid = -1;
        if (ready && !workloadScriptPath.empty()) {
            workloadScriptPid = spawnWorkloadScript();
            if (workloadScriptPid == -1) {
                ready = false;
            }
        }

        /* if all set, start fuzzing */
        if (ready) {
            if (DEBUG_ENABLED) {
                cout << coutPrefix << "Ready to replay (attempt #"
                     << rpCounter << ")";
                if (HEAVY_DEBUG_ENABLED)
                    cout << " (worker " << workerPid << ")";
                cout << endl;
            }

            ret = _applicationLoopReplay(workerPid);
        } else {
                cout << coutPrefix << "Failed to start (attempt #"
                     << rpCounter << ")" << endl;
        }

        /* make sure that worker is nice and dead */
        if (workerPid != -1) {
            killWorker(workerPid);
        }

        /* ...and so should be all workload scripts... */
        if (workloadScriptPid != -1) {
            killWorker(workloadScriptPid);
        }

        /* wait for all children to terminate */
        while(wait(NULL) > 0);

        int termRet = handleTerminationReplay(workerPid);

        if (ret == REPRODUCE_SUCCESS)
            ret = termRet;

        cleanupAppFuzzingState();

        close(workerFIFOfd);

        return ret;
}

/* ==========================================================================
 * Core fuzzing / mutation routines
 * ========================================================================== */

void _applicationLoopSandbox(int pid)
{
        int nrw;
        enum ConfFuzzOpcode op;

        while (true) {
            int errno_backup = 0;
            op = readOpcodeWithTimeout(workerFIFOfd, WORKER_TIMEOUT);

            if (op == ConfFuzzOpcode::INVALID_OPCODE && errno == ERRNO_CONFFUZZ_PIPE_TIMEOUT) {
                cerr << cerrPrefix << "Timeout reached reading FIFO; "
                     << "this might be the cause of future errors" << endl;
            } else if (op == ConfFuzzOpcode::INVALID_OPCODE) {
                errno_backup = errno;
                if (HEAVY_DEBUG_ENABLED && errno_backup != 10 /* ECHILD */) {
                    /* this is usually redundant with the switch block for INVALID_OPCODE
		     * and pretty noisy, so only enable this in heavy debug mode.
		     * Filter out ECHILD because this is typically a spurious one that
		     * happens when the child dies */
                    cerr << cerrPrefix << "Warning, reading FIFO failed with code "
                         << errno_backup << ", this might be the cause of future errors" << endl;
                }
            }

            /* We want to make sure that our view of the mappings is always as
             * up-to-date as possible. Note that this won't do much if the
             * mappings didn't change. */
            determineWorkerMappings(pid);

            if (op == ConfFuzzOpcode::WORKER_LIBRARY_CALL) {
                /* 1. Read call site from the fifo */
                uint64_t callSite = 0;
                nrw = read(workerFIFOfd, &callSite, sizeof(callSite));
                if (nrw != sizeof(callSite)) {
                    cerr << cerrPrefix << "Reading call site from worker FIFO failed unexpectedly "
                         << "(code " << errno << ")" << endl;
                    break;
                }

                if (DEBUG_ENABLED)
                    cout << coutPrefix << "Got library call signal from worker " << pid << endl;

                if (knownCallSites.find(callSite) == knownCallSites.end()) {
                    if (DEBUG_ENABLED)
                        cout << coutPrefix << "Discovered new call site " << (void*) callSite << endl;
                    knownCallSites.insert(callSite);
                }

                /* 2. Read function name */
                readFuncName();

                /* 3. Process arguments */
                uint64_t argCount = 0;
                nrw = read(workerFIFOfd, &argCount, sizeof(argCount));
                if (nrw != sizeof(argCount)) {
                    cerr << cerrPrefix << "Reading call site from worker FIFO failed "
                         << "unexpectedly (code " << errno << ")" << endl;
                    break;
                }

                if (argCount > LIBAPI_ARG_UPPER_LIMIT) {
                    cerr << cerrPrefix << "Read garbage argc from worker FIFO" << endl;
                    break;
                }

                for (unsigned int i = 0; i < argCount; i++) {
                    /* 3.1. Read argument size */
                    uint64_t argumentSize = 0;
                    nrw = read(workerFIFOfd, &argumentSize, sizeof(argumentSize));
                    if (nrw != sizeof(argumentSize)) {
                        cerr << cerrPrefix << "Reading call site from worker FIFO failed "
                             << "unexpectedly (code " << errno << ")" << endl;
                        break;
                    }

                    /* 3.2. Read argument value */
                    uint64_t argument = 0;
                    nrw = read(workerFIFOfd, &argument, sizeof(argument));
                    if (nrw != sizeof(argument)) {
                        cerr << cerrPrefix << "Reading call site from worker FIFO failed "
                             << "unexpectedly (code " << errno << ")" << endl;
                        break;
                    }

                    /* 3.3. Store this data (will be used during WORKER_LIBRARY_RETURN) */
                    if (isNonCodePointerValue(argument)) {
                        currentPointers.insert({argument, argumentSize});
                    } else if (isCodePointerValue(argument) &&
                               instrumentedCallbacks.find(argument) == instrumentedCallbacks.end()) {
                        /* instrument potential callbacks (if not already done) */
                        if (writeToFIFO(monitorFIFOfd,
                                ConfFuzzOpcode::MONITOR_INSTRUMENT_ORDER,
                                1 /* address to instrument */, argument) > WRITETOFIFO_SUCCESS) {
                            cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly "
                                 << "(code " << errno << ")" << endl;
                            break;
                        } else if (DEBUG_ENABLED) {
                            cout << coutPrefix << "Instrumenting newly found callback "
                                 << (void*) argument << " in worker " << pid << endl;
                        }

                        instrumentedCallbacks.insert(argument);
                    }
                }

                /* 4. Resume execution */
                op = ConfFuzzOpcode::MONITOR_EXEC_ACK;
                nrw = write(monitorFIFOfd, &op, sizeof(enum ConfFuzzOpcode));
                if (nrw != sizeof(enum ConfFuzzOpcode)) {
                    cerr << cerrPrefix << "Failed to send library call ACK." << endl;
                    break;
                }
            } else if (op == ConfFuzzOpcode::WORKER_CALLBACK_CALL) {
                /* 1. Read call site from the fifo */
                uint64_t callSite = 0;
                nrw = read(workerFIFOfd, &callSite, sizeof(callSite));
                if (nrw != sizeof(callSite)) {
                    cerr << cerrPrefix << "Reading call site from worker FIFO failed unexpectedly "
                         << "(code " << errno << ")" << endl;
                    break;
                }

                if (DEBUG_ENABLED)
                    cout << coutPrefix << "Detected callback call from the library to the application"
                         << endl;

                /* 2. Read function name */
                readFuncName();

                /* 3. Process arguments */
                readArguments(true /* this is a callback */,
                              true /* mutate arguments */,
                              true /* got ahead, be verbose */);

                /* 4. Resume execution */
                op = ConfFuzzOpcode::MONITOR_EXEC_ACK;
                nrw = write(monitorFIFOfd, &op, sizeof(enum ConfFuzzOpcode));
                if (nrw != sizeof(enum ConfFuzzOpcode)) {
                    cerr << cerrPrefix << "Failed to send library call ACK." << endl;
                    break;
                }
            } else if (op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL) {
                /* since we are in sandbox mode, we're not really interesting in
                 * mutating anything here. simply return NOP. */

                if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::NOP_OPCODE, 0) > WRITETOFIFO_SUCCESS) {
                    cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                         << errno << ")" << endl;
                    break;
                }

                cout << coutPrefix << "Got callback return signal from worker " << pid << endl;
            } else if (op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN) {
                /* since we are in sandbox mode, we're not really interesting in
                 * mutating anything here. simply empty the FIFO. */

                /* 1. Read return value from the fifo */
                uint64_t retval = 0;
                nrw = read(workerFIFOfd, &retval, sizeof(retval));
                if (nrw != sizeof(retval)) {
                    cerr << cerrPrefix << "Reading return value from worker FIFO failed "
                         << "unexpectedly (code " << errno << ")" << endl;
                    break;
                }

                /* 2. Don't change the return value */
                if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::NOP_OPCODE, 0) > WRITETOFIFO_SUCCESS) {
                    cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                         << errno << ")" << endl;
                    break;
                }

                cout << coutPrefix << "Got callback return signal from worker " << pid << endl;
            } else if (op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN ||
                       op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL) {
                uint64_t retval = 0;
                if (op != ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL) {
                    /* 1. Read return value from the fifo */
                    nrw = read(workerFIFOfd, &retval, sizeof(retval));
                    if (nrw != sizeof(retval)) {
                        cerr << cerrPrefix << "Reading return value from worker FIFO failed "
                             << "unexpectedly (code " << errno << ")" << endl;
                        break;
                    }
                }

                /* 2. send some pointer write orders */
                for (auto it = currentPointers.begin(); it != currentPointers.end(); it++) {

                    /* ignore if we cannot write anyways */
                    if (!it->second)
                        continue;

                    /* to mutate or not mutate? */
                    if (!proceedWithMutation()) {
                        continue;
                    }

                    uint64_t maxSize = it->second > sizeof(uint64_t) ?
                            sizeof(uint64_t) : it->second;
                    uint64_t maxWrites = 1 + it->second - maxSize;

		    /* 3 is totally arbitrary. We don't want too many writes, as it makes
		     * crashes longer to minimize, which, on the long run, gets pretty
		     * expensive. std::min is necessary to avoid running into an infinite
		     * loop when selecting offsets */
                    int numWrites = random_int_range(1, std::min(3lu, maxWrites));

                    /* remember where we wrote to avoid writing 2x at the same offset */
                    std::set<uint64_t> offs;

                    /* write the buffer a number of times, at different offsets */
                    for (int i = 0; i < numWrites; i++) {
                        /* new value to write */
                        uint64_t newval = mutateValue(0);

                        /* size of the write (at most 64 bits, but we can have multiple
                         * sequential writes).
                         *
                         * NOTE: do not always write "as much as you can"; this is not
                         * a super good strategy as it might hide more subtle faults
                         * that we could trigger with small writes */
                        uint64_t size = random_int_range(1, maxSize);

                        /* offset in the buffer where to write, retry as long as we didn't
			 * get a new one */
                        uint64_t write_offset;
                        do {
                            write_offset = random_int_range(0, it->second - size);
                        } while (offs.find(write_offset) != offs.end());
                        offs.insert(write_offset);

                        uint64_t address = (uint64_t) it->first + write_offset;

                        if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::MONITOR_WRITE_ORDER,
                                3 /* address to write to, size, value */,
                                address, size, newval) > WRITETOFIFO_SUCCESS) {
                            cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly "
                                 << "(code " << errno << ")" << endl;
                            break;
                        } else if (DEBUG_ENABLED) {
                            cout << coutPrefix << "Messing with shared memory of worker " << pid
                                 << " at " << (void*) it->first << " + " << write_offset
                                 << " (-> 0x" << std::hex << (uintptr_t) (uint8_t) newval
                                 << std::dec << ", size " << size << " / " << it->second
                                 << ")" << endl;
                        }
                    }
                }

                currentPointers.clear();

                /* 3. Send final return value to fifo */
                uint64_t retorder = mutateValue(retval);

                if (op != ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL && retval != retorder) {
                    /* amend the return value */
                    if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::MONITOR_RETURN_ORDER,
                            1 /* new return value */, retorder) > WRITETOFIFO_SUCCESS) {
                        cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                             << errno << ")" << endl;
                        break;
                    }
                } else {
                    /* don't change the return value */
                    if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::NOP_OPCODE, 0) > WRITETOFIFO_SUCCESS) {
                        cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                             << errno << ")" << endl;
                        break;
                    }
                }

                cout << coutPrefix << "Got library return signal from worker " << pid;
                if (op != ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL && retval != retorder) {
                    cout << " (" << (void *) retval << " -> " << (void*) retorder
                         << ")" << endl;
                } else {
                    cout << endl;
                }
            } else if (op == ConfFuzzOpcode::INVALID_OPCODE) { /* the read failed */
                handleInvalidOpcode(errno_backup);
                break;
            } else {
                cerr << cerrPrefix << "Worker " << pid << " is sending garbage [" << op << "]" << endl;
                break;
            }
        }
}

void _applicationLoopSafebox(int pid)
{
        std::map<uint64_t /* pointer */, uint64_t /* object size */> currentPointers;

        int nrw;
        enum ConfFuzzOpcode op;

        while (true) {
            int errno_backup = 0;

            op = readOpcodeWithTimeout(workerFIFOfd, WORKER_TIMEOUT);

            if (op == ConfFuzzOpcode::INVALID_OPCODE && errno == ERRNO_CONFFUZZ_PIPE_TIMEOUT) {
                cerr << cerrPrefix << "Timeout reached reading FIFO; "
                     << "this might be the cause of future errors" << endl;
            } else if (op == ConfFuzzOpcode::INVALID_OPCODE) {
                errno_backup = errno;
                if (HEAVY_DEBUG_ENABLED &&
                      errno_backup != ECHILD &&
                      errno_backup != ERRNO_CONFFUZZ_PIPE_READ_BUG_ON) {
                    /* this is usually redundant with the switch block for INVALID_OPCODE
                     * and pretty noisy, so only enable this in heavy debug mode.
                     * Filter out ECHILD and ERRNO_CONFFUZZ_PIPE_READ_BUG_ON because these
                     * are typically false positives / spurious ones that happens when the
                     * child dies */
                    cerr << cerrPrefix << "Warning, reading FIFO failed with code "
                         << errno_backup << ", this might be the cause of future errors" << endl;
                }
            }

            determineWorkerMappings(pid);

            if (op == ConfFuzzOpcode::WORKER_LIBRARY_CALL) {
                /* unlike sandbox mode, we mutate arguments here and perform
                 * memory accesses */

                /* 1. Read call site from the fifo */
                uint64_t callSite = 0;
                nrw = read(workerFIFOfd, &callSite, sizeof(callSite));
                if (nrw != sizeof(callSite)) {
                    cerr << cerrPrefix << "Reading call site from worker FIFO failed unexpectedly "
                         << "(code " << errno << ")" << endl;
                    break;
                }

                if (DEBUG_ENABLED)
                    cout << coutPrefix << "Got library call signal from worker " << pid << endl;

                /* 2. Read function name */
                readFuncName();

                /* 3. Process arguments */
                readArguments(false /* this is not a callback */,
                              true /* mutate arguments */,
                              true /* got ahead, be verbose */);

                /* TODO do some memory writes here */

                /* 4. Resume execution */
                op = ConfFuzzOpcode::MONITOR_EXEC_ACK;
                nrw = write(monitorFIFOfd, &op, sizeof(enum ConfFuzzOpcode));
                if (nrw != sizeof(enum ConfFuzzOpcode)) {
                    cerr << cerrPrefix << "Failed to send library call ACK." << endl;
                    break;
                }
            } else if (op == ConfFuzzOpcode::WORKER_CALLBACK_CALL) {
                /* since we are in safebox mode, we're not really interesting in
                 * mutating anything here. simply empty the FIFO. */

                /* 1. Read call site from the fifo */
                uint64_t callSite = 0;
                nrw = read(workerFIFOfd, &callSite, sizeof(callSite));
                if (nrw != sizeof(callSite)) {
                    cerr << cerrPrefix << "Reading call site from worker FIFO failed unexpectedly "
                         << "(code " << errno << ")" << endl;
                    break;
                }

                if (DEBUG_ENABLED)
                    cout << coutPrefix << "Detected callback call from the library to the application"
                         << endl;

                /* 2. Read function name */
                readFuncName();

                /* 3. Process arguments */
                readArguments(true /* this is a callback */,
                              false /* do not mutate arguments */,
                              true /* got ahead, be verbose */);

                /* 4. Resume execution */
                op = ConfFuzzOpcode::MONITOR_EXEC_ACK;
                nrw = write(monitorFIFOfd, &op, sizeof(enum ConfFuzzOpcode));
                if (nrw != sizeof(enum ConfFuzzOpcode)) {
                    cerr << cerrPrefix << "Failed to send library call ACK." << endl;
                    break;
                }
            } else if (op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL) {
                /* since we are in safebox mode, we're not really interesting in
                 * mutating anything here. simply return NOP. */

                if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::NOP_OPCODE, 0) > WRITETOFIFO_SUCCESS) {
                    cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                         << errno << ")" << endl;
                    break;
                }

                cout << coutPrefix << "Got library return signal from worker " << pid << endl;
            } else if (op == ConfFuzzOpcode::WORKER_LIBRARY_RETURN) {
                /* since we are in safebox mode, we're not really interesting in
                 * mutating anything here. simply empty the FIFO. */

                /* 1. Read return value from the fifo */
                uint64_t retval = 0;
                nrw = read(workerFIFOfd, &retval, sizeof(retval));
                if (nrw != sizeof(retval)) {
                    cerr << cerrPrefix << "Reading return value from worker FIFO failed "
                         << "unexpectedly (code " << errno << ")" << endl;
                    break;
                }

                /* 2. Don't change the return value */
                if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::NOP_OPCODE, 0) > WRITETOFIFO_SUCCESS) {
                    cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                         << errno << ")" << endl;
                    break;
                }

                cout << coutPrefix << "Got library return signal from worker " << pid << endl;
            } else if (op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN ||
                       op == ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL) {
                /* unlike sandbox mode, we *are* interested in mutating the return value
                 * here */

                /* 1. Read return value from the fifo */
                uint64_t retval = 0;
                if (op != ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL) {
                    nrw = read(workerFIFOfd, &retval, sizeof(retval));
                    if (nrw != sizeof(retval)) {
                        cerr << cerrPrefix << "Reading return value from worker FIFO failed "
                             << "unexpectedly (code " << errno << ")" << endl;
                        break;
                    }
                }

                /* 2. Send final return value to fifo */
                uint64_t retorder = mutateValue(retval);

                if (op != ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL && retval != retorder) {
                    /* amend the return value */
                    if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::MONITOR_RETURN_ORDER,
                            1 /* new return value */, retorder) > WRITETOFIFO_SUCCESS) {
                        cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                             << errno << ")" << endl;
                        break;
                    }
                } else {
                    /* don't change the return value */
                    if (writeToFIFO(monitorFIFOfd, ConfFuzzOpcode::NOP_OPCODE, 0)
                          > WRITETOFIFO_SUCCESS) {
                        cerr << cerrPrefix << "Writing to monitor FIFO failed unexpectedly (code "
                             << errno << ")" << endl;
                        break;
                    }
                }

                cout << coutPrefix << "Got callback return signal from worker " << pid;
                if (op != ConfFuzzOpcode::WORKER_CALLBACK_RETURN_NO_RETVAL && retval != retorder) {
                    cout << " (" << (void *) retval << " -> " << (void*) retorder
                         << ")" << endl;
                } else {
                    cout << endl;
                }
            } else if (op == ConfFuzzOpcode::INVALID_OPCODE) { /* the read failed */
                handleInvalidOpcode(errno_backup);
                break;
            } else {
                cerr << cerrPrefix << "Worker " << pid << " is sending garbage [" << op << "]" << endl;
                break;
            }
        }
}

/* applicationLoop: return true if the crash was new and non-false-positive */
bool applicationLoop(void)
{
        bool ready = true;

        appCounter += 1;

        cout << bold_on << coutPrefix << "Run #" << appCounter << colouring_off << endl;

        workerPid = spawnApp();
        if (!workerPid) {
            cerr << cerrPrefix << "Failed to spawn application." << endl;
            ready = false;
        }

        /* open now as this is blocking */
        workerFIFOfd = open(fifoWorkerPath.c_str(), O_RDONLY);

        /* do handshake with the worker */
        if (ready) {
            if (!appHandshake(true)) {
                ready = false;
            }
        }

        /* start workload script if provided */
        pid_t workloadScriptPid = -1;
        if (ready && !workloadScriptPath.empty()) {
            cout << coutPrefix << "Starting workload script..." << endl;
            workloadScriptPid = spawnWorkloadScript();
            if (workloadScriptPid == -1) {
                ready = false;
            }
        }

        /* if all set, start fuzzing */
        if (ready) {
            if (DEBUG_ENABLED)
                cout << coutPrefix << "In the fuzzing loop!" << endl;

            if (SAFEBOX_MODE_ENABLED) {
                _applicationLoopSafebox(workerPid);
            } else {
                _applicationLoopSandbox(workerPid);
            }
        }

        /* make sure that worker is nice and dead */
        if (workerPid != -1) {
            killWorker(workerPid);
        }

        /* ...and so should be all workload scripts... */
        if (workloadScriptPid != -1) {
            killWorker(workloadScriptPid);
        }

        /* wait for all children to terminate */
        while(wait(NULL) > 0);

        bool isNew = false;
        if (ready) {
            isNew = handleTermination(workerPid);
        } else {
            if (DEBUG_ENABLED)
                cout << coutPrefix << "Failed iteration, ignore." << endl;

            /* we didn't even manage to run this iteration, don't increment
             * the counter */
            appCounter -= 1;
        }

        cleanupAppFuzzingState();

        close(workerFIFOfd);

        return isNew;
}

static inline bool _fuzzingSetup(void)
{
        /* remove FIFOs if they already exist */
        /* we have to do this every time to ensure that no stale
         * information remains in the pipes */
        remove(fifoMonitorPath.c_str());
        remove(fifoWorkerPath.c_str());

        mkfifo(fifoMonitorPath.c_str(), 0666);
        mkfifo(fifoWorkerPath.c_str(),  0666);

        monitorFIFOfd = open(fifoMonitorPath.c_str(), O_RDWR);
        if (monitorFIFOfd <= 0) {
            cerr << cerrPrefix << "Failed to open monitor FIFO (code " << errno << ")" << endl;
            return false;
        }

        return true;
}

static inline bool _fuzzingTearDown(void)
{
        close(monitorFIFOfd);
        return true;
}

/* Try to reproduce the current bug (crashCounter - 1) with the current
 * reference corpus (in referenceCorpus). The `reps` parameter sets the
 * maximum number of attemps. We stop as soon as we managed to reproduce
 * (REPRODUCE_SUCCESS) or show that the bug is not reproducible with this
 * corpus (REPRODUCE_UNREPRODUCIBLE). Runs that failed due to a system
 * error (non-app-related, REPRODUCE_ERROR) are not counted in the `reps`
 * attempts. Fatal system errors stop the loop and are reported with
 * REPRODUCE_CRIT_ERROR. */
int reproduceLoop(int reps)
{
        int ret = REPRODUCE_FAILURE;

        for (rpCounter = 0; (reps != -1 && rpCounter < reps)
                          && ret != REPRODUCE_SUCCESS; rpCounter++) {
            /* get a fresh corpus setup for this run */
            cleanupCorpus();

            if (!_fuzzingSetup()) {
                return REPRODUCE_CRIT_ERROR;
            }

            ret = applicationLoopReplay();

            if (!_fuzzingTearDown()) {
                return REPRODUCE_CRIT_ERROR;
            }

            /* stop if we managed to prove that the crash is not reproducible
             * with this corpus */
            if (ret == REPRODUCE_UNREPRODUCIBLE) {
                return REPRODUCE_UNREPRODUCIBLE;
            }

            /* if we encountered a (minor) system error, take a small break to
             * let the situation stabilize, don't include it in the count. */
            if (ret == REPRODUCE_ERROR) {
                rpCounter -= 1;
                sleep(0.5);
            }
        }

        return ret;
}

ConfFuzzCorpus pushFrontInCorpusEvent(ConfFuzzCorpus c, ConfFuzzMsg m, int n)
{
        ConfFuzzCorpus ret = c;
        auto it = ret.begin();
        std::advance(it, n); /* go to n-th event */
        it->second.push_front(m);
        return ret;
}

ConfFuzzCorpus popInCorpusEvent(ConfFuzzCorpus c, int n, int x)
{
        ConfFuzzCorpus ret = c;
        auto it = ret.begin();
        std::advance(it, n);
        auto it2 = it->second.begin();
        std::advance(it2, x);
        it->second.erase(it2);
        return ret;
}

void fuzzingLoop(int iterations)
{
        bool isNew;

        while (!iterations || appCounter < iterations) {
            if (!_fuzzingSetup())
                return;

            isNew = applicationLoop();

            if (!_fuzzingTearDown())
                return;

            /* now, if this was a new crash, reproduce and minimize */
            if (isNew) {
                /* remove WORKER_UP event */
                corpus.pop_front();

                /* remove potential INVALID_OPCODE at the end */
                if (getEventOpcode(corpus.back()) == ConfFuzzOpcode::INVALID_OPCODE) {
                    corpus.pop_back();
                }

                /* backup corpus for later comparison */
                const ConfFuzzCorpus backupCorpus = corpus;
                ConfFuzzCorpus backupCorpusWorkingCpy = corpus;

                /* 1. reproduce */
                coutPrefix = "  [+] ";
                _cerrPrefix = "  [E] ";
                cout << coutPrefix << "Reproducing the crash..." << endl;

                referenceCorpus = corpus;
                int ret = reproduceLoop(REPRO_MAX_RETRIES);
                int minimizeMaxAttempts = rpCounter * MINIMIZE_MAX_RETRIES_FACTOR;

                if (ret == REPRODUCE_SUCCESS) {
                    cout << coutPrefix << "Success, " << bold_on << "reproduced iteration #"
                         << appCounter << colouring_off << endl;
                }

                cleanReproRuns(crashCounter - 1);

                coutPrefix = "[+] ";
                _cerrPrefix = "[E] ";

                if (ret == REPRODUCE_CRIT_ERROR) {
                    return;
                } else if (ret != REPRODUCE_SUCCESS) {
                    cout << "  [+] Unable to reproduce iteration #" << appCounter << ": "
                         << red_on << "considering non-reproducible" << colouring_off << endl;
                    markNonReproducible(crashCounter - 1);
                    goto cleanup;
                }

                /* 2. minimize */
                int actionCounter = 0, attempts = 0, el = 0;
                int n = backupCorpus.size() - 1;
                std::string s;

                coutPrefix = "  [+] ";
                _cerrPrefix = "  [E] ";

                /* 2.1. generate empty corpus */
                ConfFuzzCorpus minimizedCorpus;
                for (auto it =  backupCorpus.begin();
                          it != backupCorpus.end(); ++it) {
                    actionCounter += it->second.size();
                    minimizedCorpus.push_back(std::make_pair(
                        it->first, /* the nth event */
                        std::list<ConfFuzzMsg>() /* nothing */
                    ));
                }

                /* 2.2 do we even need to minimize? */
                if (actionCounter < 2) {
                    cout << coutPrefix << "No need to minimize, corpus has "
                         << actionCounter << " action(s)" << endl;
                    makeMinimalFolderFromRUN(crashCounter - 1, appCounter);
                    goto minimizedcleanup;
                }

                cout << coutPrefix << "Minimizing the crash..." << endl;

                if (DEBUG_ENABLED) {
                    cout << coutPrefix << "Enabling at most " << minimizeMaxAttempts
                         << " attempts per round." << endl;
                }

                /* 2.3. go over all actions in backwards order (the most
                 *      important is usually at the end) */
                for (auto itEv =  backupCorpus.crbegin();
                          itEv != backupCorpus.crend(); ++itEv) {
                    int x = itEv->second.size() - 1;
                    for (auto itMsg =  itEv->second.crbegin();
                              itMsg != itEv->second.crend(); ++itMsg) {
                        /* 2.3.1 is this action sufficient? */
                        referenceCorpus = pushFrontInCorpusEvent(minimizedCorpus,
                            *itMsg /* push this action */,
                            n /* at event n */);

                        ret = reproduceLoop(minimizeMaxAttempts);

                        if (ret == REPRODUCE_SUCCESS) {
                            /* bingo, we have our minimal corpus */
                            makeMinimalFolderFromRP(crashCounter - 1, rpCounter - 1);
                            minimizedCorpus = referenceCorpus;
                            goto minimized;
                        }

                        if (ret == REPRODUCE_CRIT_ERROR) {
                            coutPrefix = "[+] ";
                            _cerrPrefix = "[E] ";
                            return;
                        }

                        /* 2.3.2 is this action necessary? */
                        referenceCorpus = popInCorpusEvent(backupCorpusWorkingCpy,
                            n /* at event n */, x /* action x */);

                        ret = reproduceLoop(minimizeMaxAttempts);

                        if (ret == REPRODUCE_SUCCESS) {
                            /* not a necessary action */
                            backupCorpusWorkingCpy = referenceCorpus;
                            cout << coutPrefix << "Identified an unnecessary action." << endl;
                        } else if (ret == REPRODUCE_CRIT_ERROR) {
                            coutPrefix = "[+] ";
                            _cerrPrefix = "[E] ";
                            return;
                        } else {
                            /* necessary action, include it in the minimized corpus */
                            minimizedCorpus = pushFrontInCorpusEvent(minimizedCorpus,
                                *itMsg /* push this action */,
                                n /* at event n */);
                            cout << coutPrefix << "Identified a necessary action." << endl;
                        }

                        cleanReproRuns(crashCounter - 1);

                        attempts++;
                        x--;
                    }

                    n--;
                }

                /* if we minimized this point, then we didn't manage to even
                 * reproduce the crash with the full corpus, there's something
                 * fishy. */
                cerr << cerrPrefix << "Failure: we did not manage to minimize this crash" << endl;
                cerr << cerrPrefix << "This is curious and could be due to randomness in the app," << endl;
                cerr << cerrPrefix << "or to a bug in ConfFuzz." << endl;
                goto minimizedcleanup;

minimized:
                cout << coutPrefix << "Success, " << bold_on << "minimized iteration #"
                     << appCounter << " after " << attempts << " attempts" << colouring_off << endl;

                /* keep track of API endpoints that matter */
                for (auto itEv =  minimizedCorpus.cbegin();
                          itEv != minimizedCorpus.cend(); ++itEv) {

                    if (itEv->first.first == ConfFuzzOpcode::WORKER_LIBRARY_CALL) {
                        s = std::string(itEv->first.second.begin(), itEv->first.second.end());
                    }

                    if (!itEv->second.empty() &&
                         (itEv->first.first == ConfFuzzOpcode::WORKER_LIBRARY_CALL ||
                          itEv->first.first == ConfFuzzOpcode::WORKER_LIBRARY_RETURN ||
                          itEv->first.first == ConfFuzzOpcode::WORKER_LIBRARY_RETURN_NO_RETVAL)) {

                        bool isAllCB = true;
                        for (auto itMsg =  itEv->second.cbegin();
                                  itMsg != itEv->second.cend(); ++itMsg) {
                            if (itMsg->first != ConfFuzzOpcode::MONITOR_INSTRUMENT_ORDER)
                                isAllCB = false;
                        }

                        if (isAllCB)
                            continue;

                        /* this endpoint matters */
                        criticalAPIelements.insert(s);
                        el++;
                    }
                }

                if (DEBUG_ENABLED)
                    cout << coutPrefix << "Found " << el << " API endpoints that matter." << endl;

minimizedcleanup:
                cleanReproRuns(crashCounter - 1);

                coutPrefix = "[+] ";
                _cerrPrefix = "[E] ";
            }

cleanup:
            cleanupCorpus();

            /* we are done with this iteration, if we found a new bug, it's all
             * reproduced and minimized (or at least we tried!) */
            if (DEBUG_ENABLED)
                cout << coutPrefix << "Done with iteration #" << appCounter << endl;

            cout << endl;
        }
}

/* ==========================================================================
 * Setup and argument parsing boilerplate
 * ========================================================================== */

void usage(char *name)
{
        cerr << "Usage: " << name << " [<opt. params>] -l <num libs> <target shared libs> \\" << endl;
        cerr << "                                              <app binary> -- [<app params>]" << endl << endl;
        cerr << "Optional application-specific parameters:" << endl;
        cerr << "      -t <workload program binary> : workload generator for the application" << endl;
        cerr << "      -r <api regex> : regex describing the component fuzz target's API" << endl;
        cerr << "      -T <timeout> : max time between two interface crossings (default " << WORKER_TIMEOUT << "s)" << endl;
        cerr << "      -x : (temporary) use version 2 of the symbol extracter (default " << USE_SE_V2 << ")" << endl << endl;
        cerr << "Optional automation parameters:" << endl;
        cerr << "      -s <seed> : RNG generator seed to use (default random)" << endl;
        cerr << "      -i <iterations> : limit number of fuzzing iterations (default unlimited)" << endl;
        cerr << "      -F <api file> : provide API description manually instead of generating it" << endl;
        cerr << "      -G <types file> : provide types description manually instead of generating it" << endl;
        cerr << "      -X : generate API and type description files, then exit (incompatible w/ -F and -G)" << endl;
        cerr << "      -L <shared lib> : additional libraries to be used as part of type analysis" << endl;
        cerr << "                        (-L can be passed multiple times to specify several libraries)" << endl << endl;
        cerr << "Optional output parameters:" << endl;
        cerr << "      -O <crash path> : path to store fuzzer output (default " << baseCrashPath.c_str() << ")" << endl;
        cerr << "      -d : enable debugging output" << endl;
        cerr << "      -S : statically determine API entry point count" << endl;
        cerr << "      -m : reproduce/minimize false positives (default " << FPOSITIVE_MINIMIZATION_ENABLED << ")"<< endl;
        cerr << "      -D : enable heavy debugging mode" << endl;
        cerr << "      -C : disable fancy output" << endl << endl;
        cerr << "Optional mode parameters:" << endl;
        cerr << " default : sandbox mode        (the application is attacked)" << endl;
        cerr << "      -R : enable safebox mode (the component fuzz target is attacked)" << endl << endl;
        cerr << "Example:" << endl;
        cerr << "    conffuzz -d -l 1 /lib/libgs.so.9.55 /usr/bin/convert -- foo.ps foo.pdf" << endl;
        cerr << "    conffuzz -d /lib/libgs.so.9.55 /usr/bin/convert -- foo.ps foo.pdf" << endl;
        cerr << "        (if -l is omitted, -l 1 is assumed)" << endl;
}

int main(int argc, char *argv[])
{
        char *regex = NULL, *workloadScript = NULL, *crashFolderPath = NULL,
             *_existingAPIFilePath = NULL, *_existingTypesFilePath = NULL;
        bool seeded = false;
        uint64_t iterations = 0;
        int c;

        cout << "  _____          _______          " << endl;
        cout << " / ___/__  ___  / _/ __/_ ________" << endl;
        cout << "/ /__/ _ \\/ _ \\/ _/ _// // /_ /_ /" << endl;
        cout << "\\___/\\___/_//_/_//_/  \\_,_//__/__/" << endl;
        cout << endl;

        /* set default values to be printed by usage() */
        char buffer[PATH_MAX];
        readlink("/proc/self/exe", buffer, PATH_MAX);
        selfPath += buffer;

        baseCrashPath += selfPath.remove_filename();
        baseCrashPath += "../";

        /* parse arguments */
        if (argc < 3) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }

        int libNum = 1;
        while ((c = getopt (argc, argv, "xmXSRCdDt:r:F:G:s:l:L:i:T:O:")) != -1) {
            switch (c)
            {
                case 'T':
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -T.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    try {
                        WORKER_TIMEOUT = std::atoi(optarg);
                    } catch (...) {
                        cerr << cerrPrefix << "Invalid value passed to -T (" << optarg << ").\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 'i':
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -i.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    try {
                        iterations = std::atoi(optarg);
                    } catch (...) {
                        cerr << cerrPrefix << "Invalid value passed to -i (" << optarg << ").\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 'l':
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -l.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    try {
                        libNum = std::atoi(optarg);
                        if (libNum < 1)
                            throw std::invalid_argument("");
                    } catch (...) {
                        cerr << cerrPrefix << "Invalid value passed to -l (" << optarg << ").\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 's':
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -s.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    try {
                        seed = std::atoi(optarg);
                    } catch (...) {
                        cerr << cerrPrefix << "Invalid value passed to -s (" << optarg << ").\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    seeded = true;
                    break;
                case 'F':
                    _existingAPIFilePath = optarg;
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -F.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 'L':
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -L.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    analysisLibraryPaths.push_back(optarg);
                    break;
                case 'G':
                    _existingTypesFilePath = optarg;
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -G.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 'O':
                    crashFolderPath = optarg;
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -O.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 't':
                    workloadScript = optarg;
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -t.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 'r':
                    regex = optarg;
                    if (optarg[0] == '-') {
                        cerr << cerrPrefix << "Invalid value passed to -r.\n";
                        usage(argv[0]);
                        return EXIT_FAILURE;
                    }
                    break;
                case 'x':
                    USE_SE_V2 = true;
                    break;
                case 'm':
                    FPOSITIVE_MINIMIZATION_ENABLED = true;
                    break;
                case 'R':
                    SAFEBOX_MODE_ENABLED = true;
                    break;
                case 'S':
                    STATICALLY_COUNT_EPOINT_ENABLED = true;
                    break;
                case 'd':
                    DEBUG_ENABLED = true;
                    break;
                case 'X':
                    ONLY_EXTRACT_API_ENABLED = true;
                    break;
                case 'D':
                    DEBUG_ENABLED = true;
                    HEAVY_DEBUG_ENABLED = true;
                    break;
                case 'C':
                    COLOURING_ENABLED = false;
                    break;
                case '?':
                    if (optopt == 't' || optopt == 'r' || optopt == 'F' || optopt == 's') {
                        cerr << cerrPrefix << "Option -" << optopt << " requires an argument." << endl;
                    } else if (isprint (optopt)) {
                        cerr << cerrPrefix << "Unknown option `-" << optopt << "'." << endl;
                    } else {
                        cerr << cerrPrefix << "Unknown option character `" << optopt << "'." << endl;
                    }
                    /* intentional fall through */
                default:
                    usage(argv[0]);
                    return EXIT_FAILURE;
            }
        }

        if (regex == NULL || strlen(regex) == 0)
            regex = const_cast<char*>(""); /* safe, we'll never write to this */

        if (argc < optind + libNum + 1 /* a mandatory binary path */) {
            cerr << cerrPrefix << "Not enough arguments supplied; is the "
                 << "application binary path missing?" << endl;
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }

        if ((ONLY_EXTRACT_API_ENABLED && _existingAPIFilePath) ||
            (ONLY_EXTRACT_API_ENABLED && _existingTypesFilePath)) {
            cerr << cerrPrefix << "-X is incompatible with -F/-G" << endl;
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < libNum; i++) {
            std::filesystem::path libraryPath;
            libraryPath += argv[optind + i];
            libraryPaths.push_back(libraryPath);
        }

        appPath += argv[optind + libNum];
        appPath = std::filesystem::absolute(appPath);

        cout << coutPrefix << "Starting ConfFuzz..." << endl;

        /* disable randomization: this will apply to children as well and
         * allow us to compare stack traces more easily */
        personality(ADDR_NO_RANDOMIZE);

        /* initialize seed: */
        if (!seeded) {
            seed = time(NULL);
        }
        rand_engine = std::default_random_engine(seed);

        if (DEBUG_ENABLED) {
            cout << coutPrefix << "Using seed: " << seed << endl;
        }

        /* find paths to different conffuzz components */
        if (workloadScript) {
            workloadScriptPath += workloadScript;
        }

        if (crashFolderPath) {
            baseCrashPath = crashFolderPath;
            if (DEBUG_ENABLED)
                cout << coutPrefix << "Writing crash data to " << crashFolderPath << endl;
        }

        std::ifstream ifile;
        if (_existingAPIFilePath) {
            existingAPIFilePath = _existingAPIFilePath;

            /* make sure that it exists */
            ifile.open(existingAPIFilePath.c_str());
            if(!ifile) {
               cerr << cerrPrefix << "Existing API description path seems invalid." << endl;
               cerr << cerrPrefix << "There is nothing there: " << existingAPIFilePath << endl;
               return 1;
            }
            ifile.close();
        } else {
            existingAPIFilePath = "";
        }

        if (_existingTypesFilePath) {
            existingTypesFilePath = _existingTypesFilePath;

            /* make sure that it exists */
            ifile.open(existingTypesFilePath.c_str());
            if(!ifile) {
               cerr << cerrPrefix << "Existing types description path seems invalid." << endl;
               cerr << cerrPrefix << "There is nothing there: " << existingTypesFilePath << endl;
               return 1;
            }
            ifile.close();
        } else {
            existingTypesFilePath = "";
        }

        instrumentationPath += selfPath.remove_filename();
        instrumentationPath += INSTRUMENTATION_NAME;

        symbolExtracterPath += selfPath.remove_filename();
        symbolExtracterPath += SYMBOL_EXTRACTER_NAME;

        symbolExtracter2Path += selfPath.remove_filename();
        symbolExtracter2Path += SYMBOL_EXTRACTER_NAME_V2;

        symbolAnalyzerPath += selfPath.remove_filename();
        symbolAnalyzerPath += SYMBOL_ANALYZER_NAME;

        staticAnalyzerPath += selfPath.remove_filename();
        staticAnalyzerPath += STATIC_ANALYZER_NAME;

        typeAnalyzerPath += selfPath.remove_filename();
        typeAnalyzerPath += TYPE_ANALYZER_NAME;

        typeAllAnalyzerPath += selfPath.remove_filename();
        typeAllAnalyzerPath += TYPE_ALL_ANALYZER_NAME;

        pinPath += selfPath.remove_filename();
        pinPath += "../pintools/pin";

        symbolsPath += SYMBOLS_FILE_PATH;
        typesPath   += TYPES_FILE_PATH;

        fifoMonitorPath += MONITOR_FIFO_PATH;
        fifoWorkerPath += WORKER_FIFO_PATH;

        fuzzingLogPath += WORKER_FUZZING_SEQ_LOG;

        /* generate regex that matches the lib's text mappings
         * (we do it only once and then simply reuse it) */
        std::ostringstream ss;
        ss << "r.xp .*(";
        for (auto it = libraryPaths.begin(); it != libraryPaths.end(); ++it) {
            auto libraryPath = *it;
            if (it != libraryPaths.begin()) {
                ss << "|";
            }
            /* note that we do not escape libraryPath... we live dangerously... */
            ss << "(" << libraryPath.filename().c_str() << ")";
        }
        ss << ").*";
        libTextMappingsRegex = std::regex(ss.str());
        ss.str("");

        cout << coutPrefix << "Performing sanity checks" << endl;

        /* if mappings tmp file exists, remove it */
        ifile.open(WORKER_MAPPINGS_COPY_PATH);
        if(ifile) {
            remove(WORKER_MAPPINGS_COPY_PATH);
        }
        ifile.close();

        ifile.open(WORKER_MAPPINGS_COPY_PATH_OLD);
        if(ifile) {
            remove(WORKER_MAPPINGS_COPY_PATH_OLD);
        }
        ifile.close();

        /* remove outdated cache files */
        ifile.open(WORKER_CALLBACK_FILE);
        if(ifile) {
            remove(WORKER_CALLBACK_FILE);
        }
        ifile.close();

        /* no need to bother too much with crash path if we're only analyzing the API */
        std::filesystem::path crashPath, infoPath;
        if (!ONLY_EXTRACT_API_ENABLED) {
            /* check presence of crash folder path */
            if (!std::filesystem::is_directory(baseCrashPath)) {
               cerr << cerrPrefix << "Could not find crash folder base path: " << baseCrashPath << endl;
               return 1;
            }

            /* make sure that crash directory is empty */
            crashPath += baseCrashPath;
            crashPath += "/crashes/";
            if (std::filesystem::is_directory(crashPath)) {
                cerr << cerrPrefix << "Crash directory already exists at: " << crashPath << endl;
                cerr << cerrPrefix << "Remove it and restart." << endl;
                return 1;
            }

            std::filesystem::create_directories(crashPath);

            infoPath = crashPath;
            infoPath += "session_info.txt";

            if (!std::filesystem::exists(infoPath)) {
                std::ofstream ofs(infoPath.c_str(), std::ios_base::app);

                ofs << "Fuzzing seed: " << seed << endl;

                struct tm* ptm = localtime(&seed);
                char cur_time[128];

                strftime(cur_time, 128, "%Y-%m-%d %H:%M:%S", ptm);
                ofs << "Starting time: " << cur_time << endl;
            }
        }

        /* check validity of workload script path */
        if (workloadScript) {
           ifile.open(workloadScriptPath.c_str());
           if(!ifile) {
              cerr << cerrPrefix << "Workload script path seems invalid." << endl;
              cerr << cerrPrefix << "There is nothing there: " << workloadScriptPath << endl;
              return 1;
           }
           ifile.close();
        }

        /* check presence of instrumentation */
        ifile.open(instrumentationPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find instrumentation, has this binary been moved?"
                << endl;
           cerr << cerrPrefix << "It should have been at " << instrumentationPath << endl;
           return 1;
        }
        ifile.close();

        /* check presence of libraries */
        std::list<std::filesystem::path> unionLibraryPaths;
        std::set_union(libraryPaths.begin(), libraryPaths.end(),
                              analysisLibraryPaths.begin(), analysisLibraryPaths.end(),
                              std::back_inserter(unionLibraryPaths));
        for (auto it = unionLibraryPaths.begin(); it != unionLibraryPaths.end(); ++it) {
            auto libraryPath = *it;
            ifile.open(libraryPath.c_str());
            if(!ifile) {
               cerr << cerrPrefix << "Passed shared library path looks invalid." << endl;
               cerr << cerrPrefix << "There is nothing here: " << libraryPath << endl;
               return 1;
            }
            ifile.close();
        }

        /* check presence of symbol tool */
        ifile.open(symbolExtracterPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find symbol extracter tool, has this binary been moved?"
                << endl;
           cerr << cerrPrefix << "It should have been at " << symbolExtracterPath << endl;
           return 1;
        }
        ifile.close();

        ifile.open(symbolExtracter2Path.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find symbol extracter tool v2, has this binary been moved?"
                << endl;
           cerr << cerrPrefix << "It should have been at " << symbolExtracter2Path << endl;
           return 1;
        }
        ifile.close();

        /* check presence of symbol tool */
        ifile.open(symbolAnalyzerPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find symbol analyzer tool, has this binary been moved?"
                << endl;
           cerr << cerrPrefix << "It should have been at " << symbolAnalyzerPath << endl;
           return 1;
        }
        ifile.close();

        /* check presence of static analysis tool */
        ifile.open(staticAnalyzerPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find static analyzer tool, has this binary been moved?"
                << endl;
           cerr << cerrPrefix << "It should have been at " << staticAnalyzerPath << endl;
           return 1;
        }
        ifile.close();

        /* check presence of full API type tool */
        ifile.open(typeAllAnalyzerPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find type analyzer tool, has this binary been moved?"
                << endl;
           cerr << cerrPrefix << "It should have been at " << typeAllAnalyzerPath << endl;
           return 1;
        }
        ifile.close();

        /* check presence of type tool */
        ifile.open(typeAnalyzerPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find type analyzer tool, has this binary been moved?"
                << endl;
           cerr << cerrPrefix << "It should have been at " << typeAnalyzerPath << endl;
           return 1;
        }
        ifile.close();

        /* check presence of pin */
        ifile.open(pinPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Could not find pin, has this binary been moved?" << endl;
           cerr << cerrPrefix << "It should have been at " << pinPath << endl;
           return 1;
        }
        ifile.close();

        /* check presence of app */
        ifile.open(appPath.c_str());
        if(!ifile) {
           cerr << cerrPrefix << "Passed application path looks invalid." << endl;
           cerr << cerrPrefix << "There is nothing here: " << appPath << endl;
           return 1;
        }
        ifile.close();

        /* pre-populate application argv */
        appArgv = (char **) malloc(sizeof(char *) *
                (21 + argc /* app arguments (more than necessary) */
                    + libraryPaths.size() * 2 /* library paths */
                    + 1 /* NULL char */));

        appArgv[0]  = const_cast<char*>(pinPath.c_str());
        appArgv[1]  = const_cast<char*>("-t");
        appArgv[2]  = const_cast<char*>(instrumentationPath.c_str());
        appArgv[3]  = const_cast<char*>("-symbols");
        appArgv[4]  = const_cast<char*>(symbolsPath.c_str());
        appArgv[5]  = const_cast<char*>("-symboltool");
        appArgv[6]  = const_cast<char*>(symbolAnalyzerPath.c_str());
        appArgv[7]  = const_cast<char*>("-typetool");
        appArgv[8]  = const_cast<char*>(typeAnalyzerPath.c_str());
        appArgv[9]  = const_cast<char*>("-fifoMonitor");
        appArgv[10] = const_cast<char*>(fifoMonitorPath.c_str());
        appArgv[11] = const_cast<char*>("-fifoWorker");
        appArgv[12] = const_cast<char*>(fifoWorkerPath.c_str());
        appArgv[13] = const_cast<char*>("-o");
        appArgv[14] = const_cast<char*>(fuzzingLogPath.c_str());
        appArgv[15]  = const_cast<char*>("-typesPath");
        appArgv[16]  = const_cast<char*>(typesPath.c_str());

	/* this is a legacy argument, we don't really need it anymore. but
	 * since removing it is error prone, we just leave it always on. */
        appArgv[17] = const_cast<char*>("-instrRetCB");
        appArgv[18] = const_cast<char*>("1");

        appArgv[19] = const_cast<char*>("-Verbose");
        if (HEAVY_DEBUG_ENABLED) {
            appArgv[20] = const_cast<char*>("1");
        } else {
            appArgv[20] = const_cast<char*>("0");
        }

        c = 0;
        for (auto it = libraryPaths.begin(); it != libraryPaths.end(); ++it) {
            c++;
            appArgv[20 + c] = const_cast<char*>("-libPath");
            c++;
            appArgv[20 + c] = const_cast<char*>(it->c_str());
        }
        appArgv[21 + c] = const_cast<char*>("--");
        appArgv[22 + c] = const_cast<char*>(appPath.c_str());

        for (int i = 0; i < argc - (optind + 1 + libNum); i++) {
            appArgv[23 + i + c] = argv[optind + 1 + libNum + i];
        }

        appArgv[23 + argc - (optind + 1 + libNum) + c] = NULL;

	if (HEAVY_DEBUG_ENABLED) {
           cout << "[+] Instrumented app command:";
	   for (int i = 0; appArgv[i] != NULL; i++) {
              cout << " " << appArgv[i];
	   }
	   cout << std::endl;
	}

        cout << coutPrefix << "Sanity-checking binaries..." << endl;

        /* check that the application was compiled with ASan */
        ss << "objdump -TC " << appPath.string() << " | grep __asan_init > "
           << SYMBOLS_FILE_PATH;
        system(ss.str().c_str());
        ss.str("");

        std::ifstream in_file2(SYMBOLS_FILE_PATH, std::ifstream::binary);
        in_file2.seekg(0, std::ifstream::end);
        if (!in_file2.tellg()) {
           cerr << cerrPrefix << "Cannot detect ASan on this binary, have you compiled it with -fsanitize=address?" << endl;
           return 1;
        }

        /* check that application was built with debug symbols */
        ss << "file $(readlink -f " << appPath.string() << ") | grep \"with debug_info\" > " << SYMBOLS_FILE_PATH;
        system(ss.str().c_str());
        ss.str("");

        std::ifstream in_file3(SYMBOLS_FILE_PATH, std::ifstream::binary);
        in_file3.seekg(0, std::ifstream::end);
        if (!in_file3.tellg()) {
           cerr << cerrPrefix << "Cannot detect debug symbols on this application, have you compiled it with -g?" << endl;
           return 1;
        }

        /* check that library was built with debug symbols */
        for (auto it = libraryPaths.begin(); it != libraryPaths.end(); ++it) {
            auto libraryPath = *it;
            ss << "file $(readlink -f " << libraryPath.string()
               << ") | grep \"with debug_info\" > " << SYMBOLS_FILE_PATH;
            system(ss.str().c_str());
            ss.str("");
        }

        std::ifstream in_file4(SYMBOLS_FILE_PATH, std::ifstream::binary);
        in_file4.seekg(0, std::ifstream::end);
        if (!in_file4.tellg()) {
           cerr << cerrPrefix << "Cannot detect debug symbols on this library, have "
                << "you compiled it with -g?" << endl;
           return 1;
        }

        if (existingAPIFilePath == "") {
            cout << coutPrefix << "Retrieving library symbols (can take a bit of time)" << endl;

            /* retrieve library symbols */
            remove(SYMBOLS_FILE_PATH); /* in case the symbol detection program fails */
            for (auto it = libraryPaths.begin(); it != libraryPaths.end(); ++it) {
                auto libraryPath = *it;
                if (!USE_SE_V2)
                  ss << symbolExtracterPath.string() << " '" << regex << "' "
                     << libraryPath.string() << " >> " << SYMBOLS_FILE_PATH;
                else
                  ss << symbolExtracter2Path.string() << " '" << regex << "' "
                     << libraryPath.string() << " >> " << SYMBOLS_FILE_PATH;
                system(ss.str().c_str());
                ss.str("");
            }
        } else {
            cout << coutPrefix << "Retrieving library symbols from " << existingAPIFilePath << endl;
            copyFile(existingAPIFilePath.c_str(), symbolsPath.c_str());
        }

        /* sanity checks the results */
        std::ifstream in_file(SYMBOLS_FILE_PATH, std::ifstream::binary);
        in_file.seekg(0, std::ifstream::end);
        if (!in_file.tellg()) {
            if (existingAPIFilePath == "") {
                for (auto it = libraryPaths.begin(); it != libraryPaths.end(); ++it) {
                    auto libraryPath = *it;

                    /* check if this is due to the regex */
                    remove(SYMBOLS_FILE_PATH);
                    ss.str("");
                    ss << "objdump -T " << libraryPath.string() << " | grep \"\\.text\" "
                       << " > " << SYMBOLS_FILE_PATH;
                    system(ss.str().c_str());
                    ss.str("");

                    std::ifstream in_file2(SYMBOLS_FILE_PATH, std::ifstream::binary);
                    in_file2.seekg(0, std::ifstream::end);
                    if (!in_file2.tellg()) {
                        cerr << cerrPrefix << "No symbols detected in " << libraryPath
                             << ". Is this even a shared lib?" << endl;
                    } else {
                        cerr << cerrPrefix << "Passed regex did not match any symbols in "
                             << libraryPath << "." << endl;
                    }
                }
            } else {
                cerr << cerrPrefix << "Passed API description looks invalid." << endl;
            }

            return 1;
        }

        if (existingAPIFilePath == "") {
            cout << coutPrefix << "Retrieving symbol type information (can take a bit of time)" << endl;
            ss.str("");
            ss << typeAllAnalyzerPath.string() << " " << typesPath.string() << " " << SYMBOLS_FILE_PATH;
            /* this time we consider libraries passed via -L as well */
            for (auto it = unionLibraryPaths.begin(); it != unionLibraryPaths.end(); ++it) {
                ss << " " << *it;
            }

            /* discard the output */
            ss << " > /dev/null 2>&1";

            if (HEAVY_DEBUG_ENABLED)
                cout << coutPrefix << " Type analysis command: " << ss.str() << endl;

            system(ss.str().c_str());
            ss.str("");
        } else {
            cout << coutPrefix << "Retrieving type information from " << existingTypesFilePath << endl;
            copyFile(existingTypesFilePath.c_str(), typesPath.c_str());
        }

        /* don't actually fuzz if everything we were ask to do is generating the API */
        if (ONLY_EXTRACT_API_ENABLED)
            return 0;

        /* make a backup of instrumented functions */
        std::filesystem::path funcBackupPath = crashPath;
        funcBackupPath += "instrumented_functions.txt";
        std::filesystem::copy(symbolsPath, funcBackupPath);

        std::ifstream inFile(SYMBOLS_FILE_PATH);
        int lc = std::count(std::istreambuf_iterator<char>(inFile),
             std::istreambuf_iterator<char>(), '\n');
        std::ofstream ofs(infoPath.c_str(), std::ios_base::app);
        ofs << "Total instrumented API size: " << lc << endl;

        cout << coutPrefix << "Registering handlers" << endl;

        /* Install signal handler to detect child death.
         * NOTE: don't install it earlier, we're not interested in signals for
         * system() calls */
        struct sigaction sa;
        sa.sa_sigaction = SIGCHLDhandler;
        sigemptyset(&sa.sa_mask);
        /* SA_SIGINFO   = the handler should get a siginfo_t *
         * SA_NOCLDSTOP = we're only interested in children dying
         * SA_NOCLDWAIT = always reap children automatically
         * SA_RESTART   = automatically restart interruptible functions,
         *                don't return EINTR */
        sa.sa_flags = SA_SIGINFO | SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
        if (sigaction(SIGCHLD, &sa, NULL) == -1) {
            cerr << cerrPrefix << "Failed to install SIGCHLD handler" << endl;
            exit(1);
        }

        /* to register fuzzing end date */
        signal(SIGINT, SIGINThandler);

        cout << coutPrefix << "All done. Ready to fuzz!" << endl << endl;

        fuzzingLoop(iterations);

        cout << coutPrefix << "Done fuzzing. Exiting." << endl << endl;

        endSession();

        remove(fifoMonitorPath.c_str());
        remove(fifoWorkerPath.c_str());

        return 0;
}
