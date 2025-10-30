# Forge

A modern, lightweight static site generator built in C++ with a custom template engine, hot-reload development server, and flexible content management for building fast static websites.

## Features

- **Fast Static Generation**: C++ performance for instant builds and quick regeneration
- **Custom Template Engine**: Jinja2-like syntax with variables, filters, loops, and conditionals
- **Content Collections**: Organize content by type (blog, projects) with automatic sorting
- **Hot-Reload Server**: Built-in development server with automatic rebuilds on file changes
- **Markdown & HTML Support**: Process Markdown with YAML frontmatter or use standalone HTML
- **Flexible Routing**: Clean URL structure (`/pages/about.md` → `/about`)
- **Zero Configuration**: Sensible defaults with optional `forge.yaml` customization

## Technical Highlights

- Custom template engine with variable interpolation, string filters, and control flow (`if`, `for`, `where`)
- Markdown processing powered by md4c library for fast, CommonMark-compliant parsing
- YAML frontmatter support for rich metadata (accessible as `{{ page.field }}` in templates)
- File system monitoring with efsw for automatic rebuilds during development
- HTTP server using cpp-httplib for live preview and hot-reload
- Modular architecture: `SiteBuilder`, `TemplateEngine`, `MarkdownProcessor`, `FrontMatter`, `SiteConfig`
- Content collections system with configurable sorting and filtering
- Pure static output with no runtime dependencies - deploy anywhere
