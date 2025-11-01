const Terser = require("terser");
const csso = require("csso");

// Enhanced HTML minifier with inline CSS/JS minification
function minifyHTML(html, options = {}) {
  let result = html;

  // Step 1: Preserve and minify <script> tags
  const scriptBlocks = [];
  let scriptIndex = 0;

  result = result.replace(
    /(<script\b[^>]*>)([\s\S]*?)(<\/script>)/gi,
    (match, openTag, content, closeTag) => {
      // Check if script has src attribute (external script)
      const hasSrc = /\bsrc\s*=/i.test(openTag);

      if (!hasSrc && content.trim()) {
        // Minify inline JavaScript
        try {
          // Basic JavaScript minification - remove comments and extra whitespace
          let minified = content
            // Remove single-line comments
            .replace(/\/\/[^\n]*/g, "")
            // Remove multi-line comments
            .replace(/\/\*[\s\S]*?\*\//g, "")
            // Replace multiple spaces with single space
            .replace(/\s+/g, " ")
            // Remove spaces around operators and punctuation
            .replace(/\s*([=+\-*/%<>!&|,;:?{}()\[\]])\s*/g, "$1")
            // Trim
            .trim();

          content = minified;
        } catch (e) {
          console.warn("Failed to minify script block:", e.message);
        }
      }

      scriptBlocks.push(openTag + content + closeTag);
      return `___PRESERVE_SCRIPT_${scriptIndex++}___`;
    }
  );

  // Step 2: Preserve and minify <style> tags
  const styleBlocks = [];
  let styleIndex = 0;

  result = result.replace(
    /(<style\b[^>]*>)([\s\S]*?)(<\/style>)/gi,
    (match, openTag, content, closeTag) => {
      if (content.trim()) {
        // Minify inline CSS
        try {
          const minified = csso.minify(content, {
            restructure: true,
            comments: false,
          });
          content = minified.css;
        } catch (e) {
          console.warn("Failed to minify style block:", e.message);
        }
      }

      styleBlocks.push(openTag + content + closeTag);
      return `___PRESERVE_STYLE_${styleIndex++}___`;
    }
  );

  // Step 3: Minify inline style attributes
  result = result.replace(/\sstyle\s*=\s*"([^"]*)"/gi, (match, content) => {
    if (content.trim()) {
      try {
        // Wrap in a dummy selector for csso
        const wrapped = `x{${content}}`;
        const minified = csso.minify(wrapped, {
          restructure: false,
          comments: false,
        });
        // Extract the content back
        const extracted = minified.css.match(/x\{(.*)\}/);
        if (extracted && extracted[1]) {
          content = extracted[1];
        }
      } catch (e) {
        // Fallback: basic whitespace removal
        content = content.replace(/\s+/g, " ").trim();
      }
    }
    return ` style="${content}"`;
  });

  // Step 4: Minify inline event handlers (onclick, onload, etc.)
  result = result.replace(
    /\s(on[a-z]+)\s*=\s*"([^"]*)"/gi,
    (match, attr, content) => {
      if (content.trim()) {
        try {
          // Basic JS minification for inline handlers
          let minified = content
            .replace(/\/\/[^\n]*/g, "")
            .replace(/\/\*[\s\S]*?\*\//g, "")
            .replace(/\s+/g, " ")
            .replace(/\s*([=+\-*/%<>!&|,;:?{}()\[\]])\s*/g, "$1")
            .trim();

          content = minified;
        } catch (e) {
          // Fallback: basic whitespace removal
          content = content.replace(/\s+/g, " ").trim();
        }
      }
      return ` ${attr}="${content}"`;
    }
  );

  // Step 5: Preserve <pre> and <textarea> content
  const preserveBlocks = [];
  let preserveIndex = 0;

  result = result.replace(/(<pre\b[^>]*>)([\s\S]*?)(<\/pre>)/gi, (match) => {
    preserveBlocks.push(match);
    return `___PRESERVE_${preserveIndex++}___`;
  });

  result = result.replace(
    /(<textarea\b[^>]*>)([\s\S]*?)(<\/textarea>)/gi,
    (match) => {
      preserveBlocks.push(match);
      return `___PRESERVE_${preserveIndex++}___`;
    }
  );

  // Step 6: Remove HTML comments (keep IE conditionals)
  result = result.replace(/<!--(?!\[if)(?!<!\[endif)[\s\S]*?-->/g, "");

  // Step 7: Normalize whitespace
  result = result.replace(/\s+/g, " ");

  // Step 8: Remove whitespace around tags
  result = result.replace(/>\s+/g, ">");
  result = result.replace(/\s+</g, "<");

  // Step 9: Remove whitespace inside tags
  result = result.replace(/\s+>/g, ">");

  // Step 10: Trim the entire result
  result = result.trim();

  // Step 11: Restore preserved blocks (pre, textarea)
  preserveBlocks.forEach((block, index) => {
    result = result.replace(`___PRESERVE_${index}___`, block);
  });

  // Step 12: Restore script blocks
  scriptBlocks.forEach((block, index) => {
    result = result.replace(`___PRESERVE_SCRIPT_${index}___`, block);
  });

  // Step 13: Restore style blocks
  styleBlocks.forEach((block, index) => {
    result = result.replace(`___PRESERVE_STYLE_${index}___`, block);
  });

  // Step 14: Safely remove quotes from attributes
  // Only remove quotes from simple values that don't need them
  result = result.replace(
    /\s([a-zA-Z-]+)="([a-zA-Z0-9_\-]+)"/g,
    (match, attr, value) => {
      // Keep quotes for certain attributes that should always have them
      const alwaysQuoted = [
        "style",
        "onclick",
        "onload",
        "onerror",
        "onchange",
        "onsubmit",
      ];
      if (alwaysQuoted.includes(attr.toLowerCase())) {
        return match;
      }
      // Remove quotes for simple values
      return ` ${attr}=${value}`;
    }
  );

  // Step 15: Remove empty attributes
  result = result.replace(/\s[a-zA-Z-]+=""/g, "");

  // Step 16: Collapse boolean attributes
  result = result.replace(/\s([a-zA-Z-]+)="\1"/g, " $1");

  return result;
}

// Export to global scope
globalThis.Terser = Terser;
globalThis.csso = csso;
globalThis.minifyHTML = minifyHTML;

// Also export as module if needed
if (typeof module !== "undefined" && module.exports) {
  module.exports = { minifyHTML, Terser, csso };
}
