#include "site_builder.hpp"
#include "markdown.hpp"
#include "template_engine.hpp"
#include "utils/build_info.hpp"
#include "vendor/termcolor.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <sstream>

std::string escape_regex(const std::string &str) {
  static const std::regex special_chars{R"([-[\]{}()*+?.,\^$|#\s])"};
  return std::regex_replace(str, special_chars, R"(\$&)");
}

SiteBuilder::SiteBuilder(const fs::path &root) : project_root(root) {

  fs::path config_path = root / "forge.yaml";
  config = SiteConfig::load(config_path);

  content_dir = root / config.content_dir;
  templates_dir = root / config.templates_dir;
  output_dir = root / config.output_dir;
  static_dir = root / config.static_dir;

  fs::path base_path = templates_dir / "base.html";
  if (fs::exists(base_path)) {
    base_template = read_file(base_path);
  } else {
    std::cerr << termcolor::yellow << "⚠ Warning: " << termcolor::reset
              << "base.html not found\n";
  }
}

void SiteBuilder::initialize_minification() {

  minification_enabled = config.minify_output;

  if (minification_enabled) {
    js_minifier = std::make_unique<JSMinifier>();
    if (!js_minifier->initialize()) {
      std::cerr << termcolor::yellow << "⚠ Warning: " << termcolor::reset
                << "JS/CSS/HTML minification disabled (QuickJS init failed)\n";
      minification_enabled = false;
    } else {
      std::cout << termcolor::bright_green << "✓ " << termcolor::reset
                << "JS/CSS/HTML minification enabled\n";
    }
  }
}

std::string SiteBuilder::minify_css_content(const std::string &css) {
  if (!minification_enabled || !js_minifier) {
    return css;
  }

  auto result = js_minifier->minifyCSS(css);
  return result.value_or(css);
}

std::string SiteBuilder::minify_js_content(const std::string &js) {
  if (!minification_enabled || !js_minifier) {
    return js;
  }

  auto result = js_minifier->minifyJS(js);
  return result.value_or(js);
}

std::string SiteBuilder::minify_html_content(const std::string &html) {
  if (!minification_enabled || !js_minifier) {
    return html;
  }

  auto result = js_minifier->minifyHTML(html);
  return result.value_or(html);
}

std::string SiteBuilder::read_file(const fs::path &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open file: " + path.string());
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void SiteBuilder::write_file(const fs::path &path, const std::string &content) {
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path());
  }
  std::ofstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot write file: " + path.string());
  }
  file << content;
}

std::string SiteBuilder::inject_dev_scripts(const std::string &html) {
  std::string dev_script = R"(
<script defer src="/livereload.js"></script>
)";

  size_t head_close = html.find("</head>");

  if (head_close != std::string::npos) {
    return html.substr(0, head_close) + dev_script + html.substr(head_close);
  } else {
    return html + dev_script;
  }
}

void SiteBuilder::discover_content() {
  auto start = std::chrono::high_resolution_clock::now();

  std::unique_lock<std::shared_mutex> lock(pages_mutex_);

  pages.clear();
  collections.clear();

  if (!fs::exists(content_dir)) {
    std::cerr << termcolor::bright_red << "✗ Error: " << termcolor::reset
              << "Content directory not found: " << termcolor::bright_white
              << content_dir << termcolor::reset << "\n";
    return;
  }

  for (const auto &entry : fs::recursive_directory_iterator(content_dir)) {
    std::string ext = entry.path().extension().string();

    if (!entry.is_regular_file() || (ext != ".md" && ext != ".html")) {
      continue;
    }

    fs::path relative = fs::relative(entry.path(), content_dir);

    std::string content_type = "page";
    std::string url_path;

    auto it = relative.begin();
    if (it != relative.end()) {
      std::string first_folder = it->string();
      content_type = first_folder;
      ++it;

      if (first_folder == "pages") {
        fs::path rest_path;
        while (it != relative.end()) {
          rest_path /= *it;
          ++it;
        }

        if (rest_path.stem() == "index") {
          url_path = "/";
        } else {
          url_path = "/" + rest_path.stem().string();
        }
      } else {
        url_path = "/" + first_folder;

        fs::path rest_path;
        while (it != relative.end()) {
          rest_path /= *it;
          ++it;
        }

        if (!rest_path.empty() && rest_path.stem() != "index") {
          url_path += "/" + rest_path.stem().string();
        }
      }
    }

    std::string raw_content = read_file(entry.path());
    FrontMatter fm;
    std::string html_content;
    bool is_standalone = false;

    if (ext == ".md") {
      auto [parsed_fm, markdown_body] = FrontMatter::parse(raw_content);
      fm = parsed_fm;
      html_content = MarkdownProcessor::to_html(markdown_body);
    } else if (ext == ".html") {
      if (raw_content.find("---") == 0) {
        auto [parsed_fm, html_body] = FrontMatter::parse(raw_content);
        fm = parsed_fm;
        html_content = html_body;

        if (html_body.find("<!DOCTYPE") != std::string::npos ||
            html_body.find("<html") != std::string::npos) {
          is_standalone = true;
        }
      } else {
        html_content = raw_content;

        if (raw_content.find("<!DOCTYPE") != std::string::npos ||
            raw_content.find("<html") != std::string::npos) {
          is_standalone = true;
        }
      }
    }

    fs::path template_path;
    bool has_content_template = false;

    if (!is_standalone) {

      auto col_config = config.collections.find(content_type);

      if (col_config != config.collections.end() &&
          !col_config->second.template_name.empty()) {

        template_path = templates_dir / col_config->second.template_name;

        if (fs::exists(template_path) &&
            template_path.filename() != "base.html") {
          has_content_template = true;
        } else {
          std::cerr << termcolor::yellow << "⚠ Warning: " << termcolor::reset
                    << "Template '" << col_config->second.template_name
                    << "' not found for collection '" << content_type << "'\n";
        }
      } else {

        template_path = templates_dir / (content_type + ".html");

        if (fs::exists(template_path) &&
            template_path.filename() != "base.html") {
          has_content_template = true;
        }
      }
    }

    PageInfo page;
    page.content_path = entry.path();
    page.template_path = has_content_template ? template_path : fs::path();
    page.url = url_path;
    page.content_type = content_type;
    page.frontmatter = fm;
    page.html_content = html_content;
    page.needs_template = !is_standalone;

    if (page.url == "404") {
      has_error_page = true;
    }

    pages[url_path] = page;
  }

  build_collections();
}

void SiteBuilder::build_collections() {
  collections.clear();

  for (const auto &[url, page] : pages) {

    if (page.content_type == "pages")
      continue;

    collections[page.content_type].push_back(&page);
  }

  for (auto &[name, items] : collections) {
    auto col_config = config.collections.find(name);
    if (col_config != config.collections.end()) {
      std::string sort_by = col_config->second.sort_by;
      bool descending = (col_config->second.sort_order == "desc");

      std::sort(items.begin(), items.end(),
                [&sort_by, descending](const PageInfo *a, const PageInfo *b) {
                  std::string a_val = a->frontmatter.get(sort_by, "");
                  std::string b_val = b->frontmatter.get(sort_by, "");

                  if (descending) {
                    return a_val > b_val;
                  } else {
                    return a_val < b_val;
                  }
                });
    }
  }
}

std::string SiteBuilder::apply_template(const std::string &template_content,
                                        const PageInfo &page,
                                        const std::string &processed_content) {
  using json = nlohmann::json;
  json data;

  data["site"] = TemplateEngine::yaml_to_json(config.get_custom_data());

  data["page"] = TemplateEngine::serialize_page(&page);

  data["collections"] = json::object();
  for (const auto &[name, items] : collections) {
    data["collections"][name] = TemplateEngine::serialize_collection(items);
  }

  data["content"] = processed_content;

  return template_engine.render(template_content, data);
}

std::string SiteBuilder::apply_base_template(const std::string &content,
                                             const PageInfo &page) {
  if (base_template.empty()) {
    return content;
  }

  using json = nlohmann::json;
  json data;

  data["site"] = TemplateEngine::yaml_to_json(config.get_custom_data());

  data["page"] = TemplateEngine::serialize_page(&page);

  data["collections"] = json::object();
  for (const auto &[name, items] : collections) {
    data["collections"][name] = TemplateEngine::serialize_collection(items);
  }

  data["content"] = content;

  data["version"] = std::to_string(BuildInfo::getInstance().getVersion());

  std::string result = template_engine.render(base_template, data);

  if (is_dev_mode) {
    result = inject_dev_scripts(result);
  }

  return result;
}

std::string SiteBuilder::render_page(const PageInfo &page) {
  std::shared_lock<std::shared_mutex> lock(pages_mutex_);

  if (!page.needs_template) {
    return page.html_content;
  }

  using json = nlohmann::json;
  json data;

  data["site"] = TemplateEngine::yaml_to_json(config.get_custom_data());
  data["page"] = TemplateEngine::serialize_page(&page);

  data["collections"] = json::object();
  for (const auto &[name, items] : collections) {
    data["collections"][name] = TemplateEngine::serialize_collection(items);
  }

  std::string processed_content =
      template_engine.render(page.html_content, data);

  std::string content_to_wrap = processed_content;

  if (!page.template_path.empty() && fs::exists(page.template_path)) {
    std::string template_content = read_file(page.template_path);
    content_to_wrap = apply_template(template_content, page, processed_content);
  }

  return apply_base_template(content_to_wrap, page);
}

void SiteBuilder::build_page(const std::string &url) {
  auto it = pages.find(url);
  if (it == pages.end()) {
    throw std::runtime_error("Page not found: " + url);
  }

  std::string html = render_page(it->second);

  html = minify_html_content(html);

  fs::path out_path = output_dir;
  if (url == "/") {
    out_path /= "index.html";
  } else {
    std::string path_str = url.substr(1);
    out_path /= path_str;
    out_path += "/index.html";
  }

  write_file(out_path, html);
}

void SiteBuilder::build_all() {
  auto start = std::chrono::high_resolution_clock::now();

  std::cout << "\n"
            << termcolor::bright_cyan << "🔨 Building pages" << termcolor::reset
            << "\n";

  int success_count = 0;
  int error_count = 0;

  for (const auto &[url, page] : pages) {
    try {
      build_page(url);
      success_count++;
      std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
                << termcolor::white << url << termcolor::reset << "\n";
    } catch (const std::exception &e) {
      error_count++;
      std::cerr << termcolor::bright_red << "  ✗ " << termcolor::reset
                << termcolor::white << url << termcolor::reset
                << termcolor::bright_blue << ": " << e.what()
                << termcolor::reset << "\n";
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "\n"
            << termcolor::bright_green << "✓ " << termcolor::reset << "Built "
            << termcolor::bright_white << success_count << termcolor::reset
            << " pages";
  if (error_count > 0) {
    std::cout << termcolor::bright_red << " (" << error_count << " errors)"
              << termcolor::reset;
  }
  std::cout << termcolor::bright_blue << " in " << duration.count() << "ms"
            << termcolor::reset << "\n";
}

void SiteBuilder::export_static_site() {
  auto total_start = std::chrono::high_resolution_clock::now();

  std::cout << "\n"
            << termcolor::bright_cyan
            << "╔═══════════════════════════════════════════╗\n"
            << "║        🚀 Building Static Site            ║\n"
            << "╚═══════════════════════════════════════════╝"
            << termcolor::reset << "\n";

  initialize_minification();

  if (fs::exists(output_dir)) {
    fs::remove_all(output_dir);
  }
  fs::create_directories(output_dir);

  build_all();

  if (fs::exists(static_dir)) {
    auto static_start = std::chrono::high_resolution_clock::now();

    std::cout << "\n"
              << termcolor::bright_cyan << "📦 Processing static files"
              << termcolor::reset << "\n";

    fs::path static_out = output_dir / "static";
    fs::create_directories(static_out);

    int css_count = 0, js_count = 0, html_count = 0, other_count = 0;

    for (const auto &entry : fs::recursive_directory_iterator(static_dir)) {
      if (!entry.is_regular_file())
        continue;

      fs::path relative = fs::relative(entry.path(), static_dir);
      fs::path out_path = static_out / relative;

      if (out_path.has_parent_path()) {
        fs::create_directories(out_path.parent_path());
      }

      std::string ext = entry.path().extension().string();

      if (ext == ".css") {

        std::string css_content = read_file(entry.path());
        std::string minified = minify_css_content(css_content);
        write_file(out_path, minified);
        css_count++;
        std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
                  << termcolor::white << relative.string()
                  << termcolor::bright_blue << " (minified)" << termcolor::reset
                  << "\n";
      } else if (ext == ".js") {

        std::string js_content = read_file(entry.path());
        std::string minified = minify_js_content(js_content);
        write_file(out_path, minified);
        js_count++;
        std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
                  << termcolor::white << relative.string()
                  << termcolor::bright_blue << " (minified)" << termcolor::reset
                  << "\n";
      } else if (ext == ".html") {

        std::string html_content = read_file(entry.path());
        std::string minified = minify_html_content(html_content);
        write_file(out_path, minified);
        html_count++;
        std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
                  << termcolor::white << relative.string()
                  << termcolor::bright_blue << " (minified)" << termcolor::reset
                  << "\n";
      }

      else {

        fs::copy_file(entry.path(), out_path,
                      fs::copy_options::overwrite_existing);
        other_count++;
        std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
                  << termcolor::white << relative.string() << termcolor::reset
                  << "\n";
      }
    }

    auto static_end = std::chrono::high_resolution_clock::now();
    auto static_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(static_end -
                                                              static_start);

    std::cout << termcolor::bright_green << "✓ " << termcolor::reset
              << "Processed " << (css_count + js_count + other_count)
              << " static files";
    if (minification_enabled) {
      std::cout << " (" << css_count << " CSS, " << js_count << " JS minified)";
    }
    std::cout << termcolor::bright_blue << " in " << static_duration.count()
              << "ms" << termcolor::reset << "\n";
  }

  auto total_end = std::chrono::high_resolution_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      total_end - total_start);

  std::cout << "\n"
            << termcolor::bright_green
            << "╔═══════════════════════════════════════════╗\n"
            << "║           ✨ Build Complete!              ║\n"
            << "╠═══════════════════════════════════════════╣"
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "Output: " << termcolor::bright_white << std::setw(32)
            << std::left << output_dir.string() << termcolor::reset
            << termcolor::bright_green << "║" << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "Time:   " << termcolor::bright_white << std::setw(32)
            << std::left << (std::to_string(total_duration.count()) + "ms")
            << termcolor::reset << termcolor::bright_green << "║"
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "Pages:  " << termcolor::bright_white << std::setw(32)
            << std::left << std::to_string(pages.size()) << termcolor::reset
            << termcolor::bright_green << "║" << termcolor::reset << "\n";
  std::cout << termcolor::bright_green
            << "╚═══════════════════════════════════════════╝"
            << termcolor::reset << "\n\n";
}