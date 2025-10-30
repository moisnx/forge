// Import terser and csso
const Terser = require("terser");
const csso = require("csso");

// More robust HTML minifier
function minifyHTML(html, options = {}) {
  let result = html;

  // Step 1: Preserve content that should not be minified
  const preserveBlocks = [];
  let preserveIndex = 0;

  // Preserve <pre>, <textarea>, <script>, and <style> content
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

  result = result.replace(
    /(<script\b[^>]*>)([\s\S]*?)(<\/script>)/gi,
    (match) => {
      preserveBlocks.push(match);
      return `___PRESERVE_${preserveIndex++}___`;
    }
  );

  result = result.replace(
    /(<style\b[^>]*>)([\s\S]*?)(<\/style>)/gi,
    (match) => {
      preserveBlocks.push(match);
      return `___PRESERVE_${preserveIndex++}___`;
    }
  );

  // Step 2: Remove HTML comments (keep IE conditionals)
  result = result.replace(/<!--(?!\[if)(?!<!\[endif)[\s\S]*?-->/g, "");

  // Step 3: Normalize whitespace
  // Replace all sequences of whitespace (including newlines) with a single space
  result = result.replace(/\s+/g, " ");

  // Step 4: Remove whitespace around tags
  // Remove space after opening tags
  result = result.replace(/>\s+/g, ">");
  // Remove space before closing tags
  result = result.replace(/\s+</g, "<");

  // Step 5: Remove whitespace inside tags (between attributes)
  result = result.replace(/\s+>/g, ">");

  // Step 6: Trim the entire result
  result = result.trim();

  // Step 7: Restore preserved blocks
  preserveBlocks.forEach((block, index) => {
    result = result.replace(`___PRESERVE_${index}___`, block);
  });

  // Step 8: Remove quotes from attributes where safe (optional, can be aggressive)
  // Only remove quotes from simple alphanumeric values
  result = result.replace(/\s([a-zA-Z-]+)="([a-zA-Z0-9_\-\.]+)"/g, " $1=$2");

  // Step 9: Remove empty attributes
  result = result.replace(/\s[a-zA-Z-]+=""/g, "");

  // Step 10: Collapse boolean attributes
  result = result.replace(/\s([a-zA-Z-]+)="\1"/g, " $1");

  return result;
}

// Export to global scope
globalThis.Terser = Terser;
globalThis.csso = csso;
globalThis.minifyHTML = minifyHTML;
