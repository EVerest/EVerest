#pragma once

#include "config_service_client_ifc.hpp"
#include "yaml_provider_ifc.hpp"
#include <memory>
#include <string>

namespace everest::config_cli {

class CommandHandlers {
public:
    CommandHandlers(std::shared_ptr<ConfigServiceClientIfc> client, std::shared_ptr<YamlProviderIfc> yaml_provider);

    void list_slots();
    void show_slot_metadata(int slot_id);
    void get_active_slot();
    void mark_active_slot(int slot_id);
    void delete_slot(int slot_id);
    void duplicate_slot(int slot_id, const std::string& description);
    void load_yaml(const std::string& filename, const std::string& description, std::optional<int> slot_id);
    void set_description(int slot_id, const std::string& description);
    void get_configuration(int slot_id);
    void set_config_parameters(int slot_id, const std::string& filename);
    void get_config_parameters(int slot_id, const std::string& filename);
    void monitor(bool suppress_parameter_updates);

private:
    std::shared_ptr<ConfigServiceClientIfc> m_client;
    std::shared_ptr<YamlProviderIfc> m_yaml_provider;
};

} // namespace everest::config_cli
