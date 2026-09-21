/**
 * Statement lowering: aggregates the dispatch, variable, control-flow and loop
 * method groups into the `StatementMethods` mixin consumed by the generator.
 *
 * The implementation is split by functional area under `statements/`:
 *   - `dispatch`      — statement-list walking and kind routing
 *   - `variables`     — declarations, bindings, defaults and enums
 *   - `control-flow`  — `if`, `switch`, `try`/`catch`/`finally`, `return`
 *   - `loops`         — `while`, `do`, `for`, `for`/`of`, `for`/`in`
 */

import { statementDispatchMethods, type StatementDispatchMethods } from "./statements/dispatch.js";
import { variableStatementMethods, type VariableStatementMethods } from "./statements/variables.js";
import { controlFlowStatementMethods, type ControlFlowStatementMethods } from "./statements/control-flow.js";
import { loopStatementMethods, type LoopStatementMethods } from "./statements/loops.js";

export interface StatementMethods
  extends StatementDispatchMethods,
    VariableStatementMethods,
    ControlFlowStatementMethods,
    LoopStatementMethods {}

export const statementMethods: StatementMethods = Object.assign(
  {},
  statementDispatchMethods,
  variableStatementMethods,
  controlFlowStatementMethods,
  loopStatementMethods,
);
