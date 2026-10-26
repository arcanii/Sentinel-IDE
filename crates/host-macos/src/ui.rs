// SPDX-License-Identifier: GPL-3.0-or-later
//! Building the window, its views and the menu bar. Nothing here holds state or reacts to
//! anything: it makes AppKit objects, wires their targets and delegates to the controller,
//! and hands them back. The controller owns them from then on.

use objc2::rc::Retained;
use objc2::runtime::{AnyObject, ProtocolObject, Sel};
use objc2::{sel, MainThreadOnly};
use objc2_app_kit::{
    NSApplication, NSAutoresizingMaskOptions, NSBackingStoreType, NSEventModifierFlags, NSFont, NSMenu,
    NSMenuItem, NSOutlineView, NSScrollView, NSSplitView, NSSplitViewDividerStyle, NSTabView, NSTabViewItem,
    NSTableColumn, NSTableView, NSTextField, NSTextView, NSToolbar, NSToolbarDisplayMode, NSWindow,
    NSWindowStyleMask, NSWindowToolbarStyle,
};
use objc2_foundation::{ns_string, MainThreadMarker, NSPoint, NSRect, NSSize, NSString};

use crate::controller::Controller;
use crate::palette::{color, Role};

pub struct Views {
    pub window: Retained<NSWindow>,
    pub outline: Retained<NSOutlineView>,
    pub viewer: Retained<NSTextView>,
    pub output: Retained<NSTextView>,
    pub problems: Retained<NSTableView>,
    pub dock: Retained<NSTabView>,
    pub status: Retained<NSTextField>,
    pub recents: Retained<NSMenu>,
    pub mono: Retained<NSFont>,
}

pub const DOCK_PROBLEMS: isize = 0;
pub const DOCK_OUTPUT: isize = 1;

const STATUS_H: f64 = 24.0;

fn rect(x: f64, y: f64, w: f64, h: f64) -> NSRect {
    NSRect::new(NSPoint::new(x, y), NSSize::new(w, h))
}

fn fill() -> NSAutoresizingMaskOptions {
    NSAutoresizingMaskOptions::ViewWidthSizable | NSAutoresizingMaskOptions::ViewHeightSizable
}

/// The editor font from Settings, falling back to the system monospaced font when the
/// named one is not installed (Cascadia Code, say, in a settings.ini copied from Windows).
pub fn mono_font(name: &str, size: f64) -> Retained<NSFont> {
    NSFont::fontWithName_size(&NSString::from_str(name), size)
        .unwrap_or_else(|| NSFont::monospacedSystemFontOfSize_weight(size, 0.0))
}

/// A read-only text view in a scroll view: the file viewer and the Output pane.
fn text_pane(
    mtm: MainThreadMarker,
    delegate: &Controller,
    font: &NSFont,
) -> (Retained<NSScrollView>, Retained<NSTextView>) {
    let scroll = NSTextView::scrollableTextView(mtm);
    let view = scroll.documentView().and_then(|v| v.downcast::<NSTextView>().ok()).expect("a text view");
    view.setEditable(false);
    view.setSelectable(true);
    view.setRichText(false);
    view.setUsesFindBar(true);
    view.setFont(Some(font));
    view.setBackgroundColor(&color(Role::WindowBg));
    view.setTextColor(Some(&color(Role::TextPrimary)));
    // No wrapping: build output and source both read better with a horizontal scroller.
    view.setHorizontallyResizable(true);
    if let Some(c) = unsafe { view.textContainer() } {
        c.setWidthTracksTextView(false);
        c.setContainerSize(NSSize::new(f64::MAX, f64::MAX));
    }
    scroll.setHasHorizontalScroller(true);
    // The controller outlives every view it is the delegate of (it lives for the process).
    view.setDelegate(Some(ProtocolObject::from_ref(delegate)));
    (scroll, view)
}

fn column(mtm: MainThreadMarker, id: &NSString, title: &str, width: f64) -> Retained<NSTableColumn> {
    let c = NSTableColumn::initWithIdentifier(NSTableColumn::alloc(mtm), id);
    c.setTitle(&NSString::from_str(title));
    c.setWidth(width);
    // Cell-based columns are editable by default; a double-click would start editing a
    // file name or a problem's text.
    c.setEditable(false);
    c
}

fn scrolled(mtm: MainThreadMarker, doc: &objc2_app_kit::NSView) -> Retained<NSScrollView> {
    let s = NSScrollView::initWithFrame(NSScrollView::alloc(mtm), rect(0.0, 0.0, 100.0, 100.0));
    s.setDocumentView(Some(doc));
    s.setHasVerticalScroller(true);
    s.setHasHorizontalScroller(true);
    s.setAutoresizingMask(fill());
    s
}

pub fn build_window(mtm: MainThreadMarker, c: &Controller, font_name: &str) -> Views {
    let target: &AnyObject = c.as_ref();
    let mono = mono_font(font_name, 12.0);

    // SAFETY: released-when-closed is turned off below, as AppKit requires for a window
    // not owned by a window controller.
    let window = unsafe {
        NSWindow::initWithContentRect_styleMask_backing_defer(
            NSWindow::alloc(mtm),
            rect(0.0, 0.0, 1200.0, 780.0),
            NSWindowStyleMask::Titled
                | NSWindowStyleMask::Closable
                | NSWindowStyleMask::Miniaturizable
                | NSWindowStyleMask::Resizable
                | NSWindowStyleMask::UnifiedTitleAndToolbar,
            NSBackingStoreType::Buffered,
            false,
        )
    };
    unsafe { window.setReleasedWhenClosed(false) };
    window.setTitle(ns_string!("Sentinel-IDE"));
    window.setContentMinSize(NSSize::new(700.0, 450.0));
    window.center();
    window.setFrameAutosaveName(ns_string!("SentinelIDEMainWindow"));
    window.setDelegate(Some(ProtocolObject::from_ref(c)));

    let toolbar = NSToolbar::initWithIdentifier(NSToolbar::alloc(mtm), ns_string!("SentinelIDEToolbar"));
    toolbar.setDelegate(Some(ProtocolObject::from_ref(c)));
    toolbar.setDisplayMode(NSToolbarDisplayMode::IconOnly);
    window.setToolbar(Some(&toolbar));
    window.setToolbarStyle(NSWindowToolbarStyle::Unified);

    let content = window.contentView().expect("content view");
    let bounds = content.bounds();
    let (w, h) = (bounds.size.width, bounds.size.height);

    // ---- project tree ----
    let outline = NSOutlineView::initWithFrame(NSOutlineView::alloc(mtm), rect(0.0, 0.0, 240.0, 100.0));
    let col = column(mtm, ns_string!("name"), "Project", 220.0);
    outline.addTableColumn(&col);
    unsafe { outline.setOutlineTableColumn(Some(&col)) };
    outline.setHeaderView(None);
    unsafe {
        outline.setDataSource(Some(ProtocolObject::from_ref(c)));
        outline.setDelegate(Some(ProtocolObject::from_ref(c)));
    }
    let tree_scroll = scrolled(mtm, &outline);
    tree_scroll.setFrame(rect(0.0, 0.0, 240.0, h - STATUS_H));

    // ---- file viewer (the editor arrives in M2) ----
    let (viewer_scroll, viewer) = text_pane(mtm, c, &mono);
    viewer_scroll.setFrame(rect(0.0, 0.0, w - 241.0, h - STATUS_H - 230.0));

    // ---- dock: Problems | Output ----
    let problems = NSTableView::initWithFrame(NSTableView::alloc(mtm), rect(0.0, 0.0, 400.0, 100.0));
    for (id, title, width) in [
        (ns_string!("message"), "Problem", 620.0),
        (ns_string!("file"), "File", 200.0),
        (ns_string!("line"), "Line", 60.0),
    ] {
        problems.addTableColumn(&column(mtm, id, title, width));
    }
    problems.setUsesAlternatingRowBackgroundColors(true);
    unsafe {
        problems.setDataSource(Some(ProtocolObject::from_ref(c)));
        problems.setDelegate(Some(ProtocolObject::from_ref(c)));
        problems.setTarget(Some(target));
        problems.setAction(Some(sel!(problemClicked:)));
    }
    let problems_scroll = scrolled(mtm, &problems);
    let (output_scroll, output) = text_pane(mtm, c, &mono);

    let dock = NSTabView::initWithFrame(NSTabView::alloc(mtm), rect(0.0, 0.0, w - 241.0, 230.0));
    for (label, view) in [("Problems", &*problems_scroll), ("Output", &*output_scroll)] {
        let item = NSTabViewItem::new();
        item.setLabel(&NSString::from_str(label));
        item.setView(Some(view));
        dock.addTabViewItem(&item);
    }

    let right = NSSplitView::initWithFrame(NSSplitView::alloc(mtm), rect(0.0, 0.0, w - 241.0, h - STATUS_H));
    right.setVertical(false);
    right.setDividerStyle(NSSplitViewDividerStyle::Thin);
    right.addSubview(&viewer_scroll);
    right.addSubview(&dock);
    right.setAutosaveName(Some(ns_string!("SentinelIDEEditorDock")));

    let split = NSSplitView::initWithFrame(NSSplitView::alloc(mtm), rect(0.0, STATUS_H, w, h - STATUS_H));
    split.setVertical(true);
    split.setDividerStyle(NSSplitViewDividerStyle::Thin);
    split.addSubview(&tree_scroll);
    split.addSubview(&right);
    split.setAutosaveName(Some(ns_string!("SentinelIDESidebar")));
    split.setAutoresizingMask(fill());
    content.addSubview(&split);
    split.adjustSubviews();
    right.adjustSubviews();

    // ---- status line ----
    let status =
        NSTextField::labelWithString(ns_string!("Open a project to start  (File ▸ Open Project…, ⌘O)"), mtm);
    status.setFrame(rect(10.0, 4.0, w - 20.0, 16.0));
    status.setAutoresizingMask(NSAutoresizingMaskOptions::ViewWidthSizable);
    status.setTextColor(Some(&color(Role::TextSecondary)));
    status.setFont(Some(&NSFont::systemFontOfSize(11.0)));
    content.addSubview(&status);

    let recents = NSMenu::initWithTitle(NSMenu::alloc(mtm), ns_string!("Open Recent"));
    Views { window, outline, viewer, output, problems, dock, status, recents, mono }
}

// ---- the menu bar ------------------------------------------------------------------

fn item(mtm: MainThreadMarker, title: &str, action: Option<Sel>, key: &str) -> Retained<NSMenuItem> {
    // SAFETY: every selector passed here is one AppKit or the controller implements.
    unsafe {
        NSMenuItem::initWithTitle_action_keyEquivalent(
            NSMenuItem::alloc(mtm),
            &NSString::from_str(title),
            action,
            &NSString::from_str(key),
        )
    }
}

fn targeted(
    mtm: MainThreadMarker,
    title: &str,
    action: Sel,
    key: &str,
    target: &AnyObject,
) -> Retained<NSMenuItem> {
    let i = item(mtm, title, Some(action), key);
    unsafe { i.setTarget(Some(target)) };
    i
}

fn menu(
    mtm: MainThreadMarker,
    title: &str,
    items: Vec<Retained<NSMenuItem>>,
) -> (Retained<NSMenuItem>, Retained<NSMenu>) {
    let m = NSMenu::initWithTitle(NSMenu::alloc(mtm), &NSString::from_str(title));
    for i in &items {
        m.addItem(i);
    }
    let top = item(mtm, title, None, "");
    top.setSubmenu(Some(&m));
    (top, m)
}

fn sep(mtm: MainThreadMarker) -> Retained<NSMenuItem> {
    NSMenuItem::separatorItem(mtm)
}

pub fn build_menus(mtm: MainThreadMarker, c: &Controller, recents: &NSMenu) {
    let t: &AnyObject = c.as_ref();
    let app = NSApplication::sharedApplication(mtm);
    let bar = NSMenu::initWithTitle(NSMenu::alloc(mtm), ns_string!(""));

    let hide_others = item(mtm, "Hide Others", Some(sel!(hideOtherApplications:)), "h");
    hide_others.setKeyEquivalentModifierMask(NSEventModifierFlags::Command | NSEventModifierFlags::Option);
    let (app_menu, _) = menu(
        mtm,
        "Sentinel-IDE",
        vec![
            item(mtm, "About Sentinel-IDE", Some(sel!(orderFrontStandardAboutPanel:)), ""),
            sep(mtm),
            item(mtm, "Hide Sentinel-IDE", Some(sel!(hide:)), "h"),
            hide_others,
            item(mtm, "Show All", Some(sel!(unhideAllApplications:)), ""),
            sep(mtm),
            item(mtm, "Quit Sentinel-IDE", Some(sel!(terminate:)), "q"),
        ],
    );

    let open_recent = item(mtm, "Open Recent", None, "");
    open_recent.setSubmenu(Some(recents));
    let close_project = targeted(mtm, "Close Project", sel!(closeProject:), "w", t);
    close_project.setKeyEquivalentModifierMask(NSEventModifierFlags::Command | NSEventModifierFlags::Shift);
    let (file_menu, _) = menu(
        mtm,
        "File",
        vec![
            targeted(mtm, "Open Project…", sel!(openProject:), "o", t),
            open_recent,
            sep(mtm),
            close_project,
            item(mtm, "Close Window", Some(sel!(performClose:)), "w"),
        ],
    );

    let find = |title: &str, tag: isize, key: &str| {
        let i = item(mtm, title, Some(sel!(performTextFinderAction:)), key);
        i.setTag(tag); // NSTextFinderAction: 1 show, 2 next, 3 previous
        i
    };
    let find_prev = find("Find Previous", 3, "g");
    find_prev.setKeyEquivalentModifierMask(NSEventModifierFlags::Command | NSEventModifierFlags::Shift);
    let (edit_menu, _) = menu(
        mtm,
        "Edit",
        vec![
            item(mtm, "Copy", Some(sel!(copy:)), "c"),
            item(mtm, "Select All", Some(sel!(selectAll:)), "a"),
            sep(mtm),
            find("Find…", 1, "f"),
            find("Find Next", 2, "g"),
            find_prev,
        ],
    );

    let (product_menu, _) = menu(
        mtm,
        "Product",
        vec![targeted(mtm, "Build", sel!(build:), "b", t), targeted(mtm, "Run", sel!(run:), "r", t)],
    );

    let (window_item, window_menu) = menu(
        mtm,
        "Window",
        vec![
            item(mtm, "Minimize", Some(sel!(performMiniaturize:)), "m"),
            item(mtm, "Zoom", Some(sel!(performZoom:)), ""),
        ],
    );

    for top in [app_menu, file_menu, edit_menu, product_menu, window_item] {
        bar.addItem(&top);
    }
    app.setMainMenu(Some(&bar));
    app.setWindowsMenu(Some(&window_menu));
}
