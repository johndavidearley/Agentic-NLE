#include "media/process.hpp"
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int run(int argc, char **argv) {
#ifdef _WIN32
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2)
        return 2;
    const std::string mode = argv[1];
    if (mode == "sleep")
        std::this_thread::sleep_for(std::chrono::seconds(10));
    else if (mode == "flood")
        for (int i = 0; i < 100000; ++i)
            std::cout << 'x';
    else if (mode == "fail") {
        std::cerr << "fixture failure";
        return 7;
    } else if (mode == "json") {
        std::cout << "{\"ok\":true}" << std::flush;
        std::cerr << "library diagnostic\n";
    } else if (mode == "stderr-flood") {
        for (int i = 0; i < 100000; ++i)
            std::cerr << 'x';
        std::cout << "{\"ok\":true}";
    } else if (mode == "echo")
        for (int i = 2; i < argc; ++i)
            std::cout << argv[i] << '\n';
    else
        return 3;
    return 0;
}
#ifdef _WIN32
int wmain(int argc, wchar_t **wide) {
    std::vector<std::string> storage;
    for (int i = 0; i < argc; ++i)
        storage.push_back(nle::media::path_utf8(std::filesystem::path(wide[i])));
    std::vector<char *> argv;
    for (auto &arg : storage)
        argv.push_back(arg.data());
    return run(argc, argv.data());
}
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
