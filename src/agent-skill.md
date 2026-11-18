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
