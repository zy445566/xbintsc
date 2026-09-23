// A Rust extension for xbintsc.
//
// The bindings for the runtime ABI live in `runtime/xt_ext.rs`; `include!`
// pulls them into this crate. Every function exported to TypeScript is
// `#[no_mangle] pub extern "C"` with the runtime signature
// `xt_value name(int32_t argc, xt_value *argv)`.
//
// Build it (see `build.sh`) with `crate-type = ["staticlib"]`, then register
// the produced `libmathx.a` through `xbintsc.manifest.json`.

include!("../../../../runtime/xt_ext.rs");

/// `add(a: number, b: number): number`
#[no_mangle]
pub extern "C" fn mathx_add(argc: i32, argv: *const XtValue) -> XtValue {
    let a = arg_number(argc, argv, 0, 0.0);
    let b = arg_number(argc, argv, 1, 0.0);
    xt_number(a + b)
}

/// `fib(n: number): number`
#[no_mangle]
pub extern "C" fn mathx_fib(argc: i32, argv: *const XtValue) -> XtValue {
    let n = arg_number(argc, argv, 0, 0.0) as i64;
    if n <= 1 {
        return xt_number(n.max(0) as f64);
    }
    let (mut previous, mut current) = (0.0_f64, 1.0_f64);
    for _ in 2..=n {
        let next = previous + current;
        previous = current;
        current = next;
    }
    xt_number(current)
}

/// `fibSequence(count: number): number[]`
#[no_mangle]
pub extern "C" fn mathx_fib_sequence(argc: i32, argv: *const XtValue) -> XtValue {
    let count = (arg_number(argc, argv, 0, 0.0) as i64).max(0) as usize;
    let mut items: Vec<XtValue> = Vec::with_capacity(count);
    let (mut previous, mut current) = (0.0_f64, 1.0_f64);
    for _ in 0..count {
        items.push(xt_number(previous));
        let next = previous + current;
        previous = current;
        current = next;
    }
    xt_array_new(items.len() as i32, items.as_mut_ptr())
}

/// `reverse(text: string): string`
#[no_mangle]
pub extern "C" fn mathx_reverse(argc: i32, argv: *const XtValue) -> XtValue {
    let text = arg_str(argc, argv, 0).unwrap_or("");
    let reversed: String = text.chars().rev().collect();
    string_from(&reversed)
}

/// `clamp(value: number, low: number, high: number): number`
#[no_mangle]
pub extern "C" fn mathx_clamp(argc: i32, argv: *const XtValue) -> XtValue {
    let value = arg_number(argc, argv, 0, 0.0);
    let low = arg_number(argc, argv, 1, 0.0);
    let high = arg_number(argc, argv, 2, 0.0);
    let (low, high) = if high < low { (high, low) } else { (low, high) };
    xt_number(value.clamp(low, high))
}
