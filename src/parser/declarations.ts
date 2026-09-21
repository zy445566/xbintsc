/**
 * Declaration parsing: aggregates the function, class and type-level
 * declaration groups into the `DeclarationMethods` mixin consumed by the
 * parser.
 *
 * The implementation is split by functional area under `declarations/`:
 *   - `functions`    — function signatures, type parameters, parameters,
 *                      binding names and return types
 *   - `classes`      — class bodies, heritage clauses and members
 *   - `type-members` — interfaces, aliases, enums, namespaces, type members
 */

import {
  declarationFunctionMethods,
  type DeclarationFunctionMethods,
} from "./declarations/functions.js";
import {
  declarationClassMethods,
  type DeclarationClassMethods,
} from "./declarations/classes.js";
import {
  declarationTypeMethods,
  type DeclarationTypeMethods,
} from "./declarations/type-members.js";

export interface DeclarationMethods
  extends DeclarationFunctionMethods,
    DeclarationClassMethods,
    DeclarationTypeMethods {}

export const declarationMethods: DeclarationMethods = Object.assign(
  {},
  declarationFunctionMethods,
  declarationClassMethods,
  declarationTypeMethods,
);
