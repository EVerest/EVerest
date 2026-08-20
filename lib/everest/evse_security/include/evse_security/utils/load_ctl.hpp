#pragma once
#include <filesystem>
#include <map>
#include <evse_security/evse_types.hpp>
#include <evse_security/utils/evse_filesystem_types.hpp>

namespace evse_security {

void load_ctl(const fs::path& ctl_directory,
              const std::map<CaCertificateType, fs::path>& ca_bundle_path_map);

}