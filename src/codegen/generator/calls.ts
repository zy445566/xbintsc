/**
 * Call lowering: aggregates the invocation, access, literal, closure and super
 * method groups into the `CallMethods` mixin consumed by the generator.
 *
 * The implementation is split by functional area under `calls/`:
 *   - `invocation` — direct calls, built-ins, imports and argument marshalling
 *   - `access`     — property/element reads, `delete` and optional chaining
 *   - `element-inline` — the inlined `obj[key]` fast path (plain arrays and typed-array views)
 *   - `literals`   — array and object literals
 *   - `closures`   — arrow/function expressions and captured environments
 *   - `super`      — parent constructor/method delegation
 */

import { invocationCallMethods, type InvocationCallMethods } from "./calls/invocation.js";
import { accessCallMethods, type AccessCallMethods } from "./calls/access.js";
import { elementInlineMethods, type ElementInlineMethods } from "./calls/element-inline.js";
import { literalCallMethods, type LiteralCallMethods } from "./calls/literals.js";
import { closureCallMethods, type ClosureCallMethods } from "./calls/closures.js";
import { superCallMethods, type SuperCallMethods } from "./calls/super.js";

export interface CallMethods
  extends InvocationCallMethods,
    AccessCallMethods,
    ElementInlineMethods,
    LiteralCallMethods,
    ClosureCallMethods,
    SuperCallMethods {}

export const callMethods: CallMethods = Object.assign(
  {},
  invocationCallMethods,
  accessCallMethods,
  elementInlineMethods,
  literalCallMethods,
  closureCallMethods,
  superCallMethods,
);

