#ifndef HTML_MINIFIER_HPP
#define HTML_MINIFIER_HPP

#include <string>

struct HtmlMinifierOptions {
  bool remove_comments = true;
  bool collapse_whitespace = true;
  bool remove_empty_attributes = true;
  bool minify_inline_css = true;
  bool minify_inline_js = true;
  bool preserve_line_breaks = false;
};

class HtmlMinifier {
public:
  using Options = HtmlMinifierOptions;

  static std::string minify(const std::string &html,
                            const Options &opts = Options());

private:
  static bool is_whitespace(char c);
  static bool is_pre_tag(const std::string &tag);
  static bool should_preserve_whitespace(const std::string &tag);
  static std::string minify_css(const std::string &css);
  static std::string minify_js(const std::string &js);
};

#endif // HTML_MINIFIER_HPP