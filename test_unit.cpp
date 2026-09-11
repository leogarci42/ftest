#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "helpers.h"
#include "commands.h"
}

#include <ftest/ftest.hpp>

namespace {

using namespace ftest;

using HashFn = int (*)(uint8_t, int, char*);

std::string hash_string(HashFn fn, const std::string& input)
{
        std::string owned = input;
        return capture_stdout([&] { fn(Q_FLAGS, -1, owned.data()); });
}

std::string hash_file(HashFn fn, const std::string& path)
{
        FileDescriptor file(path);
        std::string owned_path = path;
        return capture_stdout(
                [&] { fn(Q_FLAGS, file.get(), owned_path.data()); });
}

struct HashRun
{
        int         rc;
        std::string output;
};

HashRun hash_fd_checked(HashFn fn, int fd, const std::string& label)
{
        std::string owned_path = label.empty() ? "(fd)" : label;
        OutputCapture capture(STDOUT_FILENO, stdout);
        int rc = fn(Q_FLAGS, fd, owned_path.data());
        return {rc, capture.content()};
}

std::string digest_line(const char* hex)
{
        return std::string(hex) + "\n";
}

struct BlockVector
{
        size_t      len;
        const char* md5_hex;
        const char* sha256_hex;
};

const std::vector<BlockVector>& block_edge_vectors()
{
        static const std::vector<BlockVector> vectors = {
                { 0,    "d41d8cd98f00b204e9800998ecf8427e",
                        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
                { 1,    "0cc175b9c0f1b6a831c399e269772661",
                        "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" },
                { 3,    "47bce5c74f589f4867dbd57e9ca9f808",
                        "9834876dcfb05cb167a5c24953eba58c4ac89b1adf57f28f2f9d09af107ee8f0" },
                { 55,   "ef1772b6dff9a122358552954ad0df65",
                        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
                { 56,   "3b0c8ac703f828b04c6c197006d17218",
                        "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
                { 63,   "b06521f39153d618550606be297466d5",
                        "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" },
                { 64,   "014842d480b571495a4a0363793f7367",
                        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
                { 65,   "c743a45e0d2e6a95cb859adae0248435",
                        "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" },
                { 119,  "8a7bd0732ed6a28ce75f6dabc90e1613",
                        "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb" },
                { 120,  "5f61c0ccad4cac44c75ff505e1f1e537",
                        "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c" },
                { 1000, "cabe45dcc9ae5b66ba86600cca6b8ba8",
                        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3" },
        };
        return vectors;
}

} // namespace

int main()
{
        ::mkdir("/tmp/opencode", 0755);

        TempFile seed("/tmp/opencode/ft_ssl_unit_seed.bin", std::string(1000, 'a'));
        TempFile binary("/tmp/opencode/ft_ssl_unit_binary.bin", [] {
                std::string data;
                for (int i = 0; i < 765; ++i)
                        data.push_back(static_cast<char>((i % 255) + 1));
                return data;
        }());

        Suite md5_suite("MD5 reference vectors");
        md5_suite.add("empty string digest", [] {
                require_equal(hash_string(md5, ""),
                              digest_line("d41d8cd98f00b204e9800998ecf8427e"),
                              "md5(\"\")");
        });
        md5_suite.add("single character 'a'", [] {
                require_equal(hash_string(md5, "a"),
                              digest_line("0cc175b9c0f1b6a831c399e269772661"),
                              "md5(\"a\")");
        });
        md5_suite.add("classic 'abc'", [] {
                require_equal(hash_string(md5, "abc"),
                              digest_line("900150983cd24fb0d6963f7d28e17f72"),
                              "md5(\"abc\")");
        });
        md5_suite.add("ascii sentence", [] {
                require_equal(hash_string(md5, "hello world"),
                              digest_line("5eb63bbbe01eeed093cb22bb8f5acdc3"),
                              "md5(\"hello world\")");
        });
        md5_suite.add("quick brown fox sentence", [] {
                require_equal(hash_string(md5,
                                          "The quick brown fox jumps over the lazy dog"),
                              digest_line("9e107d9d372bb6826bd81d3542a419d6"),
                              "md5 of quick brown fox");
        });
        md5_suite.add("block boundary lengths (55..120, 1000)", [] {
                for (const BlockVector& v : block_edge_vectors())
                        require_equal(hash_string(md5, std::string(v.len, 'a')),
                                      digest_line(v.md5_hex),
                                      "md5('a' x " + std::to_string(v.len) + ")");
        });
        md5_suite.add("binary bytes without NUL (765 bytes)", [&] {
                require_equal(hash_string(md5, [&] {
                                      std::string data;
                                      for (int i = 0; i < 765; ++i)
                                              data.push_back(static_cast<char>(
                                                      (i % 255) + 1));
                                      return data;
                              }()),
                              digest_line("299d61afdd379414503eef23b3b41121"),
                              "md5 of non-zero byte pattern x3");
        });
        md5_suite.add("one million characters (stress)", [] {
                require_equal(hash_string(md5, std::string(1000000, 'a')),
                              digest_line("7707d6ae4e027c70eea2a935c2296f21"),
                              "md5('a' x 1000000)");
        });

        Suite sha256_suite("SHA256 reference vectors");
        sha256_suite.add("empty string digest", [] {
                require_equal(hash_string(sha256, ""),
                              digest_line("e3b0c44298fc1c149afbf4c8996fb92427"
                                          "ae41e4649b934ca495991b7852b855"),
                              "sha256(\"\")");
        });
        sha256_suite.add("classic 'abc'", [] {
                require_equal(hash_string(sha256, "abc"),
                              digest_line("ba7816bf8f01cfea414140de5dae2223b0"
                                          "0361a396177a9cb410ff61f20015ad"),
                              "sha256(\"abc\")");
        });
        sha256_suite.add("NIST multi-block vector", [] {
                require_equal(hash_string(sha256,
                                          "abcdbcdecdefdefgefghfghighijhijkij"
                                          "kljklmklmnlmnomnopnopq"),
                              digest_line("248d6a61d20638b8e5c026930c3e6039a3"
                                          "3ce45964ff2167f6ecedd419db06c1"),
                              "sha256 NIST vector");
        });
        sha256_suite.add("ascii sentence", [] {
                require_equal(hash_string(sha256, "hello world"),
                              digest_line("b94d27b9934d3e08a52e52d7da7dabfa"
                                          "c484efe37a5380ee9088f7ace2efcde9"),
                              "sha256(\"hello world\")");
        });
        sha256_suite.add("block boundary lengths (55..120, 1000)", [] {
                for (const BlockVector& v : block_edge_vectors())
                        require_equal(hash_string(sha256, std::string(v.len, 'a')),
                                      digest_line(v.sha256_hex),
                                      "sha256('a' x "
                                              + std::to_string(v.len) + ")");
        });
        sha256_suite.add("binary bytes without NUL (765 bytes)", [] {
                std::string data;
                for (int i = 0; i < 765; ++i)
                        data.push_back(static_cast<char>((i % 255) + 1));
                require_equal(hash_string(sha256, data),
                              digest_line("c1ece5c6ff88943e2d5d2968d96c2322f"
                                          "c3fcf175bc5b1727a23494519b46580"),
                              "sha256 of non-zero byte pattern x3");
        });
        sha256_suite.add("one million characters (stress)", [] {
                require_equal(hash_string(sha256, std::string(1000000, 'a')),
                              digest_line("cdc76e5c9914fb9281a1c7e284d73e67f1"
                                          "809a48a497200e046d39ccc7112cd0"),
                              "sha256('a' x 1000000)");
        });

        Suite files("File descriptor mode");
        files.add("md5 hashes full file content", [&] {
                require_equal(hash_file(md5, seed.path()),
                              digest_line("cabe45dcc9ae5b66ba86600cca6b8ba8"),
                              "file mode md5 ('a' x 1000)");
        });
        files.add("sha256 hashes full file content", [&] {
                require_equal(hash_file(sha256, seed.path()),
                              digest_line("41edece42d63e8d9bf515a9ba6932e1c20"
                                          "cbc9f5a5d134645adb5db1b9737ea3"),
                              "file mode sha256 ('a' x 1000)");
        });
        files.add("binary content is not mangled by buffering", [&] {
                require_equal(hash_file(md5, binary.path()),
                              digest_line("299d61afdd379414503eef23b3b41121"),
                              "file mode md5 binary pattern");
        });

        Suite helpers_suite("Helpers (ft_strncmp / ft_putstr_fd)");
        helpers_suite.add("n=0 always compares equal", [] {
                require(ft_strncmp("abc", "xyz", 0) == 0,
                        "ft_strncmp must return 0 when n=0");
        });
        helpers_suite.add("identical strings compare equal", [] {
                require(ft_strncmp("abc", "abc", 3) == 0,
                        "ft_strncmp equal strings must return 0");
        });
        helpers_suite.add("only first n bytes are compared", [] {
                require(ft_strncmp("abc", "abd", 2) == 0,
                        "ft_strncmp must compare only n bytes");
        });
        helpers_suite.add("comparison is unsigned on high-bit chars", [] {
                require(ft_strncmp("\x80", "\x01", 1) > 0,
                        "ft_strncmp must compare as unsigned char");
        });
        helpers_suite.add("ft_putstr_fd writes exact bytes", [] {
                require_equal(capture_stdout([] {
                                      ft_putstr_fd(const_cast<char*>("hello"), 1);
                              }),
                              "hello", "ft_putstr_fd output");
        });

        Suite resources("Resource & leak checks");
        resources.add("no descriptor leaks across the whole hashing pass", [&] {
                FdLeakGuard guard("unit hashing session");
                hash_string(md5, "leak probe");
                hash_file(sha256, seed.path());
                hash_file(md5, binary.path());
                guard.require_clean();
        });
        resources.add("no heap growth while hashing 200 KB in-process", [] {
                AllocGuard guard("hashing 200 KB");
                hash_string(sha256, std::string(200000, 'a'));
                guard.require_no_heap_growth();
        });
        resources.add("/dev/null input yields empty digest", [] {
                HashRun run = hash_fd_checked(md5, open("/dev/null", O_RDONLY),
                                              "/dev/null");
                require_equal(run.output,
                              digest_line("d41d8cd98f00b204e9800998ecf8427e"),
                              "md5 of /dev/null");
                require(run.rc == 0, "md5 must report success on /dev/null");
        });
        resources.add("directory fd hits read-error path without crashing", [] {
                int fd = open("/tmp", O_RDONLY);
                require(fd >= 0, "cannot open directory for read-error test");
                HashRun run = hash_fd_checked(md5, fd, "/tmp");
                close(fd);
                require(run.rc != 0,
                        "md5 must report failure when read() errors (EISDIR)");
                require(run.output.empty(),
                        "no digest may be printed when the read fails");
        });
        resources.add("file mode and string mode agree on 1 MB input", [&] {
                TempFile big("/tmp/opencode/ft_ssl_unit_big.bin",
                             std::string(1000000, 'a'));
                require_equal(hash_file(sha256, big.path()),
                              hash_string(sha256, std::string(1000000, 'a')),
                              "sha256 file vs string on 1 MB");
        });
        resources.add("repeated hashing in one process is stable", [] {
                std::string first = hash_string(md5, "determinism check");
                for (int i = 0; i < 8; ++i)
                        require_equal(hash_string(md5, "determinism check"),
                                      first,
                                      "md5 repeat #" + std::to_string(i));
        });
        resources.add("stdout capture restores real stdout between runs", [] {
                require_equal(hash_string(md5, "a"),
                              digest_line("0cc175b9c0f1b6a831c399e269772661"),
                              "first capture");
                require_equal(hash_string(md5, "abc"),
                              digest_line("900150983cd24fb0d6963f7d28e17f72"),
                              "second capture after restore");
        });
        resources.add("many sequential file fds do not corrupt state", [&] {
                TempFile small("/tmp/opencode/ft_ssl_unit_fd.bin", "abc");
                for (int i = 0; i < 32; ++i)
                        require_equal(
                                hash_file(sha256, small.path()),
                                digest_line("ba7816bf8f01cfea414140de5dae2223b0"
                                            "0361a396177a9cb410ff61f20015ad"),
                                "sha256 iteration #" + std::to_string(i));
        });

        Runner runner;
        runner.add(std::move(md5_suite));
        runner.add(std::move(sha256_suite));
        runner.add(std::move(files));
        runner.add(std::move(helpers_suite));
        runner.add(std::move(resources));

        return runner.run_all();
}
