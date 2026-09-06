#include "config/ConfigMgr.h"

#include <boost/filesystem.hpp>
#include <iostream>
#include <map>
#include <string>

ConfigMgr::ConfigMgr()
{
    boost::filesystem::path config_path =
        boost::filesystem::path(PROJECT_ROOT_DIR) / "bin" / "conf" / "config.ini";
    std::cout << "Config Path: " << config_path << std::endl;

    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(config_path.string(), pt);

    for(const auto& sections : pt) {
        const std::string& server_name = sections.first;
        const boost::property_tree::ptree& section_tree = sections.second;

        std::map<std::string, std::string> section_config;
        for (const auto& key_value_pair : section_tree) {
            const std::string& key = key_value_pair.first;
            const std::string& value = key_value_pair.second.get_value<std::string>();
            section_config[key] = value;
        }
        SectionInfo sectionInfo;
        sectionInfo._maps = section_config;
        m_config_maps[server_name] = sectionInfo;
    }

    for (const auto& section_entry : m_config_maps) {
        const std::string& section_name = section_entry.first;
        SectionInfo section = section_entry.second;
        std::cout << "[" << section_name << "]" << std::endl;
        for (const auto& key_value_pair : section._maps) {
            std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
        }
    }
}
