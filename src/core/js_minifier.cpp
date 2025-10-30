#include "js_minifier.hpp"
#include "minifiers.h"
#include <fstream>
#include <iostream>
#include <sstream>

JSMinifier::JSMinifier() : rt(nullptr), ctx(nullptr), initialized(false) {}

JSMinifier::~JSMinifier() {
  if (ctx)
    JS_FreeContext(ctx);
  if (rt)
    JS_FreeRuntime(rt);
}

bool JSMinifier::initialize() {
  rt = JS_NewRuntime();
  if (!rt) {
    std::cerr << "Failed to create QuickJS runtime" << std::endl;
    return false;
  }

  ctx = JS_NewContext(rt);
  if (!ctx) {
    std::cerr << "Failed to create QuickJS context" << std::endl;
    return false;
  }

  std::string bundle_file(
      reinterpret_cast<const char *>(assets_minifiers_minifiers_bundle_js),
      assets_minifiers_minifiers_bundle_js_len);

  if (bundle_file.empty()) {
    std::cerr << "Warning: minifiers_bundle.js is empty." << std::endl;
    return false;
  }

  std::cout << "Loading minifiers bundle (" << bundle_file.length()
            << " bytes)..." << std::endl;

  JSValue result = JS_Eval(ctx, bundle_file.c_str(), bundle_file.length(),
                           "minifiers_bundle.js", JS_EVAL_TYPE_GLOBAL);

  if (JS_IsException(result)) {
    JSValue exception = JS_GetException(ctx);
    const char *error = JS_ToCString(ctx, exception);
    std::cerr << "Error loading minifiers: "
              << (error ? error : "Unknown error") << std::endl;
    JS_FreeCString(ctx, error);
    JS_FreeValue(ctx, exception);
    JS_FreeValue(ctx, result);
    return false;
  }

  JS_FreeValue(ctx, result);

  const char *setupCode = R"(
    globalThis.__minifyJS = function(code) {
      let result = null;
      let error = null;
      
      Terser.minify(code, {
        compress: {
          dead_code: true,
          drop_console: false,
          drop_debugger: true,
          keep_classnames: false,
          keep_fargs: true,
          keep_fnames: false,
          keep_infinity: false
        },
        mangle: {
          toplevel: false,
          keep_classnames: false,
          keep_fnames: false
        },
        format: {
          comments: false
        }
      }).then(r => {
        result = r.code;
      }).catch(e => {
        error = e.message;
      });
      
      return { result: () => result, error: () => error };
    };
    
    globalThis.__minifyCSS = function(code) {
      try {
        const result = csso.minify(code, {
          restructure: true,
          forceMediaMerge: false,
          comments: false
        });
        return result.css || "";
      } catch (e) {
        throw new Error("CSS minification failed: " + e.message);
      }
    };
    
    globalThis.__minifyHTML = function(code) {
      try {
        return minifyHTML(code);
      } catch (e) {
        throw new Error("HTML minification failed: " + e.message);
      }
    };
  )";

  JSValue setupResult = JS_Eval(ctx, setupCode, strlen(setupCode), "<setup>",
                                JS_EVAL_TYPE_GLOBAL);

  if (JS_IsException(setupResult)) {
    JSValue exception = JS_GetException(ctx);
    const char *error = JS_ToCString(ctx, exception);
    std::cerr << "Error setting up minifiers: " << (error ? error : "Unknown")
              << std::endl;
    JS_FreeCString(ctx, error);
    JS_FreeValue(ctx, exception);
    JS_FreeValue(ctx, setupResult);
    return false;
  }

  JS_FreeValue(ctx, setupResult);

  std::cout << "All minifiers loaded successfully!" << std::endl;
  initialized = true;
  return true;
}

std::string JSMinifier::escapeForJS(const std::string &str) {
  std::string result;
  result.reserve(str.length() * 1.1);

  for (char c : str) {
    switch (c) {
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    case '`':
      result += "\\`";
      break;
    case '$':
      result += "\\$";
      break;
    default:
      result += c;
      break;
    }
  }

  return result;
}

std::string JSMinifier::evaluateJS(const std::string &code) {
  JSValue result =
      JS_Eval(ctx, code.c_str(), code.length(), "<eval>", JS_EVAL_TYPE_GLOBAL);

  if (JS_IsException(result)) {
    JSValue exception = JS_GetException(ctx);
    const char *error = JS_ToCString(ctx, exception);
    std::string errorMsg = error ? error : "Unknown error";
    JS_FreeCString(ctx, error);
    JS_FreeValue(ctx, exception);
    JS_FreeValue(ctx, result);
    throw std::runtime_error("JS Error: " + errorMsg);
  }

  const char *str = JS_ToCString(ctx, result);
  std::string output = str ? str : "";
  JS_FreeCString(ctx, str);
  JS_FreeValue(ctx, result);

  return output;
}

std::optional<std::string> JSMinifier::minifyJS(const std::string &jsCode) {
  if (!initialized) {
    return std::nullopt;
  }

  try {

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue minifyFunc = JS_GetPropertyStr(ctx, global, "__minifyJS");

    if (!JS_IsFunction(ctx, minifyFunc)) {
      std::cerr << "__minifyJS function not found" << std::endl;
      JS_FreeValue(ctx, minifyFunc);
      JS_FreeValue(ctx, global);
      return std::nullopt;
    }

    JSValue jsCodeValue = JS_NewStringLen(ctx, jsCode.c_str(), jsCode.length());

    JSValue args[1] = {jsCodeValue};
    JSValue promiseResult = JS_Call(ctx, minifyFunc, global, 1, args);

    JS_FreeValue(ctx, jsCodeValue);
    JS_FreeValue(ctx, minifyFunc);
    JS_FreeValue(ctx, global);

    if (JS_IsException(promiseResult)) {
      JSValue exception = JS_GetException(ctx);
      const char *error = JS_ToCString(ctx, exception);
      std::cerr << "JS Error: " << (error ? error : "Unknown") << std::endl;
      JS_FreeCString(ctx, error);
      JS_FreeValue(ctx, exception);
      JS_FreeValue(ctx, promiseResult);
      return std::nullopt;
    }

    JSContext *ctx1;
    int err;
    for (int i = 0; i < 100; i++) {
      err = JS_ExecutePendingJob(rt, &ctx1);
      if (err <= 0) {
        if (err < 0) {
          std::cerr << "Promise job execution failed" << std::endl;
          JS_FreeValue(ctx, promiseResult);
          return std::nullopt;
        }
        break;
      }
    }

    JSValue resultFunc = JS_GetPropertyStr(ctx, promiseResult, "result");
    JSValue finalResult = JS_Call(ctx, resultFunc, promiseResult, 0, nullptr);

    JSValue errorFunc = JS_GetPropertyStr(ctx, promiseResult, "error");
    JSValue errorResult = JS_Call(ctx, errorFunc, promiseResult, 0, nullptr);

    bool hasError = !JS_IsNull(errorResult);

    if (hasError) {
      const char *errorStr = JS_ToCString(ctx, errorResult);
      std::cerr << "Minification error: " << (errorStr ? errorStr : "Unknown")
                << std::endl;
      JS_FreeCString(ctx, errorStr);
    }

    JS_FreeValue(ctx, errorFunc);
    JS_FreeValue(ctx, errorResult);
    JS_FreeValue(ctx, resultFunc);
    JS_FreeValue(ctx, promiseResult);

    if (hasError || JS_IsNull(finalResult)) {
      JS_FreeValue(ctx, finalResult);
      return std::nullopt;
    }

    const char *str = JS_ToCString(ctx, finalResult);
    std::string output = str ? str : "";
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, finalResult);

    return output;
  } catch (const std::exception &e) {
    std::cerr << "JS minification error: " << e.what() << std::endl;
    return std::nullopt;
  }
}

std::optional<std::string> JSMinifier::minifyCSS(const std::string &cssCode) {
  if (!initialized) {
    return std::nullopt;
  }

  try {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue minifyFunc = JS_GetPropertyStr(ctx, global, "__minifyCSS");

    if (!JS_IsFunction(ctx, minifyFunc)) {
      std::cerr << "__minifyCSS function not found" << std::endl;
      JS_FreeValue(ctx, minifyFunc);
      JS_FreeValue(ctx, global);
      return std::nullopt;
    }

    JSValue jsCodeValue =
        JS_NewStringLen(ctx, cssCode.c_str(), cssCode.length());
    JSValue args[1] = {jsCodeValue};
    JSValue result = JS_Call(ctx, minifyFunc, global, 1, args);

    JS_FreeValue(ctx, jsCodeValue);
    JS_FreeValue(ctx, minifyFunc);
    JS_FreeValue(ctx, global);

    if (JS_IsException(result)) {
      JSValue exception = JS_GetException(ctx);
      const char *error = JS_ToCString(ctx, exception);
      std::cerr << "CSS minification error: " << (error ? error : "Unknown")
                << std::endl;
      JS_FreeCString(ctx, error);
      JS_FreeValue(ctx, exception);
      JS_FreeValue(ctx, result);
      return std::nullopt;
    }

    const char *str = JS_ToCString(ctx, result);
    std::string output = str ? str : "";
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, result);

    return output;
  } catch (const std::exception &e) {
    std::cerr << "CSS minification error: " << e.what() << std::endl;
    return std::nullopt;
  }
}

std::optional<std::string> JSMinifier::minifyHTML(const std::string &htmlCode) {
  if (!initialized) {
    std::cerr << "HTML minifier not initialized" << std::endl;
    return std::nullopt;
  }

  try {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue minifyFunc = JS_GetPropertyStr(ctx, global, "__minifyHTML");

    if (!JS_IsFunction(ctx, minifyFunc)) {
      std::cerr << "__minifyHTML function not found" << std::endl;
      JS_FreeValue(ctx, minifyFunc);
      JS_FreeValue(ctx, global);
      return std::nullopt;
    }

    JSValue jsCodeValue =
        JS_NewStringLen(ctx, htmlCode.c_str(), htmlCode.length());
    JSValue args[1] = {jsCodeValue};
    JSValue result = JS_Call(ctx, minifyFunc, global, 1, args);

    JS_FreeValue(ctx, jsCodeValue);
    JS_FreeValue(ctx, minifyFunc);
    JS_FreeValue(ctx, global);

    if (JS_IsException(result)) {
      JSValue exception = JS_GetException(ctx);
      const char *error = JS_ToCString(ctx, exception);
      std::cerr << "HTML minification error: " << (error ? error : "Unknown")
                << std::endl;
      JS_FreeCString(ctx, error);
      JS_FreeValue(ctx, exception);
      JS_FreeValue(ctx, result);
      return std::nullopt;
    }

    const char *str = JS_ToCString(ctx, result);
    std::string output = str ? str : "";
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, result);

    return output;
  } catch (const std::exception &e) {
    std::cerr << "HTML minification error: " << e.what() << std::endl;
    return std::nullopt;
  }
}
