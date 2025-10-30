// Import terser and csso
const Terser = require("terser");
const csso = require("csso");

// More robust HTML minifier
Terser.minify("./tests/test.js", {
  compress: {
    dead_code: true,
    drop_console: false,
    drop_debugger: true,
    keep_classnames: false,
    keep_fargs: true,
    keep_fnames: false,
    keep_infinity: false,
  },
  mangle: {
    toplevel: false,
    keep_classnames: false,
    keep_fnames: false,
  },
  format: {
    comments: false,
  },
});
