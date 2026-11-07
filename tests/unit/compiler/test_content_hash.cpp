#include <catch2/catch_test_macros.hpp>
#include "compiler/content_hash.h"
#include "compiler/file_utils.h"
#include <filesystem>
#include <fstream>
#include <string>

// plan.md §6.34 S1: a content hash that is the same in every process and
// every build (std::hash promises neither), so a stored hash still means
// something after a restart or in another svlsp. SHA-256: the FIPS 180-2
// test vectors pin it.

TEST_CASE("contentHash is SHA-256 (FIPS 180-2 vectors)", "[compiler][content-hash]") {
    CHECK(contentHash("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(contentHash("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(contentHash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(contentHash(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("contentHash differs for inputs around the 64-byte block edge",
          "[compiler][content-hash]") {
    // 55, 56 and 64 bytes: the padding's length field fits, just doesn't,
    // and a whole block.
    const auto h55 = contentHash(std::string(55, 'x'));
    const auto h56 = contentHash(std::string(56, 'x'));
    const auto h64 = contentHash(std::string(64, 'x'));
    CHECK(h55 != h56);
    CHECK(h56 != h64);
    CHECK(h55.size() == 64);
}

TEST_CASE("statFile reports a file's size and modification time", "[compiler][content-hash]") {
    const std::string path = "/tmp/svlsp_test_stat_file.sv";
    { std::ofstream f(path); f << "module m; endmodule\n"; }
    const auto st = statFile(path);
    REQUIRE(st);
    CHECK(st->size == 20);
    CHECK(st->mtimeNs > 0);
    CHECK(!statFile("/tmp/svlsp_test_no_such_file.sv"));
}
