/**
 * Call lowering: aggregates the invocation, access, literal, closure and super
 * method groups into the `CallMethods` mixin consumed by the generator.
 *
 * The implementation is split by functional area under `calls/`:
 *   - `invocation` — direct calls, built-ins, imports and argument marshalling
 *   - `access`     — property/element reads, `delete` and optional chaining
 *   - `literals`   — array and object literals
 *   - `closures`   — arrow/function expressions and captured environments
 *   - `super`      — parent constructor/method delegation
 */

import { invocationCallMethods, type InvocationCallMethods } from "./calls/invocation.js";
import { accessCallMethods, type AccessCallMethods } from "./calls/access.js";
import { literalCallMethods, type LiteralCallMethods } from "./calls/literals.js";
import { closureCallMethods, type ClosureCallMethods } from "./calls/closures.js";
import { superCallMethods, type SuperCallMethods } from "./calls/super.js";

export interface CallMethods
  extends InvocationCallMethods,
    AccessCallMethods,
    LiteralCallMethods,
    ClosureCallMethods,
    SuperCallMethods {}

export const callMethods: CallMethods = Object.assign(
  {},
  invocationCallMethods,
  accessCallMethods,
  literalCallMethods,
  closureCallMethods,
  superCallMethods,
);
