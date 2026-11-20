// plan.md §6.40 D6: one canonical name per file inside svlsp, and the
// client's own name for it in every URI sent back.
#include <catch2/catch_test_macros.hpp>
#include "compiler/path_names.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct SymlinkTree {
    const fs::path root = "/tmp/svlsp_test_path_names";
    SymlinkTree()
    {
        fs::remove_all(root);
        fs::create_directories(root / "real" / "sub");
        std::ofstream(root / "real" / "a.sv") << "\n";
        std::ofstream(root / "real" / "sub" / "b.svh") << "\n";
        fs::create_directory_symlink(root / "real", root / "link");
        fs::create_symlink(root / "real" / "a.sv", root / "file_link.sv");
        pathnames::clearClientNames();
    }
    ~SymlinkTree()
    {
        pathnames::clearClientNames();
        fs::remove_all(root);
    }
    std::string p(const std::string& rel) const { return (root / rel).string(); }
};

} // namespace

TEST_CASE("canonical resolves symlinks, `.` and `..`, and keeps a missing tail",
          "[path_names][symlink]")
{
    SymlinkTree t;
    CHECK(pathnames::canonical(t.p("link/a.sv")) == t.p("real/a.sv"));
    CHECK(pathnames::canonical(t.p("file_link.sv")) == t.p("real/a.sv"));
    CHECK(pathnames::canonical(t.p("link/sub/../a.sv")) == t.p("real/a.sv"));
    CHECK(pathnames::canonical(t.p("real/./sub/b.svh")) == t.p("real/sub/b.svh"));
    CHECK(pathnames::canonical(t.p("link/")) == t.p("real"));
    // Not there (yet): the existing part resolved, the rest normalized.
    CHECK(pathnames::canonical(t.p("link/new/../x.db")) == t.p("real/x.db"));
    CHECK(pathnames::canonical("/no/such/dir/../f.sv") == "/no/such/f.sv");
}

TEST_CASE("a client name maps canonical paths back: the file, then the deepest directory",
          "[path_names][symlink]")
{
    SymlinkTree t;
    // Nothing recorded: a path is its own client name.
    CHECK(pathnames::toClient(t.p("real/a.sv")) == t.p("real/a.sv"));

    CHECK(pathnames::fromClient(t.p("link/a.sv")) == t.p("real/a.sv"));
    CHECK(pathnames::toClient(t.p("real/a.sv")) == t.p("link/a.sv"));
    // A file the client never named, under a directory it did.
    CHECK(pathnames::toClient(t.p("real/sub/b.svh")) == t.p("link/sub/b.svh"));
    // Outside every recorded directory: unchanged.
    CHECK(pathnames::toClient(t.p("other/c.sv")) == t.p("other/c.sv"));

    // A client name without symlinks records nothing new.
    CHECK(pathnames::fromClient(t.p("real/sub/b.svh")) == t.p("real/sub/b.svh"));
    CHECK(pathnames::toClient(t.p("real/sub/b.svh")) == t.p("link/sub/b.svh"));

    // The file itself a symlink: only the file maps back.
    pathnames::clearClientNames();
    CHECK(pathnames::fromClient(t.p("file_link.sv")) == t.p("real/a.sv"));
    CHECK(pathnames::toClient(t.p("real/a.sv")) == t.p("file_link.sv"));
    CHECK(pathnames::toClient(t.p("real/sub/b.svh")) == t.p("real/sub/b.svh"));
}
