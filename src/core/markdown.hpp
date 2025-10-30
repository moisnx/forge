#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <string>

class MarkdownProcessor {
public:
  static std::string to_html(const std::string &markdown);
};

#endif