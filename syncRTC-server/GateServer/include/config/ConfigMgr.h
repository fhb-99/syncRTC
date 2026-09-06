#pragma once

#include <map>
#include <utility>
#include <cstdint> // 顺带修复 intmax_t 缺失依赖
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

struct SectionInfo
{
    std::map<std::string, std::string> _maps;

    SectionInfo() = default;
    ~SectionInfo() { _maps.clear(); }

    SectionInfo(const SectionInfo& other) { _maps = other._maps; }

    SectionInfo operator = (const SectionInfo& other) {
        if(&other == this) return *this;

        this->_maps = other._maps;
        return *this;
    }

    std::string operator[] (const std::string& key) {
        if(_maps.find(key) == _maps.end()) {
            return "";
        }
        return _maps[key]; 
    }
};

class ConfigMgr
{
public:
    ConfigMgr();
    ~ConfigMgr() { m_config_maps.clear(); }

    SectionInfo operator[] (const std::string& key) {
        if(m_config_maps.find(key) == m_config_maps.end()) {
            return SectionInfo();
        }
        return m_config_maps[key];
    }

    ConfigMgr(const ConfigMgr& other) {
        this->m_config_maps = other.m_config_maps;
    }

    ConfigMgr operator= (const ConfigMgr& other) {
        if(&other == this) {
            return *this;
        }
        this->m_config_maps = other.m_config_maps;
    }

    static ConfigMgr& Init() {
        static ConfigMgr config_mgr;
        return config_mgr;
    }

private:
    std::map<std::string, SectionInfo> m_config_maps;
};
