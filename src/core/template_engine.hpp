#ifndef TEMPLATE_ENGINE_HPP
#define TEMPLATE_ENGINE_HPP

#include "frontmatter.hpp"
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vendor/inja/inja.hpp>
#include <vendor/nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

struct PageInfo {
  fs::path content_path;
  fs::path template_path;
  std::string url;
  std::string content_type;
  FrontMatter frontmatter;
  std::string html_content;
  bool needs_template;
};

class SafeJson {
public:
  static inja::json make_safe(const inja::json &data) {

    inja::json safe_data = data;
    add_safety_layer(safe_data);
    return safe_data;
  }

private:
  static void add_safety_layer(inja::json &obj) {
    if (!obj.is_object())
      return;

    for (auto &[key, value] : obj.items()) {
      if (value.is_object()) {
        add_safety_layer(value);
      }
    }
  }
};

class TemplateEngine {
public:
  using json = nlohmann::json;

  TemplateEngine() {
    env.set_throw_at_missing_includes(false);
    env.add_callback("exists", 1, [](inja::Arguments &args) {
      return !args.at(0)->is_null();
    });
    setup_custom_filters();
  }

  std::string render(const std::string &template_content, const json &data) {

    int max_attempts = 3;
    json working_data = data;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
      try {
        return env.render(template_content, working_data);
      } catch (const std::exception &e) {
        std::string error_msg = e.what();

        if (error_msg.find("variable") != std::string::npos &&
            error_msg.find("not found") != std::string::npos) {

          size_t start = error_msg.find("'");
          size_t end = error_msg.find("'", start + 1);

          if (start != std::string::npos && end != std::string::npos) {
            std::string var_path = error_msg.substr(start + 1, end - start - 1);

            std::cerr << "⚠️  Warning: Missing variable '" << var_path
                      << "'. Adding null value and retrying..." << std::endl;

            add_missing_path(working_data, var_path);
            continue;
          }
        }

        throw std::runtime_error("Template render error: " + error_msg);
      }
    }

    throw std::runtime_error(
        "Template render failed after multiple recovery attempts");
  }

  static void add_missing_path(json &data, const std::string &path) {
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;

    while (std::getline(iss, part, '.')) {
      parts.push_back(part);
    }

    json *current = &data;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (!current->contains(parts[i])) {
        if (i == parts.size() - 1) {
          (*current)[parts[i]] = nullptr;
        } else {
          (*current)[parts[i]] = json::object();
        }
      }
      current = &(*current)[parts[i]];
    }
  }

  static json yaml_to_json(const YAML::Node &node) {
    if (node.IsNull()) {
      return nullptr;
    }

    if (node.IsScalar()) {

      try {
        return node.as<int>();
      } catch (...) {
        try {
          return node.as<double>();
        } catch (...) {
          try {
            return node.as<bool>();
          } catch (...) {
            return node.as<std::string>();
          }
        }
      }
    }

    if (node.IsSequence()) {
      json result = json::array();
      for (const auto &item : node) {
        result.push_back(yaml_to_json(item));
      }
      return result;
    }

    if (node.IsMap()) {
      json result = json::object();
      for (auto it = node.begin(); it != node.end(); ++it) {
        result[it->first.as<std::string>()] = yaml_to_json(it->second);
      }
      return result;
    }

    return nullptr;
  }

  static std::tm parse_date(const std::string &date_str) {
    std::tm tm = {};
    std::istringstream ss(date_str);

    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (!ss.fail())
      return tm;

    ss.clear();
    ss.str(date_str);
    ss >> std::get_time(&tm, "%Y/%m/%d");
    if (!ss.fail())
      return tm;

    ss.clear();
    ss.str(date_str);
    ss >> std::get_time(&tm, "%d-%m-%Y");
    if (!ss.fail())
      return tm;

    ss.clear();
    ss.str(date_str);
    ss >> std::get_time(&tm, "%m/%d/%Y");
    if (!ss.fail())
      return tm;

    return {};
  }

  static json serialize_page(const PageInfo *page) {
    json page_json = {{"url", page->url},
                      {"content_type", page->content_type},
                      {"html_content", page->html_content}};

    if (!page->frontmatter.tags.empty()) {
      page_json["tags"] = page->frontmatter.tags;
    }

    for (const auto &[key, value] : page->frontmatter.data) {

      if (key == "tags") {
        continue;
      }

      if (value == "true" || value == "false") {
        page_json[key] = (value == "true");
        continue;
      }

      static const std::regex date_pattern(R"(\d{2,4}[-/]\d{1,2}[-/]\d{1,4})");
      if (std::regex_search(value, date_pattern)) {

        page_json[key] = value;
        continue;
      }

      bool is_number =
          !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
            return std::isdigit(c) || c == '.' || c == '-';
          });

      if (is_number) {
        size_t dot_count = std::count(value.begin(), value.end(), '.');
        size_t hyphen_count = std::count(value.begin(), value.end(), '-');

        if (dot_count <= 1 && hyphen_count <= 1) {
          if (hyphen_count == 1 && value[0] != '-') {

            is_number = false;
          }
        } else {
          is_number = false;
        }
      }

      if (is_number) {
        try {
          if (value.find('.') != std::string::npos) {
            page_json[key] = std::stod(value);
          } else {
            page_json[key] = std::stoi(value);
          }
          continue;
        } catch (...) {
        }
      }

      page_json[key] = value;
    }

    return page_json;
  }

  static json serialize_collection(const std::vector<const PageInfo *> &pages) {
    json collection = json::array();
    for (const PageInfo *page : pages) {
      collection.push_back(serialize_page(page));
    }
    return collection;
  }

private:
  inja::Environment env;

  void setup_custom_filters() {

    env.add_callback("date", 2, [](inja::Arguments &args) {
      std::string date_str = args.at(0)->get<std::string>();
      std::string format = args.at(1)->get<std::string>();

      std::tm tm = parse_date(date_str);
      if (tm.tm_year == 0) {
        return date_str;
      }

      static const char *month_names[] = {"January", "February", "March",
                                          "April",   "May",      "June",
                                          "July",    "August",   "September",
                                          "October", "November", "December"};
      static const char *month_names_short[] = {"Jan", "Feb", "Mar", "Apr",
                                                "May", "Jun", "Jul", "Aug",
                                                "Sep", "Oct", "Nov", "Dec"};

      if (format == "long") {
        format = "MMMM d, yyyy";
      } else if (format == "short") {
        format = "MMM d, yyyy";
      } else if (format == "iso") {
        format = "yyyy-MM-dd";
      }

      std::string result = format;

      if (result.find("yyyy") != std::string::npos) {
        std::string year = std::to_string(1900 + tm.tm_year);
        size_t pos = result.find("yyyy");
        result.replace(pos, 4, year);
      } else if (result.find("yy") != std::string::npos) {
        std::string year = std::to_string((1900 + tm.tm_year) % 100);
        if (year.length() == 1)
          year = "0" + year;
        size_t pos = result.find("yy");
        result.replace(pos, 2, year);
      }

      if (result.find("MMMM") != std::string::npos) {
        size_t pos = result.find("MMMM");
        result.replace(pos, 4, month_names[tm.tm_mon]);
      } else if (result.find("MMM") != std::string::npos) {
        size_t pos = result.find("MMM");
        result.replace(pos, 3, month_names_short[tm.tm_mon]);
      } else if (result.find("MM") != std::string::npos) {
        std::string month = std::to_string(tm.tm_mon + 1);
        if (month.length() == 1)
          month = "0" + month;
        size_t pos = result.find("MM");
        result.replace(pos, 2, month);
      } else if (result.find("M") != std::string::npos) {
        std::string month = std::to_string(tm.tm_mon + 1);
        size_t pos = result.find("M");
        result.replace(pos, 1, month);
      }

      if (result.find("dd") != std::string::npos) {
        std::string day = std::to_string(tm.tm_mday);
        if (day.length() == 1)
          day = "0" + day;
        size_t pos = result.find("dd");
        result.replace(pos, 2, day);
      } else if (result.find("d") != std::string::npos) {
        std::string day = std::to_string(tm.tm_mday);
        size_t pos = result.find("d");
        result.replace(pos, 1, day);
      }

      return result;
    });

    env.add_callback("truncate", 2, [](inja::Arguments &args) {
      std::string str = args.at(0)->get<std::string>();
      int len = args.at(1)->get<int>();
      if (str.length() > static_cast<size_t>(len)) {
        return str.substr(0, len) + "...";
      }
      return str;
    });

    env.add_callback("substring", 3, [](inja::Arguments &args) {
      std::string str = args.at(0)->get<std::string>();
      int start = args.at(1)->get<int>();
      int length = args.at(2)->get<int>();
      if (start < 0 || start >= static_cast<int>(str.length())) {
        return std::string("");
      }
      return str.substr(start, length);
    });

    env.add_callback("slice", 3, [](inja::Arguments &args) {
      auto &arr = *args.at(0);
      int start = args.at(1)->get<int>();
      int end = args.at(2)->get<int>();

      if (!arr.is_array()) {
        return json::array();
      }

      json result = json::array();
      int size = static_cast<int>(arr.size());

      if (start < 0)
        start = 0;
      if (end > size)
        end = size;
      if (start >= end)
        return result;

      for (int i = start; i < end; ++i) {
        result.push_back(arr[i]);
      }

      return result;
    });

    env.add_callback("limit", 2, [](inja::Arguments &args) {
      auto &arr = *args.at(0);
      int count = args.at(1)->get<int>();

      if (!arr.is_array()) {
        return json::array();
      }

      json result = json::array();
      int size = static_cast<int>(arr.size());
      int limit = (count < size) ? count : size;

      for (int i = 0; i < limit; ++i) {
        result.push_back(arr[i]);
      }

      return result;
    });

    env.add_callback("prefix_separator", 2, [](inja::Arguments &args) {
      std::string str = args.at(0)->get<std::string>();
      std::string sep = args.at(1)->get<std::string>();
      return str.empty() ? std::string("") : sep + str;
    });

    env.add_callback("suffix_separator", 2, [](inja::Arguments &args) {
      std::string str = args.at(0)->get<std::string>();
      std::string sep = args.at(1)->get<std::string>();
      return str.empty() ? std::string("") : str + sep;
    });
  }
};

#endif
