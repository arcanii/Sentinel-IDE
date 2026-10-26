// SPDX-License-Identifier: GPL-3.0-or-later
//! The one Objective-C class this host defines. It is the application delegate, the data
//! source and delegate of the project tree and of the Problems table, the Output pane's
//! link handler, the toolbar's delegate and the target of every menu command.
//!
//! One rule keeps it sound: **state is never borrowed across a call into AppKit that can
//! call back.** `reloadData`, selecting a row, expanding an item and appending to a text
//! view all re-enter the data-source and delegate methods below, which borrow the state
//! themselves. So every method mutates in a scope that ends, and only then touches views.
//!
//! Work off the main thread (a build, a run) reports back through [`post`], which queues a
//! closure on the main dispatch queue; lines arrive in order and "done" after the last line.

use std::cell::{OnceCell, Ref, RefCell, RefMut};
use std::ffi::OsString;
use std::fs;
use std::os::unix::process::ExitStatusExt;
use std::path::{Path, PathBuf};
use std::process::ExitStatus;

use dispatch2::DispatchQueue;
use objc2::rc::Retained;
use objc2::runtime::{AnyObject, NSObject, NSObjectProtocol};
use objc2::{define_class, msg_send, sel, DefinedClass, MainThreadOnly};
use objc2_app_kit::{
    NSAppearance, NSAppearanceNameAqua, NSAppearanceNameDarkAqua, NSApplication, NSApplicationDelegate,
    NSBitmapImageFileType, NSControlTextEditingDelegate, NSImage, NSMenuItem, NSMenuItemValidation,
    NSOpenPanel, NSOutlineView, NSOutlineViewDataSource, NSOutlineViewDelegate, NSTableColumn, NSTableView,
    NSTableViewDataSource, NSTableViewDelegate, NSTextDelegate, NSTextView, NSTextViewDelegate, NSToolbar,
    NSToolbarDelegate, NSToolbarFlexibleSpaceItemIdentifier, NSToolbarItem, NSToolbarItemIdentifier,
    NSToolbarItemValidation, NSWindowDelegate,
};
use objc2_foundation::{
    ns_string, MainThreadMarker, NSArray, NSDictionary, NSInteger, NSMutableAttributedString, NSNotification,
    NSNumber, NSRange, NSString, NSUInteger, NSURL,
};

use ide_core::buildlog::{BuildLog, LineKind, Problem};
use ide_core::logger::{self, LogLevel};
use ide_core::proc::run_streaming;
use ide_core::project::{build_plan, has_project, load_project, Project, ProjectType, Tier};
use ide_core::settings::{self, Settings};
use ide_core::signing::SncSigningCaps;
use ide_core::toolchain::find_snc;
use ide_core::tree::{folder_tree, is_sentinel_file, open_request, project_tree, Node, NodeKind};

use crate::palette::{color, Role};
use crate::ui::{self, Views, DOCK_OUTPUT, DOCK_PROBLEMS};

/// What the command line asked for.
#[derive(Debug, Default)]
pub struct Args {
    pub path: Option<PathBuf>,
    pub build: bool,
    pub run: bool,
    /// Quit when the requested build/run finishes, with its exit code, echoing Output to
    /// stdout — so the real app's build loop can be exercised from a shell.
    pub exit_when_done: bool,
    /// When the requested work is done (or right after launch, if none), draw the window's
    /// content into this PNG and quit. The app renders itself offscreen, as the Windows
    /// d2d_render test does — no screen capture, no permission, no window in front.
    pub snapshot: Option<PathBuf>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Job {
    Build { then_run: bool },
    Run,
}

/// One row of the tree, flattened: AppKit asks for children by index.
struct Row {
    label: String,
    kind: NodeKind,
    children: Vec<usize>,
}

struct State {
    args: Args,
    settings: Settings,
    snc: Option<(PathBuf, SncSigningCaps)>,
    root: Option<PathBuf>,
    project: Option<Project>,
    target: usize,
    tier: Tier,
    rows: Vec<Row>,
    top: Vec<usize>,
    /// The outline view's items. AppKit does not retain the objects a data source hands
    /// it, so they live here for as long as the tree does.
    items: Vec<Retained<NSNumber>>,
    file: Option<PathBuf>,
    file_text: String,
    log: BuildLog,
    job: Option<Job>,
    /// Where the job runs; a Problem's relative path is relative to this.
    job_dir: PathBuf,
    artifact: Option<PathBuf>,
}

pub struct Ivars {
    state: RefCell<State>,
    views: OnceCell<Views>,
}

define_class!(
    // SAFETY: NSObject has no subclassing requirements, and Controller does not implement Drop.
    #[unsafe(super = NSObject)]
    #[thread_kind = MainThreadOnly]
    #[name = "SentinelIDEController"]
    #[ivars = Ivars]
    pub struct Controller;

    unsafe impl NSObjectProtocol for Controller {}

    unsafe impl NSApplicationDelegate for Controller {
        // The window exists before any file is opened at launch: a double-clicked document
        // arrives through application:openURLs: between will- and did-finish-launching.
        #[unsafe(method(applicationWillFinishLaunching:))]
        fn will_finish_launching(&self, _n: &NSNotification) {
            self.build_ui();
        }

        #[unsafe(method(applicationDidFinishLaunching:))]
        fn did_finish_launching(&self, _n: &NSNotification) {
            let mtm = self.mtm();
            let app = NSApplication::sharedApplication(mtm);
            self.views().window.makeKeyAndOrderFront(None);
            #[allow(deprecated)]
            app.activateIgnoringOtherApps(true);
            let (path, build, run) = {
                let st = self.st();
                (st.args.path.clone(), st.args.build, st.args.run)
            };
            if let Some(p) = path {
                self.open_path(&p);
            }
            if build || run {
                self.start_build(run);
            } else if self.st().args.snapshot.is_some() {
                // Let one turn of the run loop lay the window out before it is drawn.
                post(|c| c.finish(0));
            }
        }

        #[unsafe(method(application:openURLs:))]
        fn open_urls(&self, _app: &NSApplication, urls: &NSArray<NSURL>) {
            if let Some(path) = urls.iter().next().and_then(|u| u.path()) {
                self.open_path(Path::new(&path.to_string()));
            }
        }

        #[unsafe(method(applicationShouldTerminateAfterLastWindowClosed:))]
        fn terminate_after_last_window(&self, _app: &NSApplication) -> bool {
            true
        }
    }

    unsafe impl NSWindowDelegate for Controller {}

    // ---- the project tree ----
    unsafe impl NSOutlineViewDataSource for Controller {
        #[unsafe(method(outlineView:numberOfChildrenOfItem:))]
        fn tree_count(&self, _v: &NSOutlineView, item: Option<&AnyObject>) -> NSInteger {
            let st = self.st();
            match row_of(item) {
                None => st.top.len() as NSInteger,
                Some(i) => st.rows.get(i).map_or(0, |r| r.children.len() as NSInteger),
            }
        }

        #[unsafe(method_id(outlineView:child:ofItem:))]
        fn tree_child(&self, _v: &NSOutlineView, index: NSInteger, item: Option<&AnyObject>) -> Retained<AnyObject> {
            let st = self.st();
            let list = match row_of(item) {
                None => &st.top,
                Some(i) => &st.rows[i].children,
            };
            let n = st.items[list[index as usize]].clone();
            // SAFETY: every NSNumber is an AnyObject.
            unsafe { Retained::cast_unchecked(n) }
        }

        #[unsafe(method(outlineView:isItemExpandable:))]
        fn tree_expandable(&self, _v: &NSOutlineView, item: &AnyObject) -> bool {
            let st = self.st();
            row_of(Some(item)).and_then(|i| st.rows.get(i)).is_some_and(|r| {
                !r.children.is_empty() || matches!(r.kind, NodeKind::Dir(_) | NodeKind::Group | NodeKind::Project)
            })
        }

        #[unsafe(method_id(outlineView:objectValueForTableColumn:byItem:))]
        fn tree_value(
            &self,
            _v: &NSOutlineView,
            _c: Option<&NSTableColumn>,
            item: Option<&AnyObject>,
        ) -> Option<Retained<AnyObject>> {
            self.tree_label(item)
        }
    }

    unsafe impl NSControlTextEditingDelegate for Controller {}

    unsafe impl NSOutlineViewDelegate for Controller {
        #[unsafe(method(outlineViewSelectionDidChange:))]
        fn tree_selection_changed(&self, _n: &NSNotification) {
            let v = self.views();
            let row = v.outline.selectedRow();
            if row < 0 {
                return;
            }
            let Some(i) = row_of(v.outline.itemAtRow(row).as_deref()) else { return };
            let kind = self.st().rows.get(i).map(|r| r.kind.clone());
            match kind {
                Some(NodeKind::File(p)) => self.show_file(&p),
                Some(NodeKind::Target(t)) => self.set_target(t),
                _ => {}
            }
        }
    }

    // ---- the Problems table ----
    unsafe impl NSTableViewDataSource for Controller {
        #[unsafe(method(numberOfRowsInTableView:))]
        fn problem_count(&self, _t: &NSTableView) -> NSInteger {
            self.st().log.problems.len() as NSInteger
        }

        #[unsafe(method_id(tableView:objectValueForTableColumn:row:))]
        fn problem_value(
            &self,
            _t: &NSTableView,
            column: Option<&NSTableColumn>,
            row: NSInteger,
        ) -> Option<Retained<AnyObject>> {
            self.problem_text(column, row)
        }
    }

    unsafe impl NSTableViewDelegate for Controller {}

    // ---- clickable file:line:col in Output ----
    unsafe impl NSTextDelegate for Controller {}

    unsafe impl NSTextViewDelegate for Controller {
        #[unsafe(method(textView:clickedOnLink:atIndex:))]
        fn link_clicked(&self, _v: &NSTextView, link: &AnyObject, _at: NSUInteger) -> bool {
            self.follow_link(link)
        }
    }

    // ---- toolbar ----
    unsafe impl NSToolbarDelegate for Controller {
        #[unsafe(method_id(toolbarDefaultItemIdentifiers:))]
        fn toolbar_default(&self, _t: &NSToolbar) -> Retained<NSArray<NSToolbarItemIdentifier>> {
            toolbar_ids()
        }

        #[unsafe(method_id(toolbarAllowedItemIdentifiers:))]
        fn toolbar_allowed(&self, _t: &NSToolbar) -> Retained<NSArray<NSToolbarItemIdentifier>> {
            toolbar_ids()
        }

        #[unsafe(method_id(toolbar:itemForItemIdentifier:willBeInsertedIntoToolbar:))]
        fn toolbar_item(&self, _t: &NSToolbar, id: &NSToolbarItemIdentifier, _insert: bool) -> Option<Retained<NSToolbarItem>> {
            self.toolbar_item_for(id)
        }
    }

    unsafe impl NSMenuItemValidation for Controller {
        #[unsafe(method(validateMenuItem:))]
        fn validate_menu_item(&self, item: &NSMenuItem) -> bool {
            self.can_do(item.action())
        }
    }

    unsafe impl NSToolbarItemValidation for Controller {
        #[unsafe(method(validateToolbarItem:))]
        fn validate_toolbar_item(&self, item: &NSToolbarItem) -> bool {
            self.can_do(item.action())
        }
    }

    // ---- commands ----
    impl Controller {
        #[unsafe(method(openProject:))]
        fn open_project_action(&self, _sender: Option<&AnyObject>) {
            let panel = NSOpenPanel::openPanel(self.mtm());
            panel.setCanChooseFiles(true);
            panel.setCanChooseDirectories(true);
            panel.setAllowsMultipleSelection(false);
            panel.setMessage(Some(ns_string!("Choose a project folder, a .sntproject, or a sentinel.toml")));
            if panel.runModal() == 1 {
                if let Some(path) = panel.URLs().iter().next().and_then(|u| u.path()) {
                    self.open_path(Path::new(&path.to_string()));
                }
            }
        }

        #[unsafe(method(openRecent:))]
        fn open_recent_action(&self, sender: Option<&AnyObject>) {
            let Some(item) = sender.and_then(|s| s.downcast_ref::<NSMenuItem>()) else { return };
            let path = self.st().settings.recents.get(item.tag() as usize).cloned();
            match path {
                Some(p) if p.is_dir() => self.open_path(&p),
                Some(p) => {
                    // Gone since it was recorded: drop it rather than offer it again.
                    self.st_mut().settings.recents.retain(|r| r != &p);
                    self.save_settings();
                    self.rebuild_recents();
                    self.set_status(&format!("No longer there: {}", p.display()));
                }
                None => {}
            }
        }

        #[unsafe(method(clearRecents:))]
        fn clear_recents_action(&self, _sender: Option<&AnyObject>) {
            self.st_mut().settings.recents.clear();
            self.save_settings();
            self.rebuild_recents();
        }

        #[unsafe(method(closeProject:))]
        fn close_project_action(&self, _sender: Option<&AnyObject>) {
            self.close_project();
        }

        #[unsafe(method(build:))]
        fn build_action(&self, _sender: Option<&AnyObject>) {
            self.start_build(false);
        }

        #[unsafe(method(run:))]
        fn run_action(&self, _sender: Option<&AnyObject>) {
            self.start_build(true);
        }

        #[unsafe(method(problemClicked:))]
        fn problem_clicked(&self, _sender: Option<&AnyObject>) {
            let row = self.views().problems.clickedRow();
            if row < 0 {
                return;
            }
            let p = self.st().log.problems.get(row as usize).cloned();
            if let Some(p) = p {
                self.goto(&p);
            }
        }
    }
);

fn toolbar_ids() -> Retained<NSArray<NSToolbarItemIdentifier>> {
    // SAFETY: the flexible-space identifier is AppKit's own constant.
    let flex = unsafe { NSToolbarFlexibleSpaceItemIdentifier };
    NSArray::from_slice(&[ns_string!("build"), ns_string!("run"), flex])
}

fn row_of(item: Option<&AnyObject>) -> Option<usize> {
    item?.downcast_ref::<NSNumber>().map(|n| n.as_usize())
}

fn utf16_len(s: &str) -> usize {
    s.encode_utf16().count()
}

// ---- main-thread access ----------------------------------------------------------------

thread_local! {
    static CONTROLLER: OnceCell<Retained<Controller>> = const { OnceCell::new() };
}

pub fn install(c: Retained<Controller>) {
    CONTROLLER.with(|cell| {
        let _ = cell.set(c);
    });
}

/// Queue `f` on the main thread, with the controller.
fn post(f: impl FnOnce(&Controller) + Send + 'static) {
    DispatchQueue::main().exec_async(move || {
        CONTROLLER.with(|cell| {
            if let Some(c) = cell.get() {
                f(c);
            }
        });
    });
}

impl Controller {
    pub fn new(mtm: MainThreadMarker, args: Args) -> Retained<Self> {
        let path = settings::settings_path();
        let settings = settings::load_settings(&path);
        if let Some(dir) = settings.log_file.parent() {
            let _ = fs::create_dir_all(dir);
        }
        logger::logger().configure(&settings.log_file, settings.log_level);
        logger::log(LogLevel::Info, &format!("Sentinel-IDE {} starting", env!("CARGO_PKG_VERSION")));
        let snc = resolve_snc(&settings);
        match &snc {
            Some((p, caps)) => logger::log(
                LogLevel::Info,
                &format!("Toolchain — snc={} (verify={}, sign={})", p.display(), caps.verify, caps.sign),
            ),
            None => logger::log(LogLevel::Warn, "Toolchain — no snc found"),
        }
        let state = State {
            args,
            settings,
            snc,
            root: None,
            project: None,
            target: 0,
            tier: Tier::Experimental,
            rows: Vec::new(),
            top: Vec::new(),
            items: Vec::new(),
            file: None,
            file_text: String::new(),
            log: BuildLog::new(),
            job: None,
            job_dir: PathBuf::new(),
            artifact: None,
        };
        let this = Self::alloc(mtm).set_ivars(Ivars { state: RefCell::new(state), views: OnceCell::new() });
        // SAFETY: NSObject's init has this signature.
        unsafe { msg_send![super(this), init] }
    }

    // ---- bodies of the delegate methods above that need an early return ----
    // (define_class! wraps a method's body, so `?` and `return` belong out here.)

    fn tree_label(&self, item: Option<&AnyObject>) -> Option<Retained<AnyObject>> {
        let st = self.st();
        let label = st.rows.get(row_of(item)?)?.label.clone();
        // SAFETY: every NSString is an AnyObject.
        Some(unsafe { Retained::cast_unchecked(NSString::from_str(&label)) })
    }

    fn problem_text(&self, column: Option<&NSTableColumn>, row: NSInteger) -> Option<Retained<AnyObject>> {
        let st = self.st();
        let p = st.log.problems.get(usize::try_from(row).ok()?)?;
        let text = match column?.identifier().to_string().as_str() {
            "message" => p.message.clone(),
            "file" => {
                Path::new(&p.file).file_name().map_or(p.file.clone(), |n| n.to_string_lossy().into_owned())
            }
            _ => p.line.to_string(),
        };
        Some(unsafe { Retained::cast_unchecked(NSString::from_str(&text)) })
    }

    fn follow_link(&self, link: &AnyObject) -> bool {
        let Some(s) = link.downcast_ref::<NSString>() else { return false };
        let Some(i) = s.to_string().strip_prefix("problem:").and_then(|n| n.parse::<usize>().ok()) else {
            return false;
        };
        let p = self.st().log.problems.get(i).cloned();
        if let Some(p) = p {
            self.goto(&p);
        }
        true
    }

    fn toolbar_item_for(&self, id: &NSToolbarItemIdentifier) -> Option<Retained<NSToolbarItem>> {
        let (label, symbol, action, tip) = match id.to_string().as_str() {
            "build" => ("Build", "hammer", sel!(build:), "Build the active target (⌘B)"),
            "run" => ("Run", "play.fill", sel!(run:), "Build and run the active target (⌘R)"),
            _ => return None,
        };
        let item = NSToolbarItem::initWithItemIdentifier(NSToolbarItem::alloc(self.mtm()), id);
        item.setLabel(&NSString::from_str(label));
        item.setToolTip(Some(&NSString::from_str(tip)));
        let image = NSImage::imageWithSystemSymbolName_accessibilityDescription(
            &NSString::from_str(symbol),
            Some(&NSString::from_str(label)),
        );
        item.setImage(image.as_deref());
        unsafe {
            item.setTarget(Some(self.as_ref()));
            item.setAction(Some(action));
        }
        Some(item)
    }

    fn st(&self) -> Ref<'_, State> {
        self.ivars().state.borrow()
    }

    fn st_mut(&self) -> RefMut<'_, State> {
        self.ivars().state.borrow_mut()
    }

    fn views(&self) -> &Views {
        self.ivars().views.get().expect("views are built before anything uses them")
    }

    fn build_ui(&self) {
        let mtm = self.mtm();
        let (font, theme) = {
            let st = self.st();
            (st.settings.editor_font.clone(), st.settings.theme_mode)
        };
        // -1 follows the system; 0 and 1 force light and dark, as settings.ini says on Windows.
        let name = match theme {
            0 => Some(unsafe { NSAppearanceNameAqua }),
            1 => Some(unsafe { NSAppearanceNameDarkAqua }),
            _ => None,
        };
        let appearance = name.and_then(NSAppearance::appearanceNamed);
        NSApplication::sharedApplication(mtm).setAppearance(appearance.as_deref());

        let views = ui::build_window(mtm, self, &font);
        ui::build_menus(mtm, self, &views.recents);
        let _ = self.ivars().views.set(views);
        self.rebuild_recents();
        self.show_placeholder();
        let missing = self.st().snc.is_none();
        if missing {
            self.set_status("snc not found — set SNC, or SENTINEL_LANG to a Sentinel-lang checkout");
        }
    }

    // ---- opening things ----

    fn open_path(&self, path: &Path) {
        let path = path.canonicalize().unwrap_or_else(|_| path.to_path_buf());
        let Some(req) = open_request(&path) else {
            self.set_status(&format!("Not found: {}", path.display()));
            return;
        };
        let same = self.st().root.as_deref() == Some(req.root.as_path());
        if !same {
            self.load_folder(&req.root);
        }
        if let Some(f) = req.file {
            self.show_file(&f);
        }
    }

    fn load_folder(&self, root: &Path) {
        let project = if has_project(root) {
            match load_project(root) {
                Ok(p) => Some(p),
                Err(e) => {
                    logger::log(LogLevel::Error, &format!("load_project {}: {e}", root.display()));
                    self.set_status(&format!("Could not read the project: {e}"));
                    None
                }
            }
        } else {
            None
        };
        let tree = match &project {
            Some(p) => project_tree(p),
            None => folder_tree(root),
        };
        let entry = project.as_ref().and_then(|p| {
            let e = p.resolve(&p.targets[0].entry);
            (!p.targets[0].entry.is_empty() && e.is_file()).then_some(e)
        });
        {
            let mut st = self.st_mut();
            st.tier = project.as_ref().map_or(Tier::Experimental, |p| p.default_tier);
            if project.is_some() {
                st.settings.add_recent(root);
            }
            if let Some(p) = &project {
                logger::log(
                    LogLevel::Info,
                    &format!(
                        "Loaded project: {} ({}), {} target(s), tier={}, signing require={}",
                        p.name,
                        p.r#type.name(),
                        p.targets.len(),
                        st.tier.name(),
                        p.sign_require
                    ),
                );
            }
            st.root = Some(root.to_path_buf());
            st.project = project;
            st.target = 0;
            st.file = None;
            set_rows(&mut st, &tree);
        }
        self.save_settings();
        self.reload_tree();
        self.rebuild_recents();
        self.refresh_title();
        match entry {
            Some(e) => self.show_file(&e),
            None => self.show_placeholder(),
        }
        self.set_status(&root.display().to_string());
    }

    fn close_project(&self) {
        {
            let mut st = self.st_mut();
            st.root = None;
            st.project = None;
            st.file = None;
            st.file_text.clear();
            st.log = BuildLog::new();
            st.rows.clear();
            st.top.clear();
            st.items.clear();
        }
        let v = self.views();
        self.reload_tree();
        v.problems.reloadData();
        v.output.setString(ns_string!(""));
        self.show_placeholder();
        self.refresh_title();
        self.set_status("Open a project to start  (File ▸ Open Project…, ⌘O)");
    }

    fn reload_tree(&self) {
        let v = self.views();
        v.outline.reloadData();
        // Expand the project and its groups, as the Windows tree opens on them.
        let expand: Vec<Retained<NSNumber>> = {
            let st = self.st();
            st.rows
                .iter()
                .enumerate()
                .filter(|(_, r)| matches!(r.kind, NodeKind::Project | NodeKind::Group) || st.top.len() == 1)
                .filter(|(i, r)| st.top.contains(i) || matches!(r.kind, NodeKind::Group))
                .map(|(i, _)| st.items[i].clone())
                .collect()
        };
        for item in expand {
            unsafe { v.outline.expandItem_expandChildren(Some(&item), false) };
        }
    }

    fn set_target(&self, t: usize) {
        {
            let mut st = self.st_mut();
            if st.project.as_ref().is_none_or(|p| t >= p.targets.len()) {
                return;
            }
            st.target = t;
        }
        self.refresh_title();
    }

    fn refresh_title(&self) {
        let subtitle = {
            let st = self.st();
            match (&st.project, &st.root) {
                (Some(p), _) => {
                    let t = &p.targets[st.target];
                    let tn = if t.name.is_empty() { &p.name } else { &t.name };
                    if p.targets.len() > 1 {
                        format!("{} › {} ({}) · {}", p.name, tn, t.r#type.name(), st.tier.name())
                    } else {
                        format!("{} ({}) · {}", p.name, t.r#type.name(), st.tier.name())
                    }
                }
                (None, Some(r)) => {
                    r.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
                }
                (None, None) => String::new(),
            }
        };
        self.views().window.setSubtitle(&NSString::from_str(&subtitle));
    }

    fn show_placeholder(&self) {
        let v = self.views();
        v.viewer.setString(ns_string!(
            "\n  Open a project to start — File ▸ Open Project… (⌘O)\n\n  \
             A project is a folder with a *.sntproject or a sentinel.toml.\n  \
             Build with ⌘B; build and run with ⌘R.\n"
        ));
        v.viewer.setTextColor(Some(&color(Role::TextMuted)));
    }

    /// Read-only until M2 brings the editor: the file as it is on disk.
    fn show_file(&self, path: &Path) {
        let text = match fs::read(path) {
            Ok(b) => String::from_utf8_lossy(&b).into_owned(),
            Err(e) => {
                self.set_status(&format!("Could not read {}: {e}", path.display()));
                return;
            }
        };
        let v = self.views();
        v.viewer.setString(&NSString::from_str(&text));
        v.viewer.setFont(Some(&v.mono));
        v.viewer.setTextColor(Some(&color(Role::TextPrimary)));
        {
            let mut st = self.st_mut();
            st.file = Some(path.to_path_buf());
            st.file_text = text;
        }
        self.set_status(&path.display().to_string());
    }

    /// Open a Problem's file and put its line in view. The line number is clamped: it is
    /// whatever digits the build output held, and Sentinel's arithmetic wraps on overflow.
    fn goto(&self, p: &Problem) {
        let file = {
            let st = self.st();
            let f = Path::new(&p.file);
            if f.is_absolute() {
                f.to_path_buf()
            } else {
                st.job_dir.join(f)
            }
        };
        let shown = self.st().file.as_deref() == Some(file.as_path());
        if !shown {
            if !file.is_file() {
                self.set_status(&format!("Not found: {}", file.display()));
                return;
            }
            self.show_file(&file);
        }
        let range = {
            let st = self.st();
            line_range(&st.file_text, p.line)
        };
        let v = self.views();
        v.viewer.setSelectedRange(range);
        v.viewer.scrollRangeToVisible(range);
        v.viewer.showFindIndicatorForRange(range);
        v.window.makeFirstResponder(Some(&v.viewer));
        self.set_status(&format!("{}:{}:{} — {}", file.display(), p.line, p.col, p.message));
    }

    // ---- build / run ----

    fn can_do(&self, action: Option<objc2::runtime::Sel>) -> bool {
        let Some(a) = action else { return true };
        let st = self.st();
        if a == sel!(build:) || a == sel!(run:) {
            st.job.is_none()
                && st.snc.is_some()
                && (st.project.is_some() || st.file.as_deref().is_some_and(is_sentinel_file))
        } else if a == sel!(closeProject:) {
            st.root.is_some()
        } else if a == sel!(clearRecents:) {
            !st.settings.recents.is_empty()
        } else {
            true
        }
    }

    fn start_build(&self, then_run: bool) {
        struct Plan {
            snc: PathBuf,
            args: Vec<OsString>,
            dir: PathBuf,
            out_dir: PathBuf,
            artifact: PathBuf,
            runnable: bool,
            note: Option<String>,
        }
        let plan = {
            let st = self.st();
            if st.job.is_some() {
                return;
            }
            let Some((snc, _)) = st.snc.clone() else {
                drop(st);
                self.set_status("snc not found — set SNC, or SENTINEL_LANG to a Sentinel-lang checkout");
                return;
            };
            if let Some(p) = &st.project {
                let t = &p.targets[st.target];
                if t.entry.is_empty() || !p.resolve(&t.entry).is_file() {
                    let msg = format!("Target entry not found: {}", t.entry);
                    drop(st);
                    self.set_status(&msg);
                    return;
                }
                let b = build_plan(p, t, st.tier);
                Plan {
                    snc,
                    args: b.args,
                    dir: p.dir.clone(),
                    out_dir: b.out_dir,
                    artifact: b.artifact,
                    runnable: t.r#type == ProjectType::Executable,
                    note: Some(format!(
                        "  (tier {}: snc has no tier flag yet — built at -O0 → target/{})",
                        st.tier.name(),
                        st.tier.dir()
                    )),
                }
            } else if let Some(f) = st.file.clone().filter(|f| is_sentinel_file(f)) {
                let dir = f.parent().map(Path::to_path_buf).unwrap_or_default();
                let artifact = dir.join(f.file_stem().unwrap_or_default());
                Plan {
                    snc,
                    args: vec!["build".into(), f.into(), "-o".into(), artifact.clone().into()],
                    out_dir: dir.clone(),
                    dir,
                    artifact,
                    runnable: true,
                    note: None,
                }
            } else {
                drop(st);
                self.set_status("Open a .sentinel file or a project to build");
                return;
            }
        };
        if then_run && !plan.runnable {
            self.set_status("Run applies to executable targets");
            return;
        }
        if let Err(e) = fs::create_dir_all(&plan.out_dir) {
            self.set_status(&format!("Could not create {}: {e}", plan.out_dir.display()));
            return;
        }
        {
            let mut st = self.st_mut();
            st.log = BuildLog::new();
            st.job = Some(Job::Build { then_run });
            st.job_dir = plan.dir.clone();
            st.artifact = Some(plan.artifact);
        }
        let v = self.views();
        v.problems.reloadData();
        v.output.setString(ns_string!(""));
        v.dock.selectTabViewItemAtIndex(DOCK_OUTPUT);
        let cmd = display_command(&plan.snc, &plan.args);
        logger::log(LogLevel::Info, &format!("Build: {cmd}"));
        self.append(&format!("> {cmd}"), Role::TextSecondary, None);
        if let Some(n) = plan.note {
            self.append(&n, Role::TextMuted, None);
        }
        self.set_status("Building…");
        self.revalidate();
        let job = Job::Build { then_run };
        spawn(job, plan.snc, plan.args, plan.dir);
    }

    fn start_run(&self) {
        let (artifact, dir) = {
            let st = self.st();
            (st.artifact.clone(), st.job_dir.clone())
        };
        let Some(artifact) = artifact.filter(|a| a.is_file()) else {
            self.append("No executable yet — Build first.", Role::DiagWarning, None);
            self.finish(1);
            return;
        };
        self.st_mut().job = Some(Job::Run);
        self.append("", Role::TextMuted, None);
        self.append(&format!("> {}", artifact.display()), Role::TextSecondary, None);
        self.set_status("Running…");
        spawn(Job::Run, artifact, Vec::new(), dir);
    }

    fn on_line(&self, line: String) {
        let (report, index) = {
            let mut st = self.st_mut();
            let r = st.log.push(&line);
            let i = st.log.problems.len().saturating_sub(1);
            (r, i)
        };
        let role = match report.kind {
            LineKind::Error => Role::DiagError,
            LineKind::Warning => Role::DiagWarning,
            LineKind::Command => Role::TextSecondary,
            LineKind::Plain => Role::TextPrimary,
        };
        let link = report.problem.as_ref().map(|(r, _)| (r.clone(), index));
        self.append(&line, role, link);
        if report.problem.is_some() {
            self.views().problems.reloadData();
        }
    }

    fn on_done(&self, job: Job, result: Result<ExitStatus, String>) {
        let code = match result {
            Ok(s) => s.code().unwrap_or_else(|| 128 + s.signal().unwrap_or(0)),
            Err(e) => {
                self.append(&format!("Could not start: {e}"), Role::DiagError, None);
                -1
            }
        };
        self.append("", Role::TextMuted, None);
        let ok = code == 0;
        self.append(
            &format!("[done · exit {code}]"),
            if ok { Role::TrustVerified } else { Role::DiagError },
            None,
        );
        self.st_mut().job = None;
        match job {
            Job::Build { then_run: true } if ok => self.start_run(),
            Job::Build { .. } => {
                let n = self.st().log.problems.len();
                if n > 0 {
                    self.views().dock.selectTabViewItemAtIndex(DOCK_PROBLEMS);
                }
                let s = if ok {
                    "Build succeeded".to_string()
                } else {
                    format!("Build failed (exit {code}) — {n} problem(s)")
                };
                self.set_status(&s);
                self.finish(code);
            }
            Job::Run => {
                self.set_status(&format!("Run finished (exit {code})"));
                self.finish(code);
            }
        }
    }

    /// The last step of a command-line request. With `--exit-when-done`, the transcript and
    /// the Problems go to stdout and the app exits with the job's code.
    fn finish(&self, code: i32) {
        self.revalidate();
        let snapshot = self.st().args.snapshot.clone();
        if let Some(path) = snapshot {
            match self.snapshot(&path) {
                Ok(()) => println!("snapshot: {}", path.display()),
                Err(e) => {
                    eprintln!("snapshot failed: {e}");
                    std::process::exit(2);
                }
            }
            if !self.st().args.exit_when_done {
                std::process::exit(code);
            }
        }
        if !self.st().args.exit_when_done {
            return;
        }
        let out = self.views().output.string().to_string();
        print!("{out}");
        for p in &self.st().log.problems {
            println!("problem: {}:{}:{} {}", p.file, p.line, p.col, p.message);
        }
        std::process::exit(code);
    }

    fn snapshot(&self, path: &Path) -> Result<(), String> {
        // The content view's superview is the window's frame view, which draws the window
        // background, the titlebar and the toolbar — so the picture is the whole window, not
        // just the views on it (which leave the background transparent).
        let content = self.views().window.contentView().ok_or("no content view")?;
        content.layoutSubtreeIfNeeded();
        // SAFETY: a view in a window always has a live superview chain up to the frame view.
        let content = unsafe { content.superview() }.unwrap_or(content);
        let bounds = content.bounds();
        let rep = content.bitmapImageRepForCachingDisplayInRect(bounds).ok_or("no bitmap for the window")?;
        content.cacheDisplayInRect_toBitmapImageRep(bounds, &rep);
        // SAFETY: an empty property dictionary is valid for every file type.
        let png = unsafe {
            rep.representationUsingType_properties(NSBitmapImageFileType::PNG, &NSDictionary::new())
        }
        .ok_or("PNG encoding failed")?;
        fs::write(path, png.to_vec()).map_err(|e| e.to_string())
    }

    fn revalidate(&self) {
        if let Some(t) = self.views().window.toolbar() {
            t.validateVisibleItems();
        }
    }

    // ---- small helpers ----

    fn append(&self, text: &str, role: Role, link: Option<(std::ops::Range<usize>, usize)>) {
        let v = self.views();
        let s = NSMutableAttributedString::from_nsstring(&NSString::from_str(&format!("{text}\n")));
        let all = NSRange::new(0, s.length());
        unsafe {
            s.addAttribute_value_range(objc2_app_kit::NSFontAttributeName, v.mono.as_ref(), all);
            s.addAttribute_value_range(
                objc2_app_kit::NSForegroundColorAttributeName,
                color(role).as_ref(),
                all,
            );
            if let Some((r, i)) = link {
                let at = NSRange::new(utf16_len(&text[..r.start]), utf16_len(&text[r]));
                let target = NSString::from_str(&format!("problem:{i}"));
                s.addAttribute_value_range(objc2_app_kit::NSLinkAttributeName, target.as_ref(), at);
            }
        }
        if let Some(storage) = unsafe { v.output.textStorage() } {
            storage.appendAttributedString(&s);
            v.output.scrollRangeToVisible(NSRange::new(storage.length(), 0));
        }
        if self.st().args.exit_when_done {
            logger::log(LogLevel::Debug, text);
        }
    }

    fn set_status(&self, text: &str) {
        self.views().status.setStringValue(&NSString::from_str(text));
    }

    fn save_settings(&self) {
        let r = settings::save_settings(&self.st().settings, &settings::settings_path());
        if let Err(e) = r {
            logger::log(LogLevel::Warn, &format!("save settings: {e}"));
        }
    }

    fn rebuild_recents(&self) {
        let recents = self.st().settings.recents.clone();
        let menu = &self.views().recents;
        menu.removeAllItems();
        let mtm = self.mtm();
        for (i, r) in recents.iter().enumerate() {
            let name = r.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            let parent = r.parent().map(|p| p.display().to_string()).unwrap_or_default();
            let item = unsafe {
                NSMenuItem::initWithTitle_action_keyEquivalent(
                    NSMenuItem::alloc(mtm),
                    &NSString::from_str(&format!("{name} — {parent}")),
                    Some(sel!(openRecent:)),
                    ns_string!(""),
                )
            };
            item.setTag(i as NSInteger);
            unsafe { item.setTarget(Some(self.as_ref())) };
            menu.addItem(&item);
        }
        menu.addItem(&NSMenuItem::separatorItem(mtm));
        let clear = unsafe {
            NSMenuItem::initWithTitle_action_keyEquivalent(
                NSMenuItem::alloc(mtm),
                ns_string!("Clear Menu"),
                Some(sel!(clearRecents:)),
                ns_string!(""),
            )
        };
        unsafe { clear.setTarget(Some(self.as_ref())) };
        menu.addItem(&clear);
    }
}

fn spawn(job: Job, program: PathBuf, args: Vec<OsString>, dir: PathBuf) {
    std::thread::spawn(move || {
        let result = run_streaming(&program, &args, Some(&dir), |line| post(move |c| c.on_line(line)));
        let result = result.map_err(|e| e.to_string());
        post(move |c| c.on_done(job, result));
    });
}

fn resolve_snc(s: &Settings) -> Option<(PathBuf, SncSigningCaps)> {
    if !s.snc_path.as_os_str().is_empty() && s.snc_path.is_file() {
        return Some((s.snc_path.clone(), ide_core::signing::snc_signing_caps(&s.snc_path)));
    }
    let built_with: Vec<PathBuf> =
        option_env!("SENTINEL_IDE_BUILD_SNC").map(PathBuf::from).into_iter().collect();
    find_snc(None, &built_with)
}

/// `snc build "/a b/x.sentinel" -o …`, for the Output pane and the log — never executed.
fn display_command(program: &Path, args: &[OsString]) -> String {
    let quote = |s: &str| if s.contains(' ') { format!("\"{s}\"") } else { s.to_string() };
    let name = program.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    std::iter::once(name)
        .chain(args.iter().map(|a| quote(&a.to_string_lossy())))
        .collect::<Vec<_>>()
        .join(" ")
}

fn set_rows(st: &mut State, root: &Node) {
    fn add(rows: &mut Vec<Row>, n: &Node) -> usize {
        let i = rows.len();
        rows.push(Row { label: n.label.clone(), kind: n.kind.clone(), children: Vec::new() });
        let kids: Vec<usize> = n.children.iter().map(|c| add(rows, c)).collect();
        rows[i].children = kids;
        i
    }
    st.rows.clear();
    let top = add(&mut st.rows, root);
    st.top = vec![top];
    st.items = (0..st.rows.len()).map(NSNumber::new_usize).collect();
}

/// The UTF-16 range of line `line` (1-based, clamped into the text) without its line break.
fn line_range(text: &str, line: i64) -> NSRange {
    let lines: Vec<&str> = text.split_inclusive('\n').collect();
    if lines.is_empty() {
        return NSRange::new(0, 0);
    }
    let n = (line.clamp(1, lines.len() as i64) - 1) as usize;
    let start: usize = lines[..n].iter().map(|l| utf16_len(l)).sum();
    let body = lines[n].trim_end_matches(['\n', '\r']);
    NSRange::new(start, utf16_len(body))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn line_ranges_are_utf16_and_clamped() {
        let t = "fn main() {\r\n  let café = 1;\r\n}\r\n";
        let r = line_range(t, 2);
        assert_eq!((r.location, r.length), (13, 15));
        assert_eq!(line_range(t, i64::MIN).location, 0, "a wrapped line number lands on line 1");
        let last = line_range(t, i64::MAX);
        assert_eq!((last.location, last.length), (30, 1));
    }

    #[test]
    fn commands_display_with_quotes_only_where_needed() {
        let args: Vec<OsString> =
            vec!["build".into(), "/a b/x.sentinel".into(), "-o".into(), "/out/x".into()];
        assert_eq!(display_command(Path::new("/opt/snc"), &args), "snc build \"/a b/x.sentinel\" -o /out/x");
    }
}
