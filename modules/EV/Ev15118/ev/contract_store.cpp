// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "contract_store.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <iso15118/ev/d2/pnc_material.hpp>

namespace module {

namespace {

namespace fs = std::filesystem;

constexpr const char* CHAIN_FILE = "CONTRACT_CHAIN.pem";
constexpr const char* KEY_FILE = "CONTRACT_LEAF.key";
constexpr const char* EMAID_FILE = "EMAID.txt";

std::string errno_text() {
    return std::strerror(errno);
}

// Writes `content` to a fresh temporary file beside `destination`. mkstemp creates it with mode
// 0600 before the first byte, and the same directory keeps the later rename on one filesystem.
bool stage(const fs::path& destination, const std::string& content, fs::path& staged, std::string& error) {
    auto pattern = (destination.parent_path() / ("." + destination.filename().string() + ".XXXXXX")).string();
    const int fd = ::mkstemp(pattern.data());
    if (fd < 0) {
        error = "cannot create a temporary file for " + destination.filename().string() + ": " + errno_text();
        return false;
    }
    staged = pattern;
    const char* data = content.data();
    std::size_t left = content.size();
    while (left > 0) {
        const auto written = ::write(fd, data, left);
        if (written < 0 and errno == EINTR) {
            continue;
        }
        if (written < 0) {
            error = "cannot write " + staged.filename().string() + ": " + errno_text();
            ::close(fd);
            return false;
        }
        data += written;
        left -= static_cast<std::size_t>(written);
    }
    if (::fsync(fd) != 0) {
        error = "cannot flush " + staged.filename().string() + ": " + errno_text();
        ::close(fd);
        return false;
    }
    if (::close(fd) != 0) {
        error = "cannot close " + staged.filename().string() + ": " + errno_text();
        return false;
    }
    return true;
}

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (not file.is_open()) {
        return std::nullopt;
    }
    try {
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } catch (const std::ios_base::failure&) {
        return std::nullopt;
    }
}

// The temporary files stage() leaves when its run is interrupted before the renames.
void remove_leftover_staging(const fs::path& dir, const std::array<const char*, 3>& names) {
    std::error_code ec;
    std::vector<fs::path> leftovers;
    for (fs::directory_iterator it(dir, ec), end; not ec and it != end; it.increment(ec)) {
        const auto file = it->path().filename().string();
        for (const auto* name : names) {
            const auto prefix = "." + std::string(name) + ".";
            if (file.size() == prefix.size() + 6 and file.compare(0, prefix.size(), prefix) == 0) {
                leftovers.push_back(it->path());
            }
        }
    }
    for (const auto& path : leftovers) {
        ::unlink(path.c_str());
    }
}

std::string trimmed_without_dashes(std::string text) {
    text.erase(
        std::remove_if(text.begin(), text.end(), [](unsigned char c) { return c == '-' or std::isspace(c) != 0; }),
        text.end());
    return text;
}

} // namespace

std::optional<iso15118::ev::d2::ContractMaterial> decode_contract(const std::string& chain_pem,
                                                                  const std::string& key_pem, std::string& error) {
    namespace material = iso15118::ev::d2::pnc_material;
    auto chain_der = material::pem_chain_to_der(chain_pem);
    if (chain_der.empty()) {
        error = "the chain holds no certificate";
        return std::nullopt;
    }
    iso15118::ev::d2::ContractMaterial contract;
    contract.cert_der = std::move(chain_der.front());
    contract.sub_certs_der.assign(std::make_move_iterator(chain_der.begin() + 1),
                                  std::make_move_iterator(chain_der.end()));
    contract.emaid = material::emaid_from_contract_der(contract.cert_der);
    if (contract.emaid.empty()) {
        error = "the contract leaf has no eMAID";
        return std::nullopt;
    }
    if (not material::contract_key_matches(contract.cert_der, key_pem)) {
        error = "the key is not the private key of the contract leaf";
        return std::nullopt;
    }
    contract.key_pem = key_pem;
    return contract;
}

bool store_contract(const fs::path& dir, const std::string& chain_pem, const std::string& key_pem,
                    const std::string& emaid, std::string& error, std::string& warning) {
    warning.clear();
    const auto contract = decode_contract(chain_pem, key_pem, error);
    if (not contract) {
        return false;
    }
    if (trimmed_without_dashes(emaid) != contract->emaid) {
        error = "eMAID " + emaid + " does not match the contract leaf's eMAID " + contract->emaid;
        return false;
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = "cannot create the directory: " + ec.message();
        return false;
    }
    remove_leftover_staging(dir, {KEY_FILE, CHAIN_FILE, EMAID_FILE});

    // The key goes first: a run interrupted before the chain follows leaves a key that does not
    // belong to the stored leaf, which the loader rejects.
    const std::array<std::pair<const char*, const std::string*>, 3> files{
        {{KEY_FILE, &key_pem}, {CHAIN_FILE, &chain_pem}, {EMAID_FILE, &emaid}}};
    std::array<fs::path, 3> staged;
    const auto discard_staged = [&staged] {
        for (const auto& path : staged) {
            if (not path.empty()) {
                ::unlink(path.c_str());
            }
        }
    };

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (not stage(dir / files[i].first, *files[i].second, staged[i], error)) {
            discard_staged();
            return false;
        }
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto destination = dir / files[i].first;
        if (::rename(staged[i].c_str(), destination.c_str()) != 0) {
            error = "cannot rename into " + destination.filename().string() + ": " + errno_text();
            discard_staged();
            return false;
        }
        staged[i].clear();
    }
    // The renames survive a power loss only once the directory itself is synced.
    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0 or ::fsync(dir_fd) != 0) {
        warning = "cannot sync the directory, so the stored contract may not survive a power loss: " + errno_text();
    }
    if (dir_fd >= 0) {
        ::close(dir_fd);
    }
    return true;
}

std::optional<iso15118::ev::d2::ContractMaterial> load_stored_contract(const fs::path& dir, std::string& error) {
    std::error_code status_error;
    const auto dir_status = fs::status(dir, status_error);
    if (fs::exists(dir_status) and not fs::is_directory(dir_status)) {
        error = "not a directory";
        return std::nullopt;
    }
    const auto chain_pem = read_file(dir / CHAIN_FILE);
    const auto key_pem = read_file(dir / KEY_FILE);
    const auto emaid_text = read_file(dir / EMAID_FILE);
    if (not chain_pem or not key_pem or not emaid_text) {
        const auto absent = [&dir](const char* name) {
            std::error_code ec;
            return not fs::exists(dir / name, ec) and not ec;
        };
        const char* name = not chain_pem ? CHAIN_FILE : not key_pem ? KEY_FILE : EMAID_FILE;
        if (not absent(name)) {
            error = std::string("cannot read ") + name;
        } else if (absent(CHAIN_FILE) and absent(KEY_FILE) and absent(EMAID_FILE)) {
            error.clear();
        } else {
            error = std::string(name) + " is missing";
        }
        return std::nullopt;
    }

    auto contract = decode_contract(*chain_pem, *key_pem, error);
    if (not contract) {
        return std::nullopt;
    }
    if (trimmed_without_dashes(*emaid_text) != contract->emaid) {
        error = std::string(EMAID_FILE) + " does not name the eMAID of the stored contract leaf";
        return std::nullopt;
    }
    return contract;
}

} // namespace module
