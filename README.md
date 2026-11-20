# Lectern for Omarchy

![Lectern showing the Markdown editor, rendered preview, and speaker notes](docs/screenshot.png)

Lectern edits and presents plain Markdown slide decks on Omarchy. Open a deck, edit it beside a live preview, present it fullscreen, or export it as PDF pages, notes handouts, or PNG images. The editor, presenter display, and exports share the same Qt renderer.

## Run

```sh
bin/build
build/lectern-omarchy open docs/example.md
bin/test
```

Build with GCC, make, qmake6, Qt Base, Qt Declarative, and md4c. The app uses Qt Wayland, Qt SVG, Qt Multimedia, FFmpeg, and the desktop portal at runtime.

```sh
(cd pkgbuild && makepkg)  # Build the package
bin/install              # Build and install through pacman
```

File selection uses the [XDG Desktop Portal file chooser](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html). The package adds **Lectern** to the Omarchy launcher and registers it as a Markdown file handler. The app follows Omarchy’s accent, Hyprland rounding, and language setting. English and Danish are bundled.

## Updates

Select **File → Updates** to check the latest stable [GitHub release](https://github.com/mrelph/lectern-omarchy/releases/latest). For a user-local installation at `~/.local/bin/lectern-omarchy`, select **Install update** when a matching Linux binary is available. Lectern verifies the GitHub release asset’s SHA-256 digest before replacing the executable. Restart Lectern to run the new version.

For a system installation, select **View release** and install the matching Arch package with `sudo pacman -U <package>`. The first release includes prebuilt x86_64 assets.

### Publishing a release

Set `VERSION`, commit, and tag that commit with the corresponding `vX.Y.Z` tag. Build the Arch package from that checkout and upload both the package and its executable to the stable GitHub release. Name the executable `lectern-omarchy-linux-x86_64` or `lectern-omarchy-linux-arm64` to match Qt’s reported build architecture.

## Deck format

Write Markdown in a UTF-8 file. A line containing only `---` starts a new slide outside fenced code. Start a speaker notes section with `Note:` or `Notes:`. Edit those notes in the panel below the preview. Lectern applies your notes when you leave the field or save. The fullscreen presentation and standard PDF omit them. **File → Export PDF with notes** creates a portrait handout with notes below each slide.

Open **File → Deck library** to search recent decks and folders you add. Lectern remembers the last saved deck and workspace view. Use **Ctrl+Shift+P** to search formatting, navigation, export, and presentation commands. **F1** opens the offline guide, shortcut list, and Markdown reference with copyable examples.

Choose **Theme** to see the current slide in every preset. Apply a theme to the whole deck or to the current slide. A slide override is stored as `<!-- theme: warm-paper -->` in the Markdown. The appearance toolbar also sets the deck font and base size.

An optional opening front matter block accepts `title`, `aspect` (`16:9` or `4:3`), `theme` (`auto`, `light`, `dark`, `warm-paper`, `high-contrast`, `bold-color`, `minimal-mono`, `blocks-light`, or `blocks-dark`), `font` (an installed family), `font-size` (12–96 design pixels, default 36), `code-font`, and `transition` (`fade`, `slide`, or `none`). Fonts come from the system. Relative image paths resolve alongside the saved Markdown file. See the [example deck](docs/example.md).

With a saved deck, press **Ctrl+Shift+I**, paste an image, or drop local media into the source editor. Lectern copies imported media into the adjacent `assets/` folder. A local MP4, MOV, or M4V plays on the audience display and appears as a poster frame in previews and exports. An image title such as `"50% right"` controls size and alignment.

Open decks reload when an external editor saves them. When a disk change collides with unsaved edits, **Save As** keeps your version in a separate file.

## Keyboard

| Key | Action |
| --- | --- |
| Ctrl+N / Ctrl+O | New / open |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo in the source editor |
| Ctrl+Shift+P | Find a command |
| Ctrl+Shift+L / Ctrl+Shift+O | Search the library / add a folder |
| Ctrl+1 / Ctrl+2 / Ctrl+3 | Write / Split / Preview |
| Ctrl+Shift+N | Insert a slide after the current slide |
| Ctrl+Alt+↑ / Ctrl+Alt+↓ | Move the current slide |
| Ctrl+Shift+I | Insert an image or video |
| Ctrl+Alt+N / Ctrl+Alt+Shift+N | Toggle the notes panel / hide notes in source |
| Ctrl+Shift+E / Ctrl+Alt+Shift+E | Export a PDF / PNG images |
| Ctrl+Shift+Enter | Present from the first slide |
| Ctrl+Enter | Start or stop presenting |
| Ctrl+Alt+Enter | Open Presenter View or switch between Presenter View and slides on one display |
| Space | Start presenting when focus is outside the editor |
| → / ↓ / Space / Enter | Next slide while presenting |
| ← / ↑ | Previous slide while presenting |
| Home / End | First / last slide while presenting |
| Number + Enter | Jump to a slide |
| B / W | Toggle a black or white screen |
| ? | Show presentation shortcuts while presenting |
| Esc | Close shortcuts, cancel a slide jump, or leave presentation |
| F1 / ? | Keyboard help |
| Ctrl+Q | Quit |

## Presenting

Use **File → Keep screen awake when presenting** to set the power preference. Lectern remembers the setting.

Start with **Present** or **Ctrl+Enter**. With a second display, Lectern opens the fullscreen audience slides there and keeps Presenter View on the display with the editor. Presenter View shows the current and next slides, scrollable speaker notes, the local clock, and an elapsed timer you can reset.

On one display, **Presenter View** or **Ctrl+Alt+Enter** opens the presenter dashboard. Select **Show slides** to switch to fullscreen, then press **Ctrl+Alt+Enter** to return to the dashboard.

## Headless commands

```sh
lectern-omarchy info deck.md --json
lectern-omarchy check deck.md --json
lectern-omarchy export deck.md deck.pdf --json
lectern-omarchy export deck.md handout.pdf --notes
lectern-omarchy notes get deck.md 2 --json
lectern-omarchy notes set deck.md 2 "Remind the audience about questions"
lectern-omarchy export deck.md --png slide-images --theme warm-paper
lectern-omarchy open deck.md --slide 2 --present
lectern-omarchy new talk.md --title "Quarterly review"
lectern-omarchy add talk.md --file next-slide.md
lectern-omarchy help
lectern-omarchy mcp
lectern-omarchy skill install
```

Use `-` as the notes text to read multiline notes from stdin, or pass an empty string to clear the notes.

`check` and `validate` exit 1 for problems including missing media, unreadably dense slides, and invalid deck settings. **File → Check deck** runs the same checks in the editor. For PDF exports, `--dark` selects dark rendering for decks with `theme: auto`. Choose a new output path for each CLI export.

## Swift repository review

The original Lectern for macOS separates the deck parser and editor into `LecternCore`, the AppKit slide renderer and exporters into `LecternRender`, and the CLI and stdio MCP into `LecternTool`. The SwiftUI and AppKit frontend adds the folder library, native document handling, presentation controllers, and Ephy integration. The Linux project keeps the Markdown document contract and the shared renderer architecture. Qt renders the slides, so typography and layout can differ between platforms.

The Omarchy app uses Qt Quick, local Markdown files, Linux desktop portals, and a stdio MCP server. The macOS-only Ephy integration is outside the port’s scope.
