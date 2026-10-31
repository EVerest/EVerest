// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <iso15118/ev/d2/pnc_config.hpp>

namespace module {

// The contract a chain (leaf first) and a key hold, when the chain decodes, the leaf carries an
// eMAID, derived from its CommonName as for a configured contract, and the key is the leaf's.
// std::nullopt otherwise, with error naming the problem. Never throws; error never holds key bytes.
std::optional<iso15118::ev::d2::ContractMaterial> decode_contract(const std::string& chain_pem,
                                                                  const std::string& key_pem, std::string& error);

// Refuses what decode_contract refuses and an eMAID that does not name the chain's leaf, which
// load_stored_contract would reject.
// Removes the temporary files an interrupted earlier run left in `dir`.
// Creates `dir` when missing. Each file is written to a temporary file of mode 0600 in `dir` and
// renamed over its destination once complete, so each destination is either old or new. A failed
// or interrupted chain rename leaves the new key beside the old chain, which load_stored_contract
// rejects, so the last good contract is lost and the configured one is used. A failed or
// interrupted EMAID.txt rename leaves the new chain and key beside the old EMAID.txt: the new
// contract loads when the eMAID is unchanged, and otherwise the set is rejected with the same loss.
// False with error set when any step fails. True once all three files are renamed into place, with
// warning set when the directory could not be synced afterwards, so the renames may not survive a
// power loss. Never throws, never logs; error never holds key bytes.
bool store_contract(const std::filesystem::path& dir, const std::string& chain_pem, const std::string& key_pem,
                    const std::string& emaid, std::string& error, std::string& warning);

// The contract stored in `dir` as CONTRACT_CHAIN.pem (leaf first), CONTRACT_LEAF.key (unencrypted
// PEM, so no key password) and EMAID.txt, when all three files are present, decode_contract accepts
// the chain and key, and EMAID.txt names the leaf's eMAID. std::nullopt otherwise, with error empty
// when `dir` or all three files are missing, and naming the problem otherwise. Never throws.
std::optional<iso15118::ev::d2::ContractMaterial> load_stored_contract(const std::filesystem::path& dir,
                                                                       std::string& error);

} // namespace module
