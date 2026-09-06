#include "config/ConfigMgr.h"

#include <boost/filesystem.hpp>
#include <iostream>

ConfigMgr::ConfigMgr()
{
    // 获取当前路径
    boost::filesystem::path config_path = boost::filesystem::path(PROJECT_ROOT_DIR) / "bin" / "conf" / "config.ini";
    std::cout << "Config Path: " << config_path << std::endl;

    // 使用Boost.PropertyTree来读取INI文件  
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(config_path.string(), pt);

    // 遍历
    for(const auto& sections : pt) {
        const std::string& server_name = sections.first;
        const boost::property_tree::ptree& section_tree = sections.second;

        // 在遍历其内部的map
        std::map<std::string, std::string> section_config;
        for (const auto& key_value_pair : section_tree) {
            const std::string& key = key_value_pair.first;
            const std::string& value = key_value_pair.second.get_value<std::string>();
            section_config[key] = value;
        }
        SectionInfo sectionInfo;
        sectionInfo._maps = section_config;
        // 将section的key-value对保存到config_map中  
        m_config_maps[server_name] = sectionInfo;
    }

    // 输出所有的section和key-value对  
    for (const auto& section_entry : m_config_maps) {
        const std::string& section_name = section_entry.first;
        SectionInfo section = section_entry.second;
        std::cout << "[" << section_name << "]" << std::endl;
        for (const auto& key_value_pair : section._maps) {
            std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
        }
    }
}