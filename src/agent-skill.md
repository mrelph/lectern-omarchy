---
name: lectern-omarchy
description: Inspect, check, and export Markdown presentation decks with Lectern on Omarchy.
---

Use `lectern-omarchy info deck.md --json` for slide counts and headings,
`lectern-omarchy check deck.md --json` to find empty slides, unclosed code
fences, and unknown aspect or theme values, and
`lectern-omarchy export deck.md slides.pdf --json` to render a PDF.

A line containing only `---` splits slides outside fenced code.
An opening YAML block can set the title, theme, and aspect.
Put speaker notes after a `Note:` line within each slide.

Use `lectern-omarchy notes get deck.md 2 --json` to read a slide’s speaker
notes. To replace them, pass text to `lectern-omarchy notes set deck.md 2 TEXT`,
or use `-` to read multiline notes from standard input. Pass an empty string
to clear the notes.

Add `--notes` to PDF export for a portrait speaker-notes handout.

Create a deck with `lectern-omarchy new deck.md --title "Talk" --json`.
Slide numbers start at 1. Use `slides deck.md` for an outline and
`get deck.md N` for a slide's Markdown, including notes.

    lectern-omarchy add deck.md --at 2 --file slide.md --json
    lectern-omarchy set deck.md 2 - --json < replacement.md
    lectern-omarchy move deck.md 2 1 --json
    lectern-omarchy remove deck.md 2 --json

Omit `--at` to append. Supply one slide per edit. Use `themes` to list
presets, `theme deck.md NAME [--slide N]` to apply one, and
`font deck.md FONT [--code FONT]` to change fonts. Use
`media add deck.md FILE --slide N` to copy and link local media.

For MCP clients, configure `lectern-omarchy` as the executable with
`["mcp"]` as its arguments and stdio as the transport. Relative paths
start in the server's working directory. Discover available tools with
`tools/list` and read successful tool results from `structuredContent`.


Use `validate deck.md --json` to check missing media and content density before a presentation. Export images to a new directory with `export deck.md --png slides`. Use `--theme NAME` to render one export with a different deck theme. A slide-specific theme directive takes precedence. Use `open deck.md --slide 2 --present` to present from a particular slide.

Front matter supports `title`, `aspect` (16:9 or 4:3), `theme`, `transition` (fade, slide, none), `font`, `font-size`, and `code-font`. Themes include auto, light, dark, warm-paper, high-contrast, bold-color, minimal-mono, blocks-light, and blocks-dark. Copy images and videos into the deck with the media command. An optional Markdown image title such as `"50% right"` sets placement.
