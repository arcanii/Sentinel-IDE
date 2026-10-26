// SPDX-License-Identifier: GPL-3.0-or-later
//! The dark/coral palette — Theme.h's values, as NSColors that follow the window's
//! appearance. Each colour carries its dark and its light value and AppKit asks for the
//! right one whenever the appearance changes, so a switch in System Settings re-colours
//! the Output pane without the app doing anything.

use std::ptr::NonNull;

use block2::RcBlock;
use objc2::rc::Retained;
use objc2_app_kit::{NSAppearance, NSAppearanceNameAqua, NSAppearanceNameDarkAqua, NSColor};
use objc2_foundation::NSArray;

type Rgb = (u8, u8, u8);

/// `(dark, light)` pairs from Theme.h's makeDarkTheme / makeLightTheme. The light theme's
/// window and panel colours are the system's on Windows; here they are the system's too.
#[derive(Clone, Copy)]
pub enum Role {
    WindowBg,
    TextPrimary,
    TextSecondary,
    TextMuted,
    DiagError,
    DiagWarning,
    TrustVerified,
}

fn values(r: Role) -> (Rgb, Rgb) {
    match r {
        Role::WindowBg => ((22, 22, 24), (255, 255, 255)),
        Role::TextPrimary => ((230, 230, 232), (24, 24, 26)),
        Role::TextSecondary => ((154, 154, 160), (96, 96, 102)),
        Role::TextMuted => ((106, 106, 112), (140, 140, 146)),
        Role::DiagError => ((224, 108, 117), (192, 57, 43)),
        Role::DiagWarning => ((229, 192, 123), (138, 109, 0)),
        Role::TrustVerified => ((127, 179, 122), (46, 125, 50)),
    }
}

fn srgb((r, g, b): Rgb) -> Retained<NSColor> {
    NSColor::colorWithSRGBRed_green_blue_alpha(r as f64 / 255.0, g as f64 / 255.0, b as f64 / 255.0, 1.0)
}

pub fn is_dark(a: &NSAppearance) -> bool {
    // SAFETY: the two names are AppKit's own constants.
    let names = unsafe { NSArray::from_slice(&[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]) };
    a.bestMatchFromAppearancesWithNames(&names).is_some_and(|n| &*n == unsafe { NSAppearanceNameDarkAqua })
}

pub fn color(role: Role) -> Retained<NSColor> {
    let (d, l) = values(role);
    let (dark, light) = (srgb(d), srgb(l));
    let provider = RcBlock::new(move |a: NonNull<NSAppearance>| -> NonNull<NSColor> {
        // SAFETY: AppKit hands the provider a live appearance for the duration of the call.
        let a = unsafe { a.as_ref() };
        // The block owns both colours, and the dynamic colour owns the block, so the
        // pointer handed back outlives every use AppKit makes of it.
        NonNull::from(if is_dark(a) { &*dark } else { &*light })
    });
    // SAFETY: the provider returns a valid colour for every appearance.
    unsafe { NSColor::colorWithName_dynamicProvider(None, &provider) }
}
