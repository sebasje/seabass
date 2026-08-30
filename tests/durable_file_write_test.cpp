#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "infrastructure/durable_file_write.hpp"

using namespace seabass::infrastructure;
namespace fs = std::filesystem;

namespace
{

std::string readFile(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const fs::path &p, const std::string &content)
{
    std::ofstream out(p, std::ios::binary);
    out << content;
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_durable_file_write_test";
    fs::remove_all(root);
    fs::create_directories(root);

    // Replaces an existing target's content, and leaves no temp file behind.
    {
        fs::path target = root / "target.db";
        writeFile(target, "old content");
        fs::path source = root / "new1.db";
        writeFile(source, "new content, replacing the old");

        bool ok = copyFileDurablyAtomic(source.string(), target.string());
        assert(ok);
        assert(readFile(target) == "new content, replacing the old");

        int strayTempFiles = 0;
        for (const auto &entry : fs::directory_iterator(root)) {
            if (entry.path().filename().string().find(".tmp-") != std::string::npos) {
                strayTempFiles++;
            }
        }
        assert(strayTempFiles == 0);
        std::cout << "case 1 (replaces existing target, no leftover temp file) OK\n";
    }

    // Target doesn't exist yet -- first-time create case.
    {
        fs::path target = root / "brand_new.db";
        fs::path source = root / "new2.db";
        writeFile(source, "first write");

        bool ok = copyFileDurablyAtomic(source.string(), target.string());
        assert(ok);
        assert(readFile(target) == "first write");
        std::cout << "case 2 (target didn't exist yet) OK\n";
    }

    // Source doesn't exist -- target (if any) is left completely untouched.
    {
        fs::path target = root / "untouched.db";
        writeFile(target, "must survive");

        bool ok = copyFileDurablyAtomic((root / "does_not_exist.db").string(), target.string());
        assert(!ok);
        assert(readFile(target) == "must survive");
        std::cout << "case 3 (missing source leaves target untouched) OK\n";
    }

    fs::remove_all(root);
    std::cout << "all cases passed\n";
    return 0;
}
