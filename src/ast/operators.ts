/**
 * Operator taxonomies shared by the parser and code generator.
 */

export const enum PrefixUnaryOperator {
  Plus = "+",
  Minus = "-",
  Tilde = "~",
  Exclamation = "!",
  TypeOf = "typeof",
  Void = "void",
  Delete = "delete",
  PlusPlus = "++",
  MinusMinus = "--",
  Await = "await",
}

export const enum PostfixUnaryOperator {
  PlusPlus = "++",
  MinusMinus = "--",
}

export const enum BinaryOperator {
  Add = "+",
  Subtract = "-",
  Multiply = "*",
  Divide = "/",
  Remainder = "%",
  Exponent = "**",
  LessThan = "<",
  LessThanEquals = "<=",
  GreaterThan = ">",
  GreaterThanEquals = ">=",
  EqualsEquals = "==",
  ExclamationEquals = "!=",
  EqualsEqualsEquals = "===",
  ExclamationEqualsEquals = "!==",
  AmpersandAmpersand = "&&",
  BarBar = "||",
  QuestionQuestion = "??",
  Ampersand = "&",
  Bar = "|",
  Caret = "^",
  LessThanLessThan = "<<",
  GreaterThanGreaterThan = ">>",
  GreaterThanGreaterThanGreaterThan = ">>>",
  Comma = ",",
  In = "in",
  InstanceOf = "instanceof",
}

/** Assignment operators, including the compound forms. */
export const enum AssignmentOperator {
  Assign = "=",
  AddAssign = "+=",
  SubtractAssign = "-=",
  MultiplyAssign = "*=",
  DivideAssign = "/=",
  RemainderAssign = "%=",
  ExponentAssign = "**=",
  LessThanLessThanAssign = "<<=",
  GreaterThanGreaterThanAssign = ">>=",
  GreaterThanGreaterThanGreaterThanAssign = ">>>=",
  AmpersandAssign = "&=",
  BarAssign = "|=",
  CaretAssign = "^=",
  AmpersandAmpersandAssign = "&&=",
  BarBarAssign = "||=",
  QuestionQuestionAssign = "??=",
}

export const enum TypeOperator {
  KeyOf = "keyof",
  Unique = "unique",
  Readonly = "readonly",
}
