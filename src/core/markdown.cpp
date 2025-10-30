#include "markdown.hpp"
#include "md4c-html.h"
#include "md4c.h"
#include <cstring>

// Callback for md4c to write HTML output
static void process_output(const MD_CHAR *text, MD_SIZE size, void *userdata) {
  std::string *output = static_cast<std::string *>(userdata);
  output->append(text, size);
}

std::string MarkdownProcessor::to_html(const std::string &markdown) {
  std::string html;

  // Configure parser for GitHub Flavored Markdown
  unsigned parser_flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                          MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEURLAUTOLINKS;

  unsigned renderer_flags = MD_HTML_FLAG_DEBUG | MD_HTML_FLAG_SKIP_UTF8_BOM;

  // Parse markdown and convert to HTML
  int result = md_html(markdown.c_str(), markdown.size(), process_output, &html,
                       parser_flags, renderer_flags);

  if (result != 0) {
    return "<p>Error parsing markdown</p>";
  }

  return html;
}