#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <any>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

struct CollectionConfig {
  std::string name;
  std::string sort_by = "date";
  std::string sort_order = "desc";
  std::string template_name;
  std::string url_pattern;
};

struct MinifyConfig {
  bool html = true;
  bool css = true;
  bool js = true;
};

struct ConfigValue {
  enum Type { STRING, LIST, MAP };

  Type type;
  std::string string_value;
  std::vector<std::string> list_value;
  std::unordered_map<std::string, std::string> map_value;
  std::vector<std::unordered_map<std::string, std::string>> list_of_maps;

  ConfigValue() : type(STRING) {}
  explicit ConfigValue(const std::string &s) : type(STRING), string_value(s) {}
  explicit ConfigValue(const std::vector<std::string> &l)
      : type(LIST), list_value(l) {}
};

class SiteConfig {
private:
  static std::string join_vector(const std::vector<std::string> &vec,
                                 const std::string &delimiter) {
    std::string result;
    for (size_t i = 0; i < vec.size(); ++i) {
      if (i > 0)
        result += delimiter;
      result += vec[i];
    }
    return result;
  }

  static std::unordered_map<std::string, std::string>
  parse_yaml_map(const YAML::Node &node) {
    std::unordered_map<std::string, std::string> result;
    for (auto it = node.begin(); it != node.end(); ++it) {
      std::string key = it->first.as<std::string>();
      if (it->second.IsScalar()) {
        result[key] = it->second.as<std::string>();
      } else if (it->second.IsSequence()) {
        std::vector<std::string> items;
        for (const auto &item : it->second) {
          items.push_back(item.as<std::string>());
        }
        result[key] = join_vector(items, ", ");
      }
    }
    return result;
  }

public:
  std::string site_name;
  std::string author;
  std::string description;
  std::string url;
  std::vector<std::string> keywords;

  bool minify_output = false;
  MinifyConfig minify;

  std::string github_url;
  std::string x_twitter_url;

  std::string output_dir;
  std::string static_dir;
  std::string content_dir;
  std::string templates_dir;

  std::unordered_map<std::string, CollectionConfig> collections;

  std::unordered_map<std::string, std::string> defaults;

  YAML::Node custom_yaml_data;

  static SiteConfig load(const fs::path &config_path) {
    SiteConfig config;

    if (!fs::exists(config_path)) {
      throw std::runtime_error("Config file not found: " +
                               config_path.string());
    }

    YAML::Node yaml = YAML::LoadFile(config_path.string());

    config.custom_yaml_data = yaml;

    std::unordered_set<std::string> known_keys = {
        "site_name",  "author",      "description",   "keywords",
        "url",        "github_url",  "x_twitter_url", "output_dir",
        "static_dir", "content_dir", "templates_dir", "collections",
        "defaults"};

    if (yaml["site_name"])
      config.site_name = yaml["site_name"].as<std::string>();
    if (yaml["author"])
      config.author = yaml["author"].as<std::string>();
    if (yaml["description"])
      config.description = yaml["description"].as<std::string>();
    if (yaml["keywords"])
      config.keywords = yaml["keywords"].as<std::vector<std::string>>();
    if (yaml["url"])
      config.url = yaml["url"].as<std::string>();

    if (yaml["github_url"])
      config.github_url = yaml["github_url"].as<std::string>();
    if (yaml["x_twitter_url"])
      config.x_twitter_url = yaml["x_twitter_url"].as<std::string>();

    if (yaml["output_dir"])
      config.output_dir = yaml["output_dir"].as<std::string>();
    if (yaml["static_dir"])
      config.static_dir = yaml["static_dir"].as<std::string>();
    if (yaml["content_dir"])
      config.content_dir = yaml["content_dir"].as<std::string>();
    if (yaml["templates_dir"])
      config.templates_dir = yaml["templates_dir"].as<std::string>();

    if (yaml["collections"]) {
      for (auto it = yaml["collections"].begin();
           it != yaml["collections"].end(); ++it) {
        CollectionConfig col;
        col.name = it->first.as<std::string>();

        if (it->second["sort_by"]) {
          col.sort_by = it->second["sort_by"].as<std::string>();
        }
        if (it->second["sort_order"]) {
          col.sort_order = it->second["sort_order"].as<std::string>();
        }
        if (it->second["template"]) {
          col.template_name = it->second["template"].as<std::string>();
        }
        if (it->second["url_pattern"]) {
          col.url_pattern = it->second["url_pattern"].as<std::string>();
        }

        config.collections[col.name] = col;
      }
    }

    if (yaml["defaults"]) {
      for (auto it = yaml["defaults"].begin(); it != yaml["defaults"].end();
           ++it) {
        config.defaults[it->first.as<std::string>()] =
            it->second.as<std::string>();
      }
    }

    if (yaml["minify_output"]) {
      config.minify_output = yaml["minify_output"].as<bool>();
    }

    if (yaml["minify"]) {
      if (yaml["minify"]["html"]) {
        config.minify.html = yaml["minify"]["html"].as<bool>();
      }
      if (yaml["minify"]["css"]) {
        config.minify.css = yaml["minify"]["css"].as<bool>();
      }
      if (yaml["minify"]["js"]) {
        config.minify.js = yaml["minify"]["js"].as<bool>();
      }
    }

    return config;
  }

  std::unordered_map<std::string, std::string> get_site_variables() const {
    std::unordered_map<std::string, std::string> vars;

    vars["site.name"] = site_name;
    vars["site.title"] = site_name;
    vars["site.author"] = author;
    vars["site.description"] = description;
    vars["site.keywords"] = join_vector(keywords, ", ");
    vars["site.url"] = url;
    vars["site.github_url"] = github_url;
    vars["site.x_twitter_url"] = x_twitter_url;

    for (const auto &[key, value] : defaults) {
      vars["site." + key] = value;
    }

    return vars;
  }

  const YAML::Node &get_custom_data() const { return custom_yaml_data; }

  YAML::Node get_custom_field(const std::string &field_name) const {
    if (custom_yaml_data[field_name]) {
      return custom_yaml_data[field_name];
    }
    return YAML::Node();
  }

  bool has_custom_field(const std::string &field_name) const {
    return custom_yaml_data[field_name].IsDefined();
  }
};

#endif