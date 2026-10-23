#pragma once
#include "compiler/sv_tree_walker.h"
#include <cstddef>
#include <iosfwd>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <vector>

// The parse in a worker process (plan.md §6.33 C1).
//
// ANTLR keeps everything a parse learns -- the DFA and the prediction-
// context cache -- in process-wide statics that are never given back: a
// server that compiled UVM live keeps ~6.6 GB for life. A ParseWorker runs
// SvTreeWalker::walk in a child process instead (the svlsp binary itself,
// `svlsp --internal-parse-worker`), so the server never parses. The child
// stays alive across compiles, keeping its DFA warm for the small edits
// that follow, and is killed once its resident memory is over
// `maxRssMb` after a job, which gives the memory back; the next walk
// starts a fresh one.
//
// The input is what the walk takes (preprocessed source, its SourceLine
// map, collectDocs, the initial scope) and the output a WalkResult, framed
// over a socketpair (encodeWalkRequest/encodeWalkResult below).

struct WalkRequest {
    std::string             source;
    std::vector<SourceLine> sourceMap;
    bool                    collectDocs{true};
    std::string             initialScope;
};

// The wire format: a flat little-endian encoding of each field in
// declaration order. decode* return false on a truncated or malformed
// buffer.
std::string encodeWalkRequest(const WalkRequest& req);
bool        decodeWalkRequest(const std::string& buf, WalkRequest& out);
std::string encodeWalkResult(const WalkResult& result);
bool        decodeWalkResult(const std::string& buf, WalkResult& out);

class ParseWorker {
public:
    struct Options {
        std::string   exe{"/proc/self/exe"}; // a binary that runs runParseWorker
                                             // for `--internal-parse-worker`
        std::size_t   maxRssMb{1024};        // recycle above this after a job;
                                             // 0 = never
        std::ostream* log{nullptr};          // "[worker] ..." lines (--log-files)
    };

    explicit ParseWorker(Options options);
    ~ParseWorker();
    ParseWorker(const ParseWorker&)            = delete;
    ParseWorker& operator=(const ParseWorker&) = delete;

    // SvTreeWalker::walk in the worker. A worker that dies mid-job (a
    // crash, the OOM killer) is restarted and the job retried once; if that
    // fails too, the result is a single error diagnostic on line 1.
    WalkResult walk(const std::string& source, const std::vector<SourceLine>& sourceMap,
                    bool collectDocs, const std::string& initialScope);

    // The running worker's pid, -1 when none (started lazily, by walk).
    pid_t pid() const;

private:
    Options            m_options;
    mutable std::mutex m_mutex;
    pid_t              m_pid{-1};
    int                m_fd{-1};

    bool start();
    void stop();
    bool roundTrip(const std::string& request, std::string& response);
    void log(const std::string& line) const;
};

// The worker's side: reads framed requests from `fd` until EOF, walks each
// and writes the framed result back to `fd`. Returns the exit code.
int runParseWorker(int fd);
