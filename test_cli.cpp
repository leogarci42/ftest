#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <utility>
#include <vector>

#include <ftest/ftest.hpp>

namespace {

using namespace ftest;

void require_no_crash(const std::string& label, const RunResult& result)
{
        if (result.timed_out || WIFSIGNALED(result.status))
                throw TestFailure(label + ": " + describe_exit(result));
}

void require_output(const std::string& label, const RunResult& result,
                    const std::string& expected)
{
        require_no_crash(label, result);
        require_equal(result.output, expected + "\n", label);
}

void require_exit_code(const std::string& label, const RunResult& result,
                       int expected_code)
{
        require_no_crash(label, result);
        require(WIFEXITED(result.status)
                        && WEXITSTATUS(result.status) == expected_code,
                label + ": expected exit code "
                        + std::to_string(expected_code) + ", got "
                        + describe_exit(result));
}

std::string locate_ft_ssl()
{
        for (const std::string& candidate : {"./ft_ssl", "../ft_ssl"})
        {
                if (access(candidate.c_str(), X_OK) == 0)
                        return candidate;
        }
        throw std::runtime_error(
                "cannot find the ft_ssl binary (looked in . and ..)");
}

} // namespace

int main()
{
        try
        {
                ::mkdir("/tmp/opencode", 0755);

                CommandRunner ft_ssl(locate_ft_ssl());
                TempFile seed("/tmp/opencode/ft_ssl_cli_seed.bin", "abc");

                Suite crashes("Crash regression series");
                crashes.add("no arguments exits cleanly",
                            [&] { require_no_crash("no arguments", ft_ssl.run({})); });
                crashes.add("flags only (-q) reads stdin instead of crashing",
                            [&] { require_no_crash("md5 -q",
                                                   ft_ssl.run({"md5", "-q"})); });
                crashes.add("multiple flags without file are safe",
                            [&] { require_no_crash("sha256 -q -r",
                                                   ft_ssl.run({"sha256", "-q", "-r"})); });
                crashes.add("-s missing argument does not segfault",
                            [&] { require_no_crash("md5 -s",
                                                   ft_ssl.run({"md5", "-s"})); });
                crashes.add("unknown command is rejected safely",
                            [&] { require_no_crash("bogus command",
                                                   ft_ssl.run({"bogus"})); });
                crashes.add("empty command name is rejected safely",
                            [&] { require_no_crash("empty name", ft_ssl.run({""})); });
                crashes.add("over-long command bypasses switch safely",
                            [&] { require_no_crash("long invalid name",
                                                   ft_ssl.run({std::string(64, 'x')})); });

                Suite errors("Error reporting series");
                errors.add("usage line printed when no command given", [&] {
                        require_contains(ft_ssl.run({}).output, "usage:",
                                         "no arguments usage");
                });
                errors.add("invalid command prints error banner", [&] {
                        require_contains(ft_ssl.run({"notacommand"}).output,
                                         "is an invalid command",
                                         "invalid command message");
                });
                errors.add("missing file reports open failure", [&] {
                        require_contains(
                                ft_ssl.run(
                                        {"md5",
                                         "/tmp/opencode/definitely_missing_file"})
                                        .output,
                                "No such file or directory",
                                "missing file message");
                });
                errors.add("unknown command exits with code 2", [&] {
                        require_exit_code("exit status for invalid command",
                                          ft_ssl.run({"notacommand"}), 2);
                });

                Suite golden("Golden output series");
                golden.add("md5 -q -s matches reference digest", [&] {
                        require_output("md5 -q -s abc",
                                       ft_ssl.run({"md5", "-q", "-s", "abc"}),
                                       "900150983cd24fb0d6963f7d28e17f72");
                });
                golden.add("sha256 -q -s matches reference digest", [&] {
                        require_output("sha256 -q -s abc",
                                       ft_ssl.run({"sha256", "-q", "-s", "abc"}),
                                       "ba7816bf8f01cfea414140de5dae2223b00"
                                       "361a396177a9cb410ff61f20015ad");
                });
                golden.add("md5 of empty string", [&] {
                        require_output("md5 -q -s \"\"",
                                       ft_ssl.run({"md5", "-q", "-s", ""}),
                                       "d41d8cd98f00b204e9800998ecf8427e");
                });
                golden.add("md5 file mode with -r prints 'hash file'", [&] {
                        require_output("md5 -r " + seed.path(),
                                       ft_ssl.run({"md5", "-r", seed.path()}),
                                       "900150983cd24fb0d6963f7d28e17f72 "
                                               + seed.path());
                });
                golden.add("sha256 file mode with -r prints 'hash file'", [&] {
                        require_output("sha256 -r " + seed.path(),
                                       ft_ssl.run({"sha256", "-r", seed.path()}),
                                       "ba7816bf8f01cfea414140de5dae2223b00"
                                       "361a396177a9cb410ff61f20015ad "
                                               + seed.path());
                });
                golden.add("-p echoes input then labeled md5 hash", [&] {
                        require_output("printf abc | md5 -p",
                                       ft_ssl.run({"md5", "-p"}, "abc"),
                                       "abcMD5 ((stdin)) = 900150983cd24fb0d"
                                       "6963f7d28e17f72");
                });
                golden.add("-p echoes input then labeled sha256 hash", [&] {
                        require_output("printf abc | sha256 -p",
                                       ft_ssl.run({"sha256", "-p"}, "abc"),
                                       "abcSHA256 ((stdin)) = ba7816bf8f01cf"
                                       "ea414140de5dae2223b00361a396177a9cb4"
                                       "10ff61f20015ad");
                });
                golden.add("quiet mode reads empty stdin as digest input", [&] {
                        require_output("printf '' | md5 -q",
                                       ft_ssl.run({"md5", "-q"}, ""),
                                       "d41d8cd98f00b204e9800998ecf8427e");
                });
                golden.add("-qp hashes piped input once (no double hashing)",
                           [&] {
                                   require_output("printf abc | md5 -qp",
                                                  ft_ssl.run({"md5", "-qp"}, "abc"),
                                                  "abc900150983cd24fb0d6963f7d2"
                                                  "8e17f72");
                           });
                golden.add("-q on file prints bare digest only", [&] {
                        require_output("md5 -qr " + seed.path(),
                                       ft_ssl.run({"md5", "-q", "-r", seed.path()}),
                                       "900150983cd24fb0d6963f7d28e17f72");
                });

                const std::string big_path =
                        "/tmp/opencode/ft_ssl_cli_big.bin";
                {
                        std::ofstream out(big_path,
                                          std::ios::binary | std::ios::trunc);
                        out << std::string(1000000, 'a');
                }
                const std::string empty_path =
                        "/tmp/opencode/ft_ssl_cli_empty.bin";
                {
                        std::ofstream out(empty_path,
                                          std::ios::binary | std::ios::trunc);
                }
                TempFile denied([] {
                        const std::string path =
                                "/tmp/opencode/ft_ssl_cli_denied.bin";
                        chmod(path.c_str(), 0644);
                        std::remove(path.c_str());
                        return path;
                }(), "secret");
                ::chmod(denied.path().c_str(), 0000);

                Suite fd_stress("Fd & resource stress series");
                fd_stress.add("20 sequential runs are deterministic", [&] {
                        for (int i = 0; i < 20; ++i)
                                require_output("determinism run #"
                                                       + std::to_string(i),
                                               ft_ssl.run({"md5", "-q", "-s", "abc"}),
                                               "900150983cd24fb0d6963f7d28e17f72");
                });
                fd_stress.add("runs under a tight fd limit (RLIMIT_NOFILE=16)",
                              [&] {
                                      require_output(
                                              "sha256 file with low fd limit",
                                              ft_ssl.run(
                                                      {"sha256", "-q", "-r",
                                                       seed.path()},
                                                      "", 5000, 16),
                                              "ba7816bf8f01cfea414140de5dae222"
                                              "3b00361a396177a9cb410ff61f200"
                                              "15ad");
                              });
                fd_stress.add("200 KB piped stdin survives backpressure (md5)",
                              [&] {
                                      require_output(
                                              "md5 -q on 'a' x 200000 via stdin",
                                              ft_ssl.run({"md5", "-q"},
                                                         std::string(200000, 'a')),
                                              "561b1994f6baacd6e5eaf4baaa12849f");
                              });
                fd_stress.add("200 KB piped stdin survives backpressure (sha256)",
                              [&] {
                                      require_output(
                                              "sha256 -q on 'a' x 200000 via stdin",
                                              ft_ssl.run({"sha256", "-q"},
                                                         std::string(200000, 'a')),
                                              "2287d207f24a941ff3b56c04c8a25ad5"
                                              "6b63e3023207b3bb5b4ac0c9869d74be");
                              });
                fd_stress.add("1 MB file matches known md5 constant", [&] {
                        require_output("md5 -qr 1MB file",
                                       ft_ssl.run({"md5", "-q", "-r", big_path}),
                                       "7707d6ae4e027c70eea2a935c2296f21");
                });
                fd_stress.add("directory argument takes read-error path cleanly",
                              [&] {
                                      RunResult result =
                                              ft_ssl.run({"md5", "/tmp/opencode"});
                                      require_no_crash("md5 of a directory",
                                                       result);
                                      require(result.output.empty(),
                                              "no digest may be printed when "
                                              "read() fails on a directory");
                              });
                fd_stress.add("empty file yields empty digest", [&] {
                        require_output("md5 -r empty file",
                                       ft_ssl.run({"md5", "-r", empty_path}),
                                       "d41d8cd98f00b204e9800998ecf8427e "
                                               + empty_path);
                });
                fd_stress.add("unreadable file is rejected without crashing",
                              [&] {
                                      RunResult result =
                                              ft_ssl.run({"md5", denied.path()});
                                      require_no_crash("permission denied file",
                                                       result);
                                      require(result.output.find("secret")
                                                      == std::string::npos,
                                              "no content may leak from an "
                                              "unreadable file");
                              });

                Runner runner;
                runner.add(std::move(crashes));
                runner.add(std::move(errors));
                runner.add(std::move(golden));
                runner.add(std::move(fd_stress));

                return runner.run_all();
        }
        catch (const std::exception& e)
        {
                std::cerr << RED << "fatal: " << e.what() << RESET << "\n";
                return 2;
        }
}
