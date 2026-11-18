---
title: "Lectern in five minutes"
theme: auto
aspect: "16:9"
---

# Lectern in five minutes

## Slides that are just Markdown

Note:
Open the same file in any Markdown editor. Use Ctrl+Enter to present from the current slide.

---

## One file, many slides

- A line with only `---` starts a new slide.
- Front matter sets the title, theme, and aspect ratio.
- Lines after `Note:` appear in the editor’s speaker notes panel.

Note:
Use the sidebar to jump to a slide, or move the cursor to it in the editor.

---

> Write for the audience. Keep the speaking cues in your notes.

---

## Code stays readable

```sh
lectern-omarchy check deck.md --json
lectern-omarchy export deck.md handout.pdf
```

---

## Present from the keyboard

| Key | Action |
| --- | --- |
| Space or → | Next slide |
| ← | Previous slide |
| Home or End | First or last slide |
| Esc | Leave the presentation |

---

## Share a PDF

Use **Export PDF** to create one page per slide. The PDF shows the same slide content as the fullscreen presentation.

Note:
Review the slide preview before exporting.
