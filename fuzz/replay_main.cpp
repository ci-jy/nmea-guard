// Stand-in for libFuzzer's main() so the fuzz targets also build with GCC
// and run as ordinary tests: each argument is a file or a directory of
// files, and every file is passed to LLVMFuzzerTestOneInput once. This is
// how the corpus and any saved crash inputs are re-checked in a normal build.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace fs = std::filesystem;

static bool run_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path.string().c_str());
        return false;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return true;
}

int main(int argc, char** argv) {
    std::size_t files = 0;
    bool ok = true;
    for (int i = 1; i < argc; ++i) {
        const fs::path arg(argv[i]);
        std::error_code ec;
        if (fs::is_directory(arg, ec)) {
            for (const auto& entry : fs::directory_iterator(arg)) {
                if (!entry.is_regular_file()) continue;
                ok = run_file(entry.path()) && ok;
                ++files;
            }
        } else {
            ok = run_file(arg) && ok;
            ++files;
        }
    }
    std::printf("ran %zu inputs\n", files);
    return ok && files > 0 ? 0 : 1;
}
