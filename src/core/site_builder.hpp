#ifndef SITE_BUILDER_HPP
#define SITE_BUILDER_HPP

#include "core/js_minifier.hpp"
#include "frontmatter.hpp"

#include "template_engine.hpp"
#include "utils/config.hpp"
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

class SiteBuilder {
private:
  std::unique_ptr<JSMinifier> js_minifier;
  bool minification_enabled;
  fs::path project_root;
  fs::path content_dir;
  fs::path templates_dir;
  fs::path output_dir;
  fs::path static_dir;

  SiteConfig config;
  std::string base_template;
  TemplateEngine template_engine;
  std::unordered_map<std::string, PageInfo> pages;
  mutable std::shared_mutex pages_mutex_;

  std::unordered_map<std::string, std::vector<const PageInfo *>> collections;

  bool has_error_page;

  std::string read_file(const fs::path &path);
  void write_file(const fs::path &path, const std::string &content);

  std::string apply_template(const std::string &template_content,
                             const PageInfo &page,
                             const std::string &processed_content);

  std::string apply_base_template(const std::string &content,
                                  const PageInfo &page);

  std::string inject_dev_scripts(const std::string &html);

  bool is_dev_mode = false;

public:
  SiteBuilder(const fs::path &root);

  void discover_content();
  void build_collections();

  std::string render_page(const PageInfo &page);
  void build_page(const std::string &url);
  void build_all();
  void export_static_site();

  void reload_base_template(const fs::path &path) {
    base_template = read_file(path);
  }

  const std::unordered_map<std::string, PageInfo> &get_pages() const {
    return pages;
  }

  const std::unordered_map<std::string, std::vector<const PageInfo *>> &
  get_collections() const {
    return collections;
  }

  const SiteConfig &get_config() const { return config; }

  void set_dev_mode(bool dev) { is_dev_mode = dev; }

  void initialize_minification();
  std::string minify_css_content(const std::string &css);
  std::string minify_js_content(const std::string &js);
  std::string minify_html_content(const std::string &html);
};

#endif