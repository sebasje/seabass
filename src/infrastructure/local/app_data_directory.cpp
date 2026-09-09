#include "infrastructure/local/app_data_directory.hpp"

#include "infrastructure/paths/seabass_paths.hpp"

namespace seabass::infrastructure::local
{

std::filesystem::path appDataDirectory()
{
    return paths::localMetadataDir();
}

}  // namespace seabass::infrastructure::local
