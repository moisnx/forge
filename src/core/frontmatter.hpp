#ifndef FRONTMATTER_H
#define FRONTMATTER_H

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class FrontMatter {
public:
  std::map<std::string, std::string> data;
  std::unordered_map<std::string, std::vector<std::string>> arrays;
  std::vector<std::string> tags;

  static std::pair<FrontMatter, std::string> parse(const std::string &content);

  std::string get(const std::string &key,
                  const std::string &default_val = "") const;
  bool has(const std::string &key);
};

#endif