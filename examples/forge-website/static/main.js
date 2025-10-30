// Intercept all link clicks
document.addEventListener("click", (e) => {
  const link = e.target.closest("a");

  // Only handle internal links
  if (link && link.href.startsWith(window.location.origin)) {
    e.preventDefault();
    navigateTo(link.href);
  }
});

async function navigateTo(url) {
  // Update URL without page reload
  history.pushState(null, "", url);

  // Fetch and render new content
  await loadPage(url);
}

async function loadPage(url) {
  try {
    const response = await fetch(url);
    const html = await response.text();

    // Parse the HTML
    const parser = new DOMParser();
    const doc = parser.parseFromString(html, "text/html");

    // Replace content (adjust selector to your needs)
    const newContent = doc.querySelector("main"); // or '#content', 'body', etc.
    const oldContent = document.querySelector("main");

    if (newContent && oldContent) {
      oldContent.innerHTML = newContent.innerHTML;
    }

    // Update page title
    document.title = doc.title;
  } catch (error) {
    console.error("Navigation failed:", error);
    // Fallback to normal navigation
    window.location.href = url;
  }
}

// Handle browser back/forward buttons
window.addEventListener("popstate", () => {
  loadPage(window.location.href);
});
