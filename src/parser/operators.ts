/**
 * Maps lexer token kinds to the AST operator enums and assigns the appropriate
 * precedence to binary operators.
 */

import { TokenKind } from "../lexer/token.js";
import {
  AssignmentOperator,
  BinaryOperator,
  PrefixUnaryOperator,
} from "../ast/operators.js";

/** `as` / `satisfies` share the relational precedence level. */
export const AS_PRECEDENCE = 8;

export function assignmentOperator(kind: TokenKind): AssignmentOperator | undefined {
  switch (kind) {
    case TokenKind.Equals:
      return AssignmentOperator.Assign;
    case TokenKind.PlusEquals:
      return AssignmentOperator.AddAssign;
    case TokenKind.MinusEquals:
      return AssignmentOperator.SubtractAssign;
    case TokenKind.AsteriskEquals:
      return AssignmentOperator.MultiplyAssign;
    case TokenKind.SlashEquals:
      return AssignmentOperator.DivideAssign;
    case TokenKind.PercentEquals:
      return AssignmentOperator.RemainderAssign;
    case TokenKind.AsteriskAsteriskEquals:
      return AssignmentOperator.ExponentAssign;
    case TokenKind.LessThanLessThanEquals:
      return AssignmentOperator.LessThanLessThanAssign;
    case TokenKind.GreaterThanGreaterThanEquals:
      return AssignmentOperator.GreaterThanGreaterThanAssign;
    case TokenKind.GreaterThanGreaterThanGreaterThanEquals:
      return AssignmentOperator.GreaterThanGreaterThanGreaterThanAssign;
    case TokenKind.AmpersandEquals:
      return AssignmentOperator.AmpersandAssign;
    case TokenKind.BarEquals:
      return AssignmentOperator.BarAssign;
    case TokenKind.CaretEquals:
      return AssignmentOperator.CaretAssign;
    case TokenKind.AmpersandAmpersandEquals:
      return AssignmentOperator.AmpersandAmpersandAssign;
    case TokenKind.BarBarEquals:
      return AssignmentOperator.BarBarAssign;
    case TokenKind.QuestionQuestionEquals:
      return AssignmentOperator.QuestionQuestionAssign;
    default:
      return undefined;
  }
}

export function binaryOperator(kind: TokenKind): BinaryOperator | undefined {
  switch (kind) {
    case TokenKind.Plus:
      return BinaryOperator.Add;
    case TokenKind.Minus:
      return BinaryOperator.Subtract;
    case TokenKind.Asterisk:
      return BinaryOperator.Multiply;
    case TokenKind.Slash:
      return BinaryOperator.Divide;
    case TokenKind.Percent:
      return BinaryOperator.Remainder;
    case TokenKind.AsteriskAsterisk:
      return BinaryOperator.Exponent;
    case TokenKind.LessThan:
      return BinaryOperator.LessThan;
    case TokenKind.LessThanEquals:
      return BinaryOperator.LessThanEquals;
    case TokenKind.GreaterThan:
      return BinaryOperator.GreaterThan;
    case TokenKind.GreaterThanEquals:
      return BinaryOperator.GreaterThanEquals;
    case TokenKind.EqualsEquals:
      return BinaryOperator.EqualsEquals;
    case TokenKind.ExclamationEquals:
      return BinaryOperator.ExclamationEquals;
    case TokenKind.EqualsEqualsEquals:
      return BinaryOperator.EqualsEqualsEquals;
    case TokenKind.ExclamationEqualsEquals:
      return BinaryOperator.ExclamationEqualsEquals;
    case TokenKind.AmpersandAmpersand:
      return BinaryOperator.AmpersandAmpersand;
    case TokenKind.BarBar:
      return BinaryOperator.BarBar;
    case TokenKind.QuestionQuestion:
      return BinaryOperator.QuestionQuestion;
    case TokenKind.Ampersand:
      return BinaryOperator.Ampersand;
    case TokenKind.Bar:
      return BinaryOperator.Bar;
    case TokenKind.Caret:
      return BinaryOperator.Caret;
    case TokenKind.LessThanLessThan:
      return BinaryOperator.LessThanLessThan;
    case TokenKind.GreaterThanGreaterThan:
      return BinaryOperator.GreaterThanGreaterThan;
    case TokenKind.GreaterThanGreaterThanGreaterThan:
      return BinaryOperator.GreaterThanGreaterThanGreaterThan;
    case TokenKind.InKeyword:
      return BinaryOperator.In;
    case TokenKind.InstanceOfKeyword:
      return BinaryOperator.InstanceOf;
    default:
      return undefined;
  }
}

export function binaryPrecedence(op: BinaryOperator): number {
  switch (op) {
    case BinaryOperator.QuestionQuestion:
      return 1;
    case BinaryOperator.BarBar:
      return 2;
    case BinaryOperator.AmpersandAmpersand:
      return 3;
    case BinaryOperator.Bar:
      return 4;
    case BinaryOperator.Caret:
      return 5;
    case BinaryOperator.Ampersand:
      return 6;
    case BinaryOperator.EqualsEquals:
    case BinaryOperator.ExclamationEquals:
    case BinaryOperator.EqualsEqualsEquals:
    case BinaryOperator.ExclamationEqualsEquals:
      return 7;
    case BinaryOperator.LessThan:
    case BinaryOperator.LessThanEquals:
    case BinaryOperator.GreaterThan:
    case BinaryOperator.GreaterThanEquals:
    case BinaryOperator.In:
    case BinaryOperator.InstanceOf:
      return 8;
    case BinaryOperator.LessThanLessThan:
    case BinaryOperator.GreaterThanGreaterThan:
    case BinaryOperator.GreaterThanGreaterThanGreaterThan:
      return 9;
    case BinaryOperator.Add:
    case BinaryOperator.Subtract:
      return 10;
    case BinaryOperator.Multiply:
    case BinaryOperator.Divide:
    case BinaryOperator.Remainder:
      return 11;
    case BinaryOperator.Exponent:
      return 12;
    default:
      return -1;
  }
}

export function prefixUnaryOperator(kind: TokenKind): PrefixUnaryOperator | undefined {
  switch (kind) {
    case TokenKind.Plus:
      return PrefixUnaryOperator.Plus;
    case TokenKind.Minus:
      return PrefixUnaryOperator.Minus;
    case TokenKind.Tilde:
      return PrefixUnaryOperator.Tilde;
    case TokenKind.Exclamation:
      return PrefixUnaryOperator.Exclamation;
    case TokenKind.TypeOfKeyword:
      return PrefixUnaryOperator.TypeOf;
    case TokenKind.VoidKeyword:
      return PrefixUnaryOperator.Void;
    case TokenKind.DeleteKeyword:
      return PrefixUnaryOperator.Delete;
    case TokenKind.PlusPlus:
      return PrefixUnaryOperator.PlusPlus;
    case TokenKind.MinusMinus:
      return PrefixUnaryOperator.MinusMinus;
    case TokenKind.AwaitKeyword:
      return PrefixUnaryOperator.Await;
    default:
      return undefined;
  }
}
