# WinDiaTweaks — porting notes

Goal: match [JaceThings/DiaTweaks](https://github.com/JaceThings/DiaTweaks) (macOS) on Windows —
unofficial UI tweaks for the Dia browser. Nothing in Dia's install folder is modified; uninstall = delete this folder.

Target: `TheBrowserCompany.Dia` 1.5.0.588, x64, packaged (MSIX), Swift + WinUI 3.

## Use

Open Dia through `bin\diatweaks.exe`, then **Dia → Settings → Patches**: every tweak has a switch there
(plus a drop-down or text box where it has an option). Changes apply instantly and are saved to `bin\tweaks.conf`.

The command line does the same:

```
bin\diatweaks.exe                  :: start Dia with tweaks (or reload if already running)
bin\diatweaks.exe list
bin\diatweaks.exe enable hideToolbar
bin\diatweaks.exe set windowMaterial acrylic
bin\diatweaks.exe disable all
bin\diatweaks.exe status
bin\diatweaks.exe dump             :: writes bin\tree.txt (Dia's UI tree, for making tweaks)
bin\diatweaks.exe probe Name Name* :: describes live elements (for making tweaks)
```

Tweaks need to be re-attached each time Dia starts: open Dia through `bin\diatweaks.exe`.

Build: `build.cmd` (close Dia first). It needs VS 2022 C++ tools + Windows SDK. The first build runs
`gen-winrt.cmd`, which makes C++/WinRT headers from the WinUI metadata inside Dia's own install
(`winmd\`, `gen\`); re-run it after a Dia update if the build complains.

## How it works

`diatweaks.exe` loads `DiaTweaksTap.dll` into the running Dia once; the DLL attaches through WinUI's XAML
diagnostics API. From there:

- `src/tap.cpp` tracks Dia's live UI tree and applies the property rules in `tweaks.conf`, putting Dia's
  own values back when a tweak is turned off.
- When Dia changes a property a tweak overrides (e.g. its page inset while the sidebar animates), WinUI
  notifies the engine synchronously and the tweak's value goes straight back before the frame is drawn,
  so there's no flash. Dia's new value is remembered so turning the tweak off restores it.
- `src/ui.cpp` uses real WinUI objects (C++/WinRT) for the **Settings → Patches** page (a NavigationViewItem
  added to Dia's Settings NavigationView, page built in Dia's card style), the window states rules can use
  (`sidebarOpen`, `newTab`, `showToolbar`), and the tweaks that need code.
- **Hidden toolbar** (and the three "show toolbar" tweaks) is done in code: a watcher thread checks the
  pointer every 8 ms and pings Dia's UI thread at once. Showing/hiding slides the toolbar *and the web page*
  together each frame (ease-out, 200 ms). While sliding, the page keeps its size, so Dia only moves its
  web-content window; the page is resized once, at the end of a reveal or the start of a hide. This is the
  sliding, not reflowing every frame. Measured: ~20 positions per slide,
  first movement ~10-25 ms after the pointer reaches the top. Typing in the address bar (Ctrl+L) also keeps
  the toolbar out.
- With the toolbar hidden (Hide toolbar / Arc URL bar), Dia's invisible window-drag strip over the old toolbar
  (`BoostBrowser.WindowCaptionInputSink`) is reshaped to cover only the sidebar, so the top of the page takes
  clicks; Dia's own shape comes back when the toolbar shows. On Dia's start page (New Tab), which is drawn in
  XAML, the toolbar slide runs as a compositor translation with the page height kept constant, so it doesn't
  re-lay out the start page every frame.
- **Safe mode** (as on macOS): if Dia closes twice in a row within a minute of starting with tweaks, the next
  start runs with tweaks off and Settings → Patches shows a "Turn patches back on" button.

## Tweaks — macOS list vs Windows

Verified in Dia on this machine with screenshots (on, then off again, unless noted).

| macOS tweak | Windows key | Status |
|---|---|---|
| Edge-to-edge web content | `edgeToEdge` | Done. Dia only adds the gap in some window states; maximized has none. |
| Square corners by the sidebar | `squareCorners` | Done, verified (normal and maximized window). Squares Dia's rounded UI corners around the page. The web page's own bottom corners are rounded inside Dia's engine (`roundsContentCorners` in Dia's Swift code), so the page is stretched ~9 px past the bottom of the window and Windows clips the rounded corners off; the last ~9 px of the page sit below the window edge. |
| Hide toolbar | `hideToolbar` | Done, verified. The window buttons slide and fade out with the toolbar and back in when it's revealed (130 ms down, 180 ms up, in the toolbar's easing); while hidden they can't be clicked by accident. The reveal band is 6 px maximized or full screen, 12 px in a window (reaching below the resize border); once out, the toolbar stays while the pointer is within 52 px, or up to 24 px past the window's top, and leaving takes 200 ms of the pointer staying away. A hover change re-evaluates only the rules that follow the pointer (state hover, showToolbar; ~2 ms). In a window, the top resize border (InputNonClientPointerSource, HTTOP) is subclassed to let the pointer through until it has stayed at the top for 1.5 s (corners always resize; verified with WM_NCHITTEST). With Toolbar in the title bar (and no Edge-to-edge) the slide ends 4 px higher than Dia's layout (Dia's gap is 4 px on top, right and bottom, none by the sidebar), so the strip that comes down is level with the window buttons, as when the toolbar moves into the title bar; collapsed, the page keeps Dia's 4 px gap. The sidebar's top row (logo, back, forward) sits at the strip's middle all the time, so it doesn't jump on reveal. While the toolbar is out on hover only, the page keeps its size (it moves, and its bottom goes under the window's edge), so pages aren't laid out again on every reveal; held out (typing in it, Show toolbar with the sidebar or on new tab pages), it fits the window. Dia's `BottomSeparator` under the toolbar, which Dia shows from code on scrolled chat pages, is hidden with Opacity as well as Visibility. The Chat button's box (`BackgroundBorder`) is hidden until the pointer is on it, in that title bar and with the Arc URL bar. |
| Reveal toolbar on hover | *(merged into `hideToolbar`)* | Done, verified. Hide toolbar now always slides the toolbar back in while the pointer is at the top edge. |
| Show toolbar with the sidebar | `showToolbarWithSidebar` | Done, verified (sidebar open → shown, closed → hidden). |
| Show toolbar on new tab pages | `showToolbarOnNewTab` | Done, verified (Dia's start page). |
| *(new)* Toolbar in the title bar | `titleBar` | Done, not yet re-verified on this branch. Replaces Toolbar color (`toolbarColor`): updates carry Sidebar over as on, Adaptive as off (`MergeConf` in src/diatweaks.cpp). The shown tab's real toolbar (`NavigationBarHost`) moves out of its pane (`NavigationChromeHost`, its row capped at 0) into a row of ours (`diatweaksTitleBar`) at the top of the window grid, pulled up level with the window buttons, and the sidebar's top row centered on it (measured: Dia's own row sits 2 px higher than the toolbar's buttons), its space-name hover box 32 px like theirs (MaxHeight 40 on SpaceSwitcherButton, which Dia pads 4 px); tab switches move the new tab's toolbar up before its first frame. Split view keeps Dia's layout (a toolbar in each pane, in the sidebar's colors). The address box opens centered under the field, Dia's drag button over the field goes and a click on the field opens the box. With top tabs, the native SpacePagingScrollViewer moves below the toolbar into a second Auto row, whose minimum height preserves its measured native height; its parent, index and Grid.Row are restored on exit. The toolbar reserves room for the window buttons. The move stands aside with the Arc URL bar in the sidebar, Hide toolbar or split view (Centered's strip goes up with the toolbar; state `titleBarUp` while it's up). Colors as Toolbar color = Sidebar was: Dia keeps each toolbar's site palette in the strip's own resources (`NavigationBarBackgroundBrush`, `ButtonBackgroundPointerOver`, `SubtleFillColorSecondaryBrush`, `ControlFillColorDefaultBrush` for Chat, ...); with Sidebar those brushes are recolored to the app's values for the same keys (the strip itself: see-through), so hover and pressed states look like the sidebar's. The address field's hover fill is its own brush and is swapped for the app's subtle fill; icons and window buttons take the sidebar's text color. Dia's colors come back with the patch off; popovers take the sidebar's theme too, and the window buttons' glyphs are re-centered. `probe` now also lists an element's own brush resources. |
| *(new)* Preset | `preset` | Done, verified. Drop-down at the top of Settings > Patches: None (all patches off, the default), Arc (edge-to-edge, square corners, hide toolbar with reveal on hover, address bar = Sidebar, floating new tab bar, hide tab search button, hide Chat button, no tab hover cards, Peek, window material = Acrylic; everything else off), Arc (Windows) (the same, but address bar = Centered and the toolbar kept visible, as in Arc for Windows), Custom (your own mix). Picking None, Arc or Arc (Windows) sets every patch in one write; changing any patch by hand switches the preset to Custom. "Turn all off" = None. |
| *(new)* Address bar: Sidebar | `arcUrlBar` = `sidebar` | Done, verified. Drop-down (Address bar): Toolbar (Dia) = off, Sidebar (Arc for Mac), Centered (Arc for Windows). Header row: Dia logo, sidebar toggle, back/forward/reload (Dia's ⌄ menu kept at the end); all icons in one row at Dia's own button height, same color (the logo keeps Dia's own look), the toggle right next to the logo, and the nav buttons against ⌄ (or the edge, at the logo's margin, when ⌄ is hidden); the address pill directly below; top toolbar hidden. With Hide toolbar the space name moves up into the toolbar that comes down as a title bar, and back to the header without it. That toolbar is thin, as tall as the centered bar's: 36 px (its NavigationChromeHost row, set in code and watched, as Dia sets it again on tab switches), 2 px padding, the Chat button 28 px. The window buttons follow any toolbar strip: as tall as the strip plus the gap above it (they still take clicks up to the window's edge), their hover box inset to start level with the strip, and their glyph centered in it. The header's buttons are added to the title bar's passthrough areas, so their top halves (in the window's top 32 px) take clicks instead of dragging the window. The address field's visibility, which Dia sets from code (hidden on its start page), is followed while the field is borrowed, so it goes home with Dia's latest value: a tab opened from New Tab no longer lost its address when the mode was switched off. Dia's real elements are moved (single-toolbar style). Dia keeps a toolbar per tab; the bar takes over the new tab's controls the moment Dia inserts them, on the next frame (~16-23 ms), restyled in the same pass. The pill shows only the site (search pages: Dia's "Google /" chip + query); the page's action buttons (☆ / ⋯ or link / site settings) fade in at the far right only while the pointer is over the pill. Icons use the sidebar color, not Dia's per-site tint. In split view it follows the selected pane (the one with keyboard focus) and stays put while focus is in the sidebar, so it no longer flips between panes. Each pane's own toolbar keeps only its close (X) button; the selected pane's controls live only in the sidebar. The sidebar toggle stays visible whichever pane is selected (Dia normally shows it only in the leftmost pane). Clicking the pill or Ctrl+L opens Dia's command bar: anchored over the pill at Arc's ~600 px width while the sidebar is docked; centered over the page (760 px wide, 20% from the top, above the floating sidebar) while the sidebar is closed. Enter/Go navigates. Ctrl+T is Dia's own New Tab unless the Floating new tab bar patch is on. |
| *(new)* Address bar: Centered | `arcUrlBar` = `windows` | Done, verified (layout, sidebar toggle, mode switching, off restores). Measured from Arc for Windows' own UI tree (Arc 1.107): Arc's centered group is [link] [site, 12 px, padding 8] [site controls] with 2 px spacing, in the icon color. Arc has one title bar per window that never changes with tabs; Dia has a toolbar per tab. So Dia's real controls (sidebar toggle, back/forward/reload, the address field, the page buttons on the right) are moved into one strip of ours in the window's ContentGroup, laid over the selected tab's (emptied) toolbar, and handed over to a new tab before its first frame, as the Arc (sidebar) mode does. Dia's address text is hidden (still there and clickable) and the group [☆] [site] [⋯] is centered in the toolbar (in the field when the toolbar is too narrow; the field is clipped to the group, so only the group takes hover and clicks; on hover Dia's full address is reduced to the site too; an empty tab shows a "Search or Enter URL…" pill), the site 12 px in the icon color with a 28 px, radius-6 hover pill. The toolbar keeps Dia's colors, following the site (the strip lives in the shown tab's own toolbar, so the controls keep its palette and theme), and takes the sidebar's with Toolbar in the title bar, which also moves it up, Dia's loading bar is hidden, and while the sidebar is docked Dia's own toggle is moved into the sidebar header right after Dia's logo, and back to the strip's left when the sidebar is closed (a copy wouldn't work: inside Dia's window-drag areas only Dia's own buttons take clicks). While the Floating new tab bar opens a URL in a new tab, the strip shows the new address at once and keeps the other controls in place until the new tab has its site (the new tab's own toolbar stays invisible), so only the address changes. As in Arc's title bar: the site is its own button (a click opens the command bar), ☆ / ⋯ always take clicks, and the strip's empty space is registered as a standard WinUI caption (window-drag) area, so dragging there is the system's own window drag (with snapping); verified with WM_NCHITTEST: gaps = caption, group and buttons = client. A click on the site opens Dia's command bar, centered over the window like the Ctrl+T bar. Not changed: Dia's 40 px toolbar and 32 px buttons (Arc: 32 px strip, 28 px buttons). |
| *(new)* Hide tab search button | `hideTabSearchButton` | Done, verified. Hides the ⌄ (search tabs) button at the top of the sidebar; with the Arc URL bar, back/forward/reload move into its place. |
| *(new)* Floating new tab bar | `floatingNewTab` | Done, verified. Ctrl+T shows Dia's command bar floating over the current page, empty and centered (the same bar as Ctrl+L), instead of a New Tab page. Enter opens a new, selected tab with the URL (~0.3 s): Dia has no 'open in new tab' key (Enter = this tab, Alt+Enter = split view, Shift+Enter = new line, Ctrl+Shift+Enter = search here), so the patch opens Dia's own new tab, keeps its New Tab artwork hidden, and submits the text in that tab's box once Dia has read it. Esc creates nothing. Dia's New Tab page itself is unchanged. |
| Always show address field | `alwaysShowAddressField` | Done, verified. Field background + bookmark/site buttons; Dia's hover binding is restored when off. |
| Hide sidebar button | `hideSidebarButton` | Done, verified. |
| Hide Chat button | `hideChatButton` | Done, verified (also from the Settings switch). |
| Hide Chats button / Hide recent tabs button | — | Dia for Windows has no such buttons. |
| Clean search box | `cleanSearchBox` | Done, verified (empty, typing with suggestions, long text, off again). The box on Dia's New Tab page (under `VariantHost`) and the Ctrl+L / Ctrl+T command bar (Dia's popover, under `DynamicPopoverRoot`), all the same control; never chat (`AssistantPanelHost`). The box's bottom row (`ToolbarArea`: "Add tabs or files" + send) is moved into the text's row (`InputContent`, row 1 of `CommandBarContentGrid`), right-aligned and level with the text; everything in it but `SubmitButton` is collapsed, its 48 px row is set to 0 and the grid's `MinHeight` (96) to 0. New Tab's text row is 48 px with the text in its top 32, so that spare 16 px replaces the card's bottom padding (one 60 px line); Ctrl+L's row is 32 px and keeps its padding (56 px). The button widens while typing ("Go ↵", "Google ↵", "Chat"), so the text row gets a right margin of the button's width minus the room Dia already leaves there (32 px on New Tab; none on Ctrl+L while its × shows): text and × stay left of the button. Off puts every value back. |
| Profile emoji | `profileEmoji` | Done, verified. One emoji (set in Settings → Patches) at the top of the sidebar; not per-profile. |
| Hide pinned tab border | `hidePinnedTabBorder` | Done (applied to all pinned tabs; not seen with a pinned tab selected). |
| Translucent sidebar | `translucentSidebar` | Done, verified. |
| Tidy Tabs | `tidyTabs` (+ `tidyTabsShow`, `tidyTabsStyle`, `tidyTabsButtons`) | Done. Its own Settings section: show always or on hover, icons only or with text, Tidy and/or Clear. The UI follows macOS: a row (line + Jace's tilted broom) eases open over 0.2 s when the pointer comes near the top of the tab list (6+ rows), shuts 150 ms after it leaves, and the line rolls as a sine wave while busy. macOS groups with Apple Intelligence; Windows has none, so the broom asks Dia's own chat, whose built-in GroupTabs tool makes, names and colors the groups. Nothing shows: Dia's chat panel (`AssistantPanelHost`) is parked past the window's right edge (a negative right margin as wide as it is, so it keeps its size but takes no room and is clipped away; Dia's Chromium chat window follows it off-screen), opened with its Chat button (pressed even while Hide Chat button keeps it collapsed), and the prompt is put into the panel's own XAML prompt box (`QueryTextBox`) and sent with its `SubmitButton`: no keyboard, focus, tab or command bar. Busy ends when Dia's chat is done (the panel's send button shows Stop, U+EE95, while it works), i.e. once every group is made and named; Dia's end-of-reply chime is muted for the run (this process's own audio sessions, so only Dia's interface sounds: web pages play from Chromium's separate audio process). Then the panel's `CloseButton` shuts it and it's put back once closed. Dia's Chat button is off on the New Tab page, so the broom does nothing there. Dia's divider with "↓ Clear" (`TabSeparatorRow`) only exists while Close All Tabs asks to confirm, so the row is ours, over Dia's `TabListScrollViewer`. While the hidden chat opens and closes, the page is held still: a click-through copy of it is laid over it (Chromium redraws the page then, and on the first tidy after Dia starts resizes its GPU surface, which flashed a black band). The wave and pulse run on the compositor. Each tidy adds a chat to Dia's history. Clear is Dia's own sidebar right-click > Close All Tabs, confirmed: Dia builds that menu only on a right-click, so one is sent to an empty spot of the sidebar (the pointer goes straight back), the menu is hidden as it opens and its Close All Tabs run, then Dia's "↓ Clear" line (`CloseAllButton`) is pressed. With tabs on top there is no line: the buttons are icons at the right end of the tab bar, in a spot reserved by narrowing Dia's tab strip (`TabStripContentHost`) so they never move, tinted like Dia's own +, shown on hover over the bar (or always), the broom pulsing while Tidy works; Dia has no Close All Tabs there, so Clear closes each loose tab (outside every group pill) with its own close button. |
| Don't pop up downloads | `dontPopUpDownloads` | Implemented, **not verified**: needs a real download to test. Closes a downloads pop-up that opens with no click or key press just before it. |
| Close tabs from the tab switcher | — | Dia for Windows has no tab-switcher UI (Ctrl+Tab switches directly). |
| Keep windows active | — | Dia for Windows looks identical active and inactive (checked pixel by pixel), so there is nothing to keep. |
| macOS 26 window corners | — | macOS-only (Windows 11 rounds windows itself). |
| Hide traffic lights | `hideWindowButtons` | Done, verified. Hides minimize/maximize/close *and* removes their click areas (Dia registers them with Windows, so a hidden button could otherwise still be clicked); Alt+F4 still closes. |
| Window material | `windowMaterial` | Done, verified. Mica, Mica Alt or Acrylic behind the tabs and sidebar. |
| Allow iCloud passkeys / iCloud Passwords / 1Password | — | macOS-only. |
| Group Tabs by Domain (command) | — | Not done: Dia exposes no way to reorder tabs other than dragging them. |

The macOS Settings → Patches page is ported (switches, options, turn all off, safe mode). Not ported:
in-app Software Update / auto-update and Restore (DiaTweaks for Windows never patches Dia, so there is
nothing to restore — turn all off or delete the folder).

## Files

- `src/tap.cpp` — tree tracking, rule engine, config, safe mode. `src/engine.h` — API shared with ui.cpp.
- `src/ui.cpp` — Settings → Patches page, window states, code-driven tweaks, `probe`.
- `src/diatweaks.cpp` — the command.
- `src/dia-load.cpp`, `src/test_attach.cpp`, `src/launch_direct.cpp` — earlier experiments, not needed.
- `build.cmd`, `gen-winrt.cmd`, `reload.ps1` (close Dia, rebuild, restart with tweaks).
- `tweaks.conf` — tweak definitions (copied to `bin\` on first build; `bin\tweaks.conf` is the live one).
