# Toolbar refinement handoff

Checkpoint: 2026-11-07. The build succeeds and Dia is open with the integration DLL. The toolbar UI is unfinished. Physical menu clicks, hover appearance and dragging still need verification.

## Checkout and GitHub destination

Work in `C:\Users\PC\Documents\vivaldi-zen\WinDiaTweaks\integration-v1.2`, branch `integration/toolbar-v1.2`. This checkout has a monolithic `src/ui.cpp`.

Upstream release `v1.2.1` was merged in commit `d06d164`. The checkpoint containing this handoff preserves the subsequent toolbar work. Its destination is remote `fork`, `https://github.com/moondling/WinDiaTweaks.git`, on the integration branch. No upstream PR or release publication is part of this checkpoint.

The parent checkout is a separate branch, `custom-tweaks`, with unrelated user changes. Leave those changes alone. `WinDiaTweaks-upstream` and `WinDiaTweaks-pr` are separate worktrees too.

An intermediate backup remains in stash `3117a3b984101d68017e25370e939c418d99bb18`, named `toolbar bug fixes before v1.2.1 merge`. It was applied after the merge. It contains an earlier patch, so applying it over this checkpoint would duplicate or conflict with later work.

## Launch-helper conflict: check this before debugging

The user reported that reopening Dia removed the changes. Inspection found the running DLL was actually:

```text
C:\Users\PC\Documents\vivaldi-zen\WinDiaTweaks-upstream\bin\DiaTweaksTap.dll
```

That checkout's `DiaLauncher.exe` helper was running. WinDiaTweaks uses shared helper/event names, and `diatweaks status` reports tweaks loaded without distinguishing which checkout supplied the DLL. Several apparent build reversions were therefore observations of a different build.

The helper was stopped through `stop-helper`, then `sync-helper` and `restart` were run from this integration checkout. Both the loaded DLL and helper paths were verified under `integration-v1.2\bin` afterward. The normal-launch setting remains on. Address bar is set to regular Dia (`arcUrlBar` off); `titleBar` is on.

Verify the actual path whenever restarting or testing alongside another worktree:

```powershell
Get-Process Dia | Where-Object MainWindowHandle -ne 0 | ForEach-Object {
    $_.Modules | Where-Object FileName -match 'DiaTweaks' |
        ForEach-Object FileName
}
Get-Process DiaLauncher -ErrorAction SilentlyContinue | ForEach-Object Path
```

If the helper points elsewhere, use the integration tools:

```powershell
.\bin\diatweaks.exe stop-helper
.\bin\diatweaks.exe sync-helper
.\bin\diatweaks.exe restart
```

These commands update the existing normal-launch helper's destination. Avoid competing helpers from different worktrees. Live process inspection and launch commands required sandbox escalation in this session; the ordinary sandbox sometimes reported Dia absent.

## What changed

* Address modes: scoped `HiddenAddress` state retains the native editor until its opacity, clip and focus properties are restored. Callback tokens are revoked before restoration. Mode cleanup precedes borrowing controls for another mode. The code no longer forces the native address host visible, which masked Dia's visibility changes.
* Centered: the native `AssistantBarHost` stays in its original navigation tree. Its native trailing stack moves as a unit, with a temporary spacer for the site label. Individual button translations were abandoned. Spacing, transforms and pointer-state bindings are restored on exit. Borrowed controls preserve their effective DataContext.
* Horizontal tabs: the title-bar row is 36 px, with 32 px controls. The tab inset moves every native canvas layer together, including pinned tabs and the picker. Margin callbacks counter native resets; cleanup restores the saved properties.
* Hover styling: toolbar controls use neutral app brushes, with guards on backgrounds and palette changes. Root grids and hover borders receive radius 6. OS window-button hover backgrounds are excluded from the toolbar-specific override. `FixedAddressFill()` gives the address-background callbacks one shared target, avoiding recursive disagreement with Always show address field.
* Dragging: actual regular-toolbar controls and measured address-text bounds get passthrough regions; empty areas get caption regions. Stale native passthrough is cut out of those drag gaps. Saved caption, passthrough and HWND regions are restored only while the active geometry still matches ours.
* Arc sidebar: the moved space/profile button is capped at 32 px, including its own minimum/maximum constraints.
* URL display: `src/address_text.h` shares site extraction between address modes, including schemeless hover URLs. `tests/address-text.cpp` and `scripts/Test-AddressText.ps1` cover 16 cases.
* Build: `build.cmd` can move loaded binaries to `.old` and rebuild. README and porting notes record the 36 px horizontal title bar.

Most implementation work is in `src/ui.cpp`. Search for `HiddenAddress`, `AdoptActiveTab`, `LayoutArcWin`, `LookWant`, `FixedAddressFill`, `RemoveTopTabInset`, `CompactTitleBar`, `RegularToolbarClickRects`, `ToolbarDragRects`, and `UpdateArcHeaderPassthrough`.

## Verification and known remaining issues

MSVC compilation and linking passed. `git diff --check` passed. The URL parser's 16 tests passed; they do not test WinUI interaction. The locally built executable reports `v1.2.1-15-gd06d164-dirty`, because it was built before the checkpoint commit. Dia version is `1.6.0.525`.

A fresh probe after verifying the DLL path confirmed a 36 px row, 32 px Back/Reload buttons at y=3, radius 6, and caption gaps alongside clickable controls. A captured tab-layout probe put pinned tabs at x=8 instead of x=83, with regular content at x=146 and pinned right edge x=143. Recheck this after resizing, pin changes and space switching. Diagnostic files live in ignored `bin/`, including `final-toolbar-geometry.txt`; some earlier snapshots may precede the helper correction.

The remaining work is concrete:

1. Header alignment is still uneven. Fresh passthrough bounds put the lifted logo at `(4,5)` and chevron at `(48,7)`, while navigation buttons start at y=3. Their layout sizes are 32 px, but rendered offsets persist. A detailed probe also showed the logo's template grid at 24x28. Inspect native RenderTransform/Translation and content insets before changing more margins. The chevron can extend into the navigation allocation. Do not call alignment finished.
2. The user's last explicit Centered-menu test failed. Changes that retain the native address owner came afterward, but physical success is unconfirmed. Earlier XAML hit probes found both buttons, and `WindowFromPoint` reached the desktop content bridge. Those observations do not prove Click handlers execute.
3. Rounded, even hover boxes and absence of site-color flicker need visual testing in both address modes. Test theme changes and tab switches too.
4. Regular-toolbar window dragging needs a physical test, especially after switching address modes. The registered geometry was checked; actual dragging was not.
5. Check off/on restoration, empty tabs, all-pinned tabs, pin changes, split view and sidebar layout. Cached inset measurement and native template replacement deserve attention.

## Build and probe commands

```powershell
Set-Location C:\Users\PC\Documents\vivaldi-zen\WinDiaTweaks\integration-v1.2
cmd /d /c build.cmd
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-AddressText.ps1
.\bin\diatweaks.exe restart
.\bin\diatweaks.exe probe diatweaksTitleBar NavigationBarHost BackButton ReloadButton REGIONS
```

Append `*` to a probe name for its descendants. `HIT:x,y` performs a XAML hit test in window DIPs. Region enum labels were corrected in `ProbeRegions`; older output mislabeled Caption as Icon.

Check that `bin/probe.out` gets a fresh timestamp and `bin/probe.txt` is consumed. A helper from another checkout can leave this checkout's probe output stale. Preserve the user's selected address mode when doing temporary tests.

Computer-use tools could not connect to their native pipe, even after retries and reset. CLI probes worked, but there was no reliable desktop input/screenshot automation. User screenshots and physical tests remain necessary.
