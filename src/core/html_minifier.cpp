#include "html_minifier.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <unordered_set>

bool HtmlMinifier::is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool HtmlMinifier::is_pre_tag(const std::string &tag) {
  return tag == "pre" || tag == "textarea" || tag == "script" ||
         tag == "style" || tag == "code";
}

bool HtmlMinifier::should_preserve_whitespace(const std::string &tag) {
  return tag == "pre" || tag == "textarea";
}

std::string HtmlMinifier::minify_css(const std::string &css) {
  std::string result = css;

  result = std::regex_replace(
      result, std::regex(R"(/\*[^*]*\*+(?:[^/*][^*]*\*+)*/)"), "");

  result =
      std::regex_replace(result, std::regex(R"(\s*([{}:;,>+~()])\s*)"), "$1");

  result = std::regex_replace(result, std::regex(R"(\s+([>+~])\s+)"), "$1");

  result = std::regex_replace(result, std::regex(R"(\s+)"), " ");

  result = std::regex_replace(result, std::regex(R"(:\s+)"), ":");

  result = std::regex_replace(result, std::regex(R"(\s+\{)"), "{");

  result = std::regex_replace(result, std::regex(R"(\}\s+)"), "}");

  result = std::regex_replace(result, std::regex(R"(^\s+|\s+$)"), "");

  return result;
}

std::string HtmlMinifier::minify_js(const std::string &js) {
  std::string result;
  result.reserve(js.length());

  bool in_string = false;
  bool in_single_quote = false;
  bool in_template = false;
  bool in_regex = false;
  bool in_multi_comment = false;
  char prev = '\0';
  char prev_non_ws = '\0';

  auto needs_space = [](char last, char next) {
    if (std::isalnum(last) || last == '_' || last == '$') {
      return std::isalnum(next) || next == '_' || next == '$';
    }

    return false;
  };

  for (size_t i = 0; i < js.length(); i++) {
    char c = js[i];
    char next = (i + 1 < js.length()) ? js[i + 1] : '\0';

    if (!in_string && !in_single_quote && !in_template && !in_multi_comment &&
        next == '*' && c == '/') {
      in_multi_comment = true;
      i++;
      continue;
    }

    if (in_multi_comment) {
      if (c == '*' && next == '/') {
        in_multi_comment = false;
        i++;
      }
      continue;
    }

    if (!in_string && !in_single_quote && !in_template && c == '/' &&
        next == '/' && prev != ':' && prev != 'h') {
      while (i < js.length() && js[i] != '\n' && js[i] != '\r') {
        i++;
      }
      continue;
    }

    if (!in_single_quote && !in_template && c == '"' && prev != '\\') {
      in_string = !in_string;
      result += c;
    } else if (!in_string && !in_template && c == '\'' && prev != '\\') {
      in_single_quote = !in_single_quote;
      result += c;
    } else if (!in_string && !in_single_quote && c == '`' && prev != '\\') {
      in_template = !in_template;
      result += c;
    }

    else if (in_string || in_single_quote || in_template) {
      result += c;
    }

    else if (is_whitespace(c)) {

      if (!result.empty() && !is_whitespace(result.back())) {
        char last = result.back();

        size_t j = i;
        while (j < js.length() && is_whitespace(js[j])) {
          j++;
        }

        if (j < js.length()) {
          char next_char = js[j];

          if (needs_space(last, next_char)) {
            result += ' ';
          }
        }
      }
    } else {
      result += c;
      prev_non_ws = c;
    }

    prev = c;
  }

  return result;
}

std::string HtmlMinifier::minify(const std::string &html, const Options &opts) {
  std::ostringstream output;
  size_t i = 0;
  size_t len = html.length();

  std::vector<std::string> tag_stack;
  bool in_tag = false;
  std::string current_tag;
  bool pending_space = false;

  auto is_inline = [](const std::string &tag) {
    static const std::unordered_set<std::string> inline_elements = {
        "a",     "span", "strong", "em",   "b",   "i",      "u",
        "small", "code", "abbr",   "cite", "kbd", "mark",   "q",
        "s",     "sub",  "sup",    "time", "var", "button", "label"};
    return inline_elements.find(tag) != inline_elements.end();
  };

  auto in_preserve_whitespace_tag = [&tag_stack]() {
    for (const auto &tag : tag_stack) {
      if (should_preserve_whitespace(tag)) {
        return true;
      }
    }
    return false;
  };

  auto last_char = [&output]() -> char {
    std::string str = output.str();
    return str.empty() ? '\0' : str.back();
  };

  auto remove_trailing_space = [&output]() {
    std::string str = output.str();
    if (!str.empty() && str.back() == ' ') {
      str.pop_back();
      output.str("");
      output << str;
    }
  };

  while (i < len) {
    char c = html[i];

    if (opts.remove_comments && i + 4 <= len && html.substr(i, 4) == "<!--") {
      size_t end = html.find("-->", i + 4);
      if (end != std::string::npos) {

        if (i + 5 < len && html[i + 4] == '[') {
          size_t conditional_end = html.find("<![endif]", i);
          if (conditional_end != std::string::npos &&
              conditional_end < end + 20) {
            while (i <= conditional_end + 9 && i < len) {
              output << html[i++];
            }
            continue;
          }
        }
        i = end + 3;
        pending_space = false;
        continue;
      }
    }

    if (i + 9 <= len && html.substr(i, 9) == "<!DOCTYPE") {
      size_t end = html.find('>', i);
      if (end != std::string::npos) {
        output << html.substr(i, end - i + 1);
        i = end + 1;
        pending_space = false;
        continue;
      }
    }

    if (c == '<') {
      pending_space = false;
      in_tag = true;
      current_tag.clear();
      output << c;
      i++;

      bool is_closing = (i < len && html[i] == '/');
      if (is_closing) {
        output << '/';
        i++;
      }

      while (i < len && !is_whitespace(html[i]) && html[i] != '>' &&
             html[i] != '/') {
        char tag_char = std::tolower(html[i]);
        current_tag += tag_char;
        output << html[i];
        i++;
      }

      if (is_closing) {
        if (!tag_stack.empty() && tag_stack.back() == current_tag) {
          tag_stack.pop_back();
        }
      } else {

        static const std::unordered_set<std::string> void_tags = {
            "area",  "base", "br",   "col",   "embed",  "hr",    "img",
            "input", "link", "meta", "param", "source", "track", "wbr"};

        if (void_tags.find(current_tag) == void_tags.end()) {
          tag_stack.push_back(current_tag);
        }
      }

      bool in_attr_value = false;
      char quote_char = '\0';
      bool last_was_space = false;

      while (i < len && html[i] != '>') {
        char ch = html[i];

        if (ch == '/' && i + 1 < len && html[i + 1] == '>') {
          if (!tag_stack.empty() && tag_stack.back() == current_tag) {
            tag_stack.pop_back();
          }
          remove_trailing_space();
          output << "/>";
          i += 2;
          in_tag = false;
          break;
        }

        if (!in_attr_value && (ch == '"' || ch == '\'')) {
          in_attr_value = true;
          quote_char = ch;
          output << ch;
          last_was_space = false;
        } else if (in_attr_value && ch == quote_char) {
          in_attr_value = false;
          quote_char = '\0';
          output << ch;
          last_was_space = false;
        } else if (in_attr_value) {

          output << ch;
          last_was_space = false;
        } else if (opts.collapse_whitespace && is_whitespace(ch)) {

          if (!last_was_space) {
            output << ' ';
            last_was_space = true;
          }
        } else {
          output << ch;
          last_was_space = false;
        }
        i++;
      }

      if (i < len && html[i] == '>') {
        remove_trailing_space();
        output << '>';
        i++;
        in_tag = false;

        if (!is_closing) {
          if (current_tag == "script") {
            size_t script_end = html.find("</script>", i);
            if (script_end != std::string::npos) {
              std::string script_content = html.substr(i, script_end - i);

              size_t start = 0;
              while (start < script_content.length() &&
                     is_whitespace(script_content[start])) {
                start++;
              }
              size_t end = script_content.length();
              while (end > start && is_whitespace(script_content[end - 1])) {
                end--;
              }
              script_content = script_content.substr(start, end - start);

              if (opts.minify_inline_js && !script_content.empty()) {
                script_content = minify_js(script_content);
              }
              output << script_content;
              i = script_end;
            }
          } else if (current_tag == "style") {
            size_t style_end = html.find("</style>", i);
            if (style_end != std::string::npos) {
              std::string style_content = html.substr(i, style_end - i);

              size_t start = 0;
              while (start < style_content.length() &&
                     is_whitespace(style_content[start])) {
                start++;
              }
              size_t end = style_content.length();
              while (end > start && is_whitespace(style_content[end - 1])) {
                end--;
              }
              style_content = style_content.substr(start, end - start);

              if (opts.minify_inline_css && !style_content.empty()) {
                style_content = minify_css(style_content);
              }
              output << style_content;
              i = style_end;
            }
          }
        }
      }
      continue;
    }

    if (in_preserve_whitespace_tag()) {

      output << c;
      i++;
    } else if (opts.collapse_whitespace && is_whitespace(c)) {

      pending_space = true;
      i++;
    } else {

      char prev = last_char();

      if (pending_space && prev != '\0' && prev != '>' && prev != '<') {

        bool in_inline = false;
        for (const auto &tag : tag_stack) {
          if (is_inline(tag)) {
            in_inline = true;
            break;
          }
        }

        if (in_inline || (std::isalnum(prev) && std::isalnum(c))) {
          output << ' ';
        }
      }

      pending_space = false;
      output << c;
      i++;
    }
  }

  return output.str();
}