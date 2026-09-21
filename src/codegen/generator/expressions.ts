/**
 * Expression lowering: aggregates the primary, operator and assignment method
 * groups into the `ExpressionMethods` mixin consumed by the generator.
 *
 * The implementation is split by functional area under `expressions/`:
 *   - `primary`     — dispatcher, identifiers, `this`/`new`/`await`, templates
 *   - `operators`   — binary/short-circuit/conditional/unary operators
 *   - `assignment`  — assignments and destructuring targets
 */

import { primaryExpressionMethods, type PrimaryExpressionMethods } from "./expressions/primary.js";
import { operatorExpressionMethods, type OperatorExpressionMethods } from "./expressions/operators.js";
import { assignmentExpressionMethods, type AssignmentExpressionMethods } from "./expressions/assignment.js";

export interface ExpressionMethods
  extends PrimaryExpressionMethods,
    OperatorExpressionMethods,
    AssignmentExpressionMethods {}

export const expressionMethods: ExpressionMethods = Object.assign(
  {},
  primaryExpressionMethods,
  operatorExpressionMethods,
  assignmentExpressionMethods,
);
