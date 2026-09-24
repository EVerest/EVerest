// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <framework/runtime.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Everest {

inline constexpr std::uint32_t MODULE_PLUGIN_ABI_VERSION = 1;
inline constexpr const char* MODULE_PLUGIN_ENTRY_SYMBOL = "everest_module_entry";

/// One instance of a module created by its shared object. The host owns it and must keep the shared object
/// loaded for as long as the instance, or any callback handed out by it, exists.
class ModulePluginInstance {
public:
    virtual ~ModulePluginInstance() = default;
    /// Callbacks bound to this instance, in the shape ModuleLoader expects.
    virtual ModuleCallbacks callbacks() = 0;
};

/// Returned by the entry symbol of a module shared object.
struct ModulePluginEntry {
    std::uint32_t abi_version;
    std::string module_name;
    VersionInformation version;
    std::unique_ptr<ModulePluginInstance> (*create_instance)();
};

using ModulePluginEntryFn = const ModulePluginEntry* (*)();

} // namespace Everest

extern "C" const Everest::ModulePluginEntry* everest_module_entry();
