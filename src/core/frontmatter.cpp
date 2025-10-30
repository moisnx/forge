#include "frontmatter.hpp"
#include "yaml-cpp/yaml.h"

std::pair<FrontMatter, std::string>
FrontMatter::parse(const std::string &content) {
  FrontMatter fm;

  if (content.substr(0, 3) != "---") {
    return {fm, content};
  }

  size_t end_pos = content.find("\n---\n", 3);
  if (end_pos == std::string::npos) {

    end_pos = content.find("\n---", 3);
    if (end_pos == std::string::npos) {
      return {fm, content};
    }
  }

  std::string yaml_str = content.substr(3, end_pos - 3);
  std::string markdown = content.substr(end_pos + 5);

  try {
    YAML::Node node = YAML::Load(yaml_str);

    for (auto it = node.begin(); it != node.end(); ++it) {
      std::string key = it->first.as<std::string>();

      if (it->second.IsSequence()) {
        std::vector<std::string> arr;
        for (const auto &item : it->second) {
          arr.push_back(item.as<std::string>());
        }
        fm.arrays[key] = arr;

        if (key == "tags") {
          fm.tags = arr;
        }
      } else {
        fm.data[key] = it->second.as<std::string>();
      }
    }
  } catch (const YAML::Exception &e) {
    throw std::runtime_error("YAML parsing error: " + std::string(e.what()));
  }

  return {fm, markdown};
}

std::string FrontMatter::get(const std::string &key,
                             const std::string &default_val) const {
  auto it = data.find(key);
  return (it != data.end()) ? it->second : default_val;
}

bool FrontMatter::has(const std::string &key) {
  return data.find(key) != data.end();
}