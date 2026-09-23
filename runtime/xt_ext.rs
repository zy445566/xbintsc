// xbintsc native-extension bindings for Rust.
//
// Include this file from a Rust `staticlib`/`cdylib` that xbintsc links into a
// generated binary:
//
//     // src/lib.rs
//     include!("../../runtime/xt_ext.rs");
//
//     #[no_mangle]
//     pub extern "C" fn mathx_add(argc: i32, argv: *const XtValue) -> XtValue {
//         let a = arg_number(argc, argv, 0, 0.0);
//         let b = arg_number(argc, argv, 1, 0.0);
//         xt_number(a + b)
//     }
//
// The exported functions use the runtime ABI, so a native manifest maps them
// exactly like C or C++ symbols: `xt_value name(int32_t argc, xt_value *argv)`.
//
// Build with the runtime directory reachable and produce a static library:
//
//     cargo build --release      # crate-type = ["staticlib"]
//     # -> target/release/libmathx.a

/// A JavaScript value: a 64-bit NaN-boxed word (see `runtime/rt.h`).
pub type XtValue = u64;

pub const XT_TAG_MASK: XtValue = 0xFFFF_0000_0000_0000;
pub const XT_NUMBER_MASK: XtValue = 0xFFF8_0000_0000_0000;

pub const XT_UNDEFINED: XtValue = 0xFFF8_0000_0000_0000;
pub const XT_NULL: XtValue = 0xFFF9_0000_0000_0000;
pub const XT_FALSE: XtValue = 0xFFFA_0000_0000_0000;
pub const XT_TAG_STRING: XtValue = 0xFFFC_0000_0000_0000;

// Raw runtime entry points. They are wrapped in safe helpers below so that
// extension authors never have to spell `unsafe` just to build a value.
extern "C" {
    #[link_name = "xt_number"]
    fn raw_xt_number(d: f64) -> XtValue;
    #[link_name = "xt_string_new"]
    fn raw_xt_string_new(data: *const u8, len: usize) -> XtValue;
    #[link_name = "xt_string_data"]
    fn raw_xt_string_data(value: XtValue) -> *const u8;
    #[link_name = "xt_string_length_value"]
    fn raw_xt_string_length_value(value: XtValue) -> i32;
    #[link_name = "xt_truthy"]
    fn raw_xt_truthy(value: XtValue) -> i32;
    #[link_name = "xt_to_number"]
    fn raw_xt_to_number(value: XtValue) -> f64;
    #[link_name = "xt_to_string"]
    fn raw_xt_to_string(value: XtValue) -> XtValue;
    #[link_name = "xt_array_new"]
    fn raw_xt_array_new(count: i32, items: *mut XtValue) -> XtValue;
}

/// Box an `f64` into a JavaScript number value.
#[inline]
pub fn xt_number(d: f64) -> XtValue {
    unsafe { raw_xt_number(d) }
}

/// Build a string value from a byte slice (assumed UTF-8).
#[inline]
pub fn xt_string_new(data: *const u8, len: usize) -> XtValue {
    unsafe { raw_xt_string_new(data, len) }
}

/// Pointer to the first byte of a string value's payload.
#[inline]
pub fn xt_string_data(value: XtValue) -> *const u8 {
    unsafe { raw_xt_string_data(value) }
}

/// Byte length of a string value.
#[inline]
pub fn xt_string_length_value(value: XtValue) -> i32 {
    unsafe { raw_xt_string_length_value(value) }
}

/// JavaScript truthiness of a value (`0`/`1`).
#[inline]
pub fn xt_truthy(value: XtValue) -> i32 {
    unsafe { raw_xt_truthy(value) }
}

/// Convert a value to a number, coercing non-numbers like the runtime does.
#[inline]
pub fn xt_to_number(value: XtValue) -> f64 {
    unsafe { raw_xt_to_number(value) }
}

/// Convert a value to a string value.
#[inline]
pub fn xt_to_string(value: XtValue) -> XtValue {
    unsafe { raw_xt_to_string(value) }
}

/// Build an array from `count` items.
#[inline]
pub fn xt_array_new(count: i32, items: *mut XtValue) -> XtValue {
    unsafe { raw_xt_array_new(count, items) }
}

/// Whether a value carries the number (unboxed double) representation.
#[inline]
pub fn is_number(value: XtValue) -> bool {
    (value & XT_NUMBER_MASK) != XT_NUMBER_MASK
}

/// Whether a value is a string.
#[inline]
pub fn is_string(value: XtValue) -> bool {
    (value & XT_TAG_MASK) == XT_TAG_STRING
}

/// Reinterpret an unboxed number value as `f64`.
#[inline]
pub fn as_f64(value: XtValue) -> f64 {
    f64::from_bits(value)
}

/// Read argument `index`, returning `undefined` when it was not supplied.
///
/// `argv` may be null when a function is called with no arguments.
#[inline]
pub fn arg(argc: i32, argv: *const XtValue, index: usize) -> XtValue {
    if argv.is_null() || index >= argc as usize {
        return XT_UNDEFINED;
    }
    unsafe { *argv.add(index) }
}

/// Read argument `index` coerced to a number, or `fallback` when absent.
#[inline]
pub fn arg_number(argc: i32, argv: *const XtValue, index: usize, fallback: f64) -> f64 {
    let value = arg(argc, argv, index);
    if value == XT_UNDEFINED {
        return fallback;
    }
    xt_to_number(value)
}

/// Read argument `index` coerced to a boolean.
#[inline]
pub fn arg_bool(argc: i32, argv: *const XtValue, index: usize, fallback: bool) -> bool {
    let value = arg(argc, argv, index);
    if value == XT_UNDEFINED {
        return fallback;
    }
    xt_truthy(value) != 0
}

/// Borrow argument `index` as a UTF-8 string slice, or `None` when it is not a
/// string. The bytes live in the runtime arena and never move.
#[inline]
pub fn arg_str<'a>(argc: i32, argv: *const XtValue, index: usize) -> Option<&'a str> {
    let value = arg(argc, argv, index);
    if !is_string(value) {
        return None;
    }
    let length = xt_string_length_value(value) as usize;
    let data = xt_string_data(value);
    if data.is_null() || length == 0 {
        return Some("");
    }
    unsafe { Some(core::str::from_utf8_unchecked(core::slice::from_raw_parts(data, length))) }
}

/// Build a string value from a Rust string slice.
#[inline]
pub fn string_from(text: &str) -> XtValue {
    xt_string_new(text.as_ptr(), text.len())
}
