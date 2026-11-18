/**
 * Inline element access (`obj[key]`) in generated IR.
 *
 * `xt_get_element` / `xt_set_element` are runtime calls, and without LTO clang
 * cannot inline them into generated code, so every element access paid a call
 * plus the whole generic dispatch (tag checks, index canonicalisation, a
 * property-bag fallback). Geometry code performs millions of these per level,
 * which made that call overhead the dominant cost of element access.
 *
 * This module emits the hot path *in the IR*:
 *
 *   1. guard the key to a canonical array index (`0 .. 2^32-2`, integral) —
 *      `ta[1.5]` and `ta[-1]` are ordinary property accesses and must not be
 *      truncated into element 1 / 0;
 *   2. for a plain array (`XT_TAG_ARRAY`), bounds-check against the live
 *      `length` and load/store the NaN-boxed element straight from `items`;
 *   3. for a typed-array view (`XT_TAG_OBJECT` with a resolved `view_bytes`),
 *      test the memoized element kind and load the element with that kind's own
 *      load + conversion. Writes inline only for the float kinds: integer
 *      stores need the runtime's `ToUintN`/`ToIntN` wrapping (NaN → 0, modulo),
 *      which is not a plain truncation, so those keep the runtime call.
 *
 * Everything else (strings, `Map`s, host views, out-of-range indices, a view
 * whose kind is not memoized yet, a non-numeric stored value) branches to the
 * ordinary `xt_get_element` / `xt_set_element`, so semantics are exactly the
 * runtime's. Two properties make the direct field reads safe:
 *
 *   - the object pointer is reloaded from the value on every access, so an
 *     array whose `items` was reallocated by `push` is seen through its current
 *     header (no pointer is cached across mutations);
 *   - nothing allocates between the field load and the element load, so the
 *     collector cannot run in between; it never moves objects, and the receiver
 *     is a live value the conservative stack scan already roots.
 */

import type { Generator } from "../generator.js";
import { XT_INLINE_TAGS, XT_LAYOUT } from "../layout.js";
import { i64 } from "../../values.js";

/** Kinds inlined on the read path, in test order (most common first). */
const INLINE_READ_KINDS = [
  { kind: 7, width: 4 }, // Float32Array
  { kind: 8, width: 8 }, // Float64Array
  { kind: 0, width: 1 }, // Uint8Array
  { kind: 3, width: 2 }, // Uint16Array
  { kind: 5, width: 4 }, // Uint32Array
] as const;

export interface ElementInlineMethods {
  /** Read `object[key]` with the inline fast path. */
  emitElementRead(this: Generator, objectValue: string, keyValue: string): string;
  /** Write `object[key] = value` with the inline fast path. */
  emitElementWrite(this: Generator, objectValue: string, keyValue: string, value: string): void;
}

/* -- small IR helpers ------------------------------------------------------ */

function reg(self: Generator): string {
  return self.reg();
}

function emit(self: Generator, line: string): void {
  self.emit(line);
}

/** `value` is a number (not undefined/null/string/object/...). */
function emitIsNumber(self: Generator, value: string): string {
  const masked = reg(self);
  emit(self, `  ${masked} = and i64 ${value}, ${i64(XT_INLINE_TAGS.numberMask)}`);
  const isNumber = reg(self);
  emit(self, `  ${isNumber} = icmp ne i64 ${masked}, ${i64(XT_INLINE_TAGS.numberMask)}`);
  return isNumber;
}

/** `value` carries `tag` in its high 16 bits. */
function emitHasTag(self: Generator, value: string, tag: bigint): string {
  const masked = reg(self);
  emit(self, `  ${masked} = and i64 ${value}, ${i64(XT_INLINE_TAGS.tagMask)}`);
  const matches = reg(self);
  emit(self, `  ${matches} = icmp eq i64 ${masked}, ${i64(tag)}`);
  return matches;
}

/** `value` is an integral array index in `0 .. 2^32-2`; safe to `fptosi`. */
function emitIsIndex(self: Generator, value: string): string {
  const asDouble = reg(self);
  emit(self, `  ${asDouble} = bitcast i64 ${value} to double`);
  const truncated = reg(self);
  emit(self, `  ${truncated} = call double @llvm.trunc.f64(double ${asDouble})`);
  const integral = reg(self);
  emit(self, `  ${integral} = fcmp oeq double ${truncated}, ${asDouble}`);
  const nonNegative = reg(self);
  emit(self, `  ${nonNegative} = fcmp oge double ${asDouble}, 0.000000e+00`);
  const bounded = reg(self);
  emit(self, `  ${bounded} = fcmp olt double ${asDouble}, 4.294967294000000e+09`);
  const partial = reg(self);
  emit(self, `  ${partial} = and i1 ${integral}, ${nonNegative}`);
  const isIndex = reg(self);
  emit(self, `  ${isIndex} = and i1 ${partial}, ${bounded}`);
  return isIndex;
}

/** Truncate a key already proven to be an index. */
function emitIndexOf(self: Generator, value: string): string {
  const asDouble = reg(self);
  emit(self, `  ${asDouble} = bitcast i64 ${value} to double`);
  const index = reg(self);
  emit(self, `  ${index} = fptosi double ${asDouble} to i64`);
  return index;
}

/** An `i8*` onto the heap payload of a tagged value. */
function emitPayload(self: Generator, value: string): string {
  const pointer = reg(self);
  emit(self, `  ${pointer} = and i64 ${value}, ${i64(0x0000ffffffffffffn)}`);
  const base = reg(self);
  emit(self, `  ${base} = inttoptr i64 ${pointer} to i8*`);
  return base;
}

function addressAt(self: Generator, base: string, offset: number): string {
  const address = reg(self);
  emit(self, `  ${address} = getelementptr i8, i8* ${base}, i64 ${offset}`);
  return address;
}

function loadU32(self: Generator, base: string, offset: number): string {
  const pointer = reg(self);
  emit(self, `  ${pointer} = bitcast i8* ${addressAt(self, base, offset)} to i32*`);
  const value = reg(self);
  emit(self, `  ${value} = load i32, i32* ${pointer}`);
  return value;
}

function loadI64(self: Generator, base: string, offset: number): string {
  const pointer = reg(self);
  emit(self, `  ${pointer} = bitcast i8* ${addressAt(self, base, offset)} to i64*`);
  const value = reg(self);
  emit(self, `  ${value} = load i64, i64* ${pointer}`);
  return value;
}

function loadI16(self: Generator, base: string, offset: number): string {
  const pointer = reg(self);
  emit(self, `  ${pointer} = bitcast i8* ${addressAt(self, base, offset)} to i16*`);
  const value = reg(self);
  emit(self, `  ${value} = load i16, i16* ${pointer}`);
  return value;
}

/** `items[index]` as an `i64*` slot of the array whose header starts at `base`. */
function arraySlot(self: Generator, base: string, index: string): string {
  const raw = loadI64(self, base, XT_LAYOUT.arrayItems);
  const items = reg(self);
  emit(self, `  ${items} = inttoptr i64 ${raw} to i64*`);
  const slot = reg(self);
  emit(self, `  ${slot} = getelementptr i64, i64* ${items}, i64 ${index}`);
  return slot;
}

/** `view_bytes + index * width`, plus the exclusive end of that element. */
function viewOffset(
  self: Generator,
  view: string,
  index: string,
  width: number,
): { address: string; end: string } {
  const base = reg(self);
  emit(self, `  ${base} = inttoptr i64 ${view} to i8*`);
  const offset = reg(self);
  emit(self, `  ${offset} = mul i64 ${index}, ${width}`);
  const address = reg(self);
  emit(self, `  ${address} = getelementptr i8, i8* ${base}, i64 ${offset}`);
  const end = reg(self);
  emit(self, `  ${end} = add i64 ${offset}, ${width}`);
  return { address, end };
}

/** Load one typed-array element and box it as a NaN-boxed double. */
function loadTypedElement(self: Generator, address: string, kind: number): string {
  const converted = reg(self);
  switch (kind) {
    case 7: {
      const pointer = reg(self);
      emit(self, `  ${pointer} = bitcast i8* ${address} to float*`);
      const raw = reg(self);
      emit(self, `  ${raw} = load float, float* ${pointer}`);
      emit(self, `  ${converted} = fpext float ${raw} to double`);
      break;
    }
    case 8: {
      const pointer = reg(self);
      emit(self, `  ${pointer} = bitcast i8* ${address} to double*`);
      emit(self, `  ${converted} = load double, double* ${pointer}`);
      break;
    }
    case 0: {
      const raw = reg(self);
      emit(self, `  ${raw} = load i8, i8* ${address}`);
      const wide = reg(self);
      emit(self, `  ${wide} = zext i8 ${raw} to i32`);
      emit(self, `  ${converted} = uitofp i32 ${wide} to double`);
      break;
    }
    case 3: {
      const pointer = reg(self);
      emit(self, `  ${pointer} = bitcast i8* ${address} to i16*`);
      const raw = reg(self);
      emit(self, `  ${raw} = load i16, i16* ${pointer}`);
      const wide = reg(self);
      emit(self, `  ${wide} = zext i16 ${raw} to i32`);
      emit(self, `  ${converted} = uitofp i32 ${wide} to double`);
      break;
    }
    default: {
      const pointer = reg(self);
      emit(self, `  ${pointer} = bitcast i8* ${address} to i32*`);
      const raw = reg(self);
      emit(self, `  ${raw} = load i32, i32* ${pointer}`);
      emit(self, `  ${converted} = uitofp i32 ${raw} to double`);
      break;
    }
  }
  const boxed = reg(self);
  emit(self, `  ${boxed} = bitcast double ${converted} to i64`);
  return boxed;
}

export const elementInlineMethods: ElementInlineMethods = {
  emitElementRead(objectValue: string, keyValue: string): string {
    const result = this.alloca();
    const done = this.label("elem.done");
    const generic = this.label("elem.generic");
    const numeric = emitIsNumber(this, keyValue);
    const indexPath = this.label("elem.index");
    this.terminate(`br i1 ${numeric}, label %${indexPath}, label %${generic}`);

    this.startBlock(indexPath);
    const isIndex = emitIsIndex(this, keyValue);
    const keyIndex = this.label("elem.index.ok");
    this.terminate(`br i1 ${isIndex}, label %${keyIndex}, label %${generic}`);
    this.startBlock(keyIndex);
    const index = emitIndexOf(this, keyValue);

    /* -- plain array ------------------------------------------------------- */
    const isArray = emitHasTag(this, objectValue, XT_INLINE_TAGS.array);
    const arrayHit = this.label("elem.array.hit");
    const notArray = this.label("elem.notarray");
    this.terminate(`br i1 ${isArray}, label %${arrayHit}, label %${notArray}`);
    this.startBlock(arrayHit);
    const base = emitPayload(this, objectValue);
    const length32 = loadU32(this, base, XT_LAYOUT.arrayLength);
    const length = reg(this);
    this.emit(`  ${length} = zext i32 ${length32} to i64`);
    const inBounds = reg(this);
    this.emit(`  ${inBounds} = icmp ult i64 ${index}, ${length}`);
    const arrayStore = this.label("elem.array.store");
    this.terminate(`br i1 ${inBounds}, label %${arrayStore}, label %${generic}`);
    this.startBlock(arrayStore);
    const slot = arraySlot(this, base, index);
    const loaded = reg(this);
    this.emit(`  ${loaded} = load i64, i64* ${slot}`);
    this.emit(`  store i64 ${loaded}, i64* ${result}`);
    this.terminate(`br label %${done}`);

    /* -- typed-array view -------------------------------------------------- */
    this.startBlock(notArray);
    const isObject = emitHasTag(this, objectValue, XT_INLINE_TAGS.object);
    const viewCheck = this.label("elem.view.check");
    this.terminate(`br i1 ${isObject}, label %${viewCheck}, label %${generic}`);
    this.startBlock(viewCheck);
    const viewBase = emitPayload(this, objectValue);
    const view = loadI64(this, viewBase, XT_LAYOUT.objectViewBytes);
    const hasView = reg(this);
    this.emit(`  ${hasView} = icmp ne i64 ${view}, 0`);
    const kindCheck = this.label("elem.view.kind");
    this.terminate(`br i1 ${hasView}, label %${kindCheck}, label %${generic}`);
    this.startBlock(kindCheck);
    const viewLength32 = loadU32(this, viewBase, XT_LAYOUT.objectViewLength);
    const viewLength = reg(this);
    this.emit(`  ${viewLength} = zext i32 ${viewLength32} to i64`);
    const kindRaw = loadI16(this, viewBase, XT_LAYOUT.objectTypedKind);
    const kindSet = reg(this);
    this.emit(`  ${kindSet} = icmp ne i16 ${kindRaw}, 0`);
    const kind = reg(this);
    this.emit(`  ${kind} = add i16 ${kindRaw}, -1`);
    const dispatch = this.label("elem.view.dispatch");
    this.terminate(`br i1 ${kindSet}, label %${dispatch}, label %${generic}`);

    this.startBlock(dispatch);
    for (const entry of INLINE_READ_KINDS) {
      const matches = reg(this);
      this.emit(`  ${matches} = icmp eq i16 ${kind}, ${entry.kind}`);
      const hit = this.label(`elem.k${entry.kind}`);
      const next = this.label("elem.nextkind");
      this.terminate(`br i1 ${matches}, label %${hit}, label %${next}`);
      this.startBlock(hit);
      const { address, end } = viewOffset(this, view, index, entry.width);
      const fits = reg(this);
      this.emit(`  ${fits} = icmp ule i64 ${end}, ${viewLength}`);
      const load = this.label("elem.load");
      this.terminate(`br i1 ${fits}, label %${load}, label %${generic}`);
      this.startBlock(load);
      const element = loadTypedElement(this, address, entry.kind);
      this.emit(`  store i64 ${element}, i64* ${result}`);
      this.terminate(`br label %${done}`);
      this.startBlock(next);
    }
    this.terminate(`br label %${generic}`);

    /* -- fallback ---------------------------------------------------------- */
    this.startBlock(generic);
    const fallback = reg(this);
    this.emit(`  ${fallback} = call i64 @xt_get_element(i64 ${objectValue}, i64 ${keyValue}, i64* null)`);
    this.emit(`  store i64 ${fallback}, i64* ${result}`);
    this.terminate(`br label %${done}`);
    this.startBlock(done);
    const merged = reg(this);
    this.emit(`  ${merged} = load i64, i64* ${result}`);
    return merged;
  },

  emitElementWrite(objectValue: string, keyValue: string, value: string): void {
    const done = this.label("setelem.done");
    const generic = this.label("setelem.generic");
    const numeric = emitIsNumber(this, keyValue);
    const indexPath = this.label("setelem.index");
    this.terminate(`br i1 ${numeric}, label %${indexPath}, label %${generic}`);

    this.startBlock(indexPath);
    const isIndex = emitIsIndex(this, keyValue);
    const keyIndex = this.label("setelem.index.ok");
    this.terminate(`br i1 ${isIndex}, label %${keyIndex}, label %${generic}`);
    this.startBlock(keyIndex);
    const index = emitIndexOf(this, keyValue);

    /* -- plain array: an in-range store never changes the length ----------- */
    const isArray = emitHasTag(this, objectValue, XT_INLINE_TAGS.array);
    const arrayHit = this.label("setelem.array.hit");
    const notArray = this.label("setelem.notarray");
    this.terminate(`br i1 ${isArray}, label %${arrayHit}, label %${notArray}`);
    this.startBlock(arrayHit);
    const base = emitPayload(this, objectValue);
    const length32 = loadU32(this, base, XT_LAYOUT.arrayLength);
    const length = reg(this);
    this.emit(`  ${length} = zext i32 ${length32} to i64`);
    const inBounds = reg(this);
    this.emit(`  ${inBounds} = icmp ult i64 ${index}, ${length}`);
    const arrayStore = this.label("setelem.array.store");
    this.terminate(`br i1 ${inBounds}, label %${arrayStore}, label %${generic}`);
    this.startBlock(arrayStore);
    const slot = arraySlot(this, base, index);
    this.emit(`  store i64 ${value}, i64* ${slot}`);
    this.terminate(`br label %${done}`);

    /* -- typed-array view: float kinds only (integer stores coerce) -------- */
    this.startBlock(notArray);
    const isObject = emitHasTag(this, objectValue, XT_INLINE_TAGS.object);
    const viewCheck = this.label("setelem.view.check");
    this.terminate(`br i1 ${isObject}, label %${viewCheck}, label %${generic}`);
    this.startBlock(viewCheck);
    const viewBase = emitPayload(this, objectValue);
    const view = loadI64(this, viewBase, XT_LAYOUT.objectViewBytes);
    const hasView = reg(this);
    this.emit(`  ${hasView} = icmp ne i64 ${view}, 0`);
    const kindCheck = this.label("setelem.kind");
    this.terminate(`br i1 ${hasView}, label %${kindCheck}, label %${generic}`);
    this.startBlock(kindCheck);
    const viewLength32 = loadU32(this, viewBase, XT_LAYOUT.objectViewLength);
    const viewLength = reg(this);
    this.emit(`  ${viewLength} = zext i32 ${viewLength32} to i64`);
    const kindRaw = loadI16(this, viewBase, XT_LAYOUT.objectTypedKind);
    const kind = reg(this);
    this.emit(`  ${kind} = add i16 ${kindRaw}, -1`);
    /* A non-number value needs the runtime's ToNumber conversion. */
    const valueIsNumber = emitIsNumber(this, value);
    const f32Test = this.label("setelem.f32.test");
    this.terminate(`br i1 ${valueIsNumber}, label %${f32Test}, label %${generic}`);

    this.startBlock(f32Test);
    const isF32 = reg(this);
    this.emit(`  ${isF32} = icmp eq i16 ${kind}, 7`);
    const f32Store = this.label("setelem.f32");
    const f64Test = this.label("setelem.f64.test");
    this.terminate(`br i1 ${isF32}, label %${f32Store}, label %${f64Test}`);

    this.startBlock(f32Store);
    {
      const { address, end } = viewOffset(this, view, index, 4);
      const fits = reg(this);
      this.emit(`  ${fits} = icmp ule i64 ${end}, ${viewLength}`);
      const ok = this.label("setelem.f32.ok");
      this.terminate(`br i1 ${fits}, label %${ok}, label %${generic}`);
      this.startBlock(ok);
      const pointer = reg(this);
      const wide = reg(this);
      const narrowed = reg(this);
      this.emit(`  ${pointer} = bitcast i8* ${address} to float*`);
      this.emit(`  ${wide} = bitcast i64 ${value} to double`);
      this.emit(`  ${narrowed} = fptrunc double ${wide} to float`);
      this.emit(`  store float ${narrowed}, float* ${pointer}`);
      this.terminate(`br label %${done}`);
    }

    this.startBlock(f64Test);
    const isF64 = reg(this);
    this.emit(`  ${isF64} = icmp eq i16 ${kind}, 8`);
    const f64Store = this.label("setelem.f64");
    this.terminate(`br i1 ${isF64}, label %${f64Store}, label %${generic}`);

    this.startBlock(f64Store);
    {
      const { address, end } = viewOffset(this, view, index, 8);
      const fits = reg(this);
      this.emit(`  ${fits} = icmp ule i64 ${end}, ${viewLength}`);
      const ok = this.label("setelem.f64.ok");
      this.terminate(`br i1 ${fits}, label %${ok}, label %${generic}`);
      this.startBlock(ok);
      const pointer = reg(this);
      const wide = reg(this);
      this.emit(`  ${pointer} = bitcast i8* ${address} to double*`);
      this.emit(`  ${wide} = bitcast i64 ${value} to double`);
      this.emit(`  store double ${wide}, double* ${pointer}`);
      this.terminate(`br label %${done}`);
    }

    this.startBlock(generic);
    this.emit(`  call i64 @xt_set_element(i64 ${objectValue}, i64 ${keyValue}, i64 ${value})`);
    this.terminate(`br label %${done}`);
    this.startBlock(done);
  },
};

