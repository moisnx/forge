#pragma once
#include "quickjs.h"
#include <optional>
#include <string>

class JSMinifier {
private:
  JSRuntime *rt;
  JSContext *ctx;
  bool initialized;

  std::string evaluateJS(const std::string &code);
  std::string escapeForJS(const std::string &str);

public:
  JSMinifier();
  ~JSMinifier();

  bool initialize();
  std::optional<std::string> minifyJS(const std::string &jsCode);
  std::optional<std::string> minifyCSS(const std::string &cssCode);
  std::optional<std::string> minifyHTML(const std::string &htmlCode);
};