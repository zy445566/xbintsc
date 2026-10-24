/* eslint-disable @typescript-eslint/no-unused-vars -- the vendored cases call these globals */
/*
 * test262-compatible assertion harness for xbintsc.
 *
 * The test sources under `cases/` are unmodified test262 files. test262's own
 * `harness/assert.js` attaches the assertion helpers to the `assert` function
 * object; xbintsc currently materialises a fresh closure for every reference to
 * a function declaration, so expando properties on `assert` are not observable
 * (`assert.sameValue === undefined`). This harness provides the same public API
 * on `Function.prototype` instead, which resolves through the ordinary
 * prototype chain and keeps the test source untouched.
 *
 * Only the surface the vendored cases use is implemented. `$DONE` prints the
 * markers the runner looks for, so an async case cannot pass just because its
 * continuation never ran.
 */

function Test262Error(message) {
  this.message = message || "";
}

Test262Error.prototype.toString = function () {
  return "Test262Error: " + this.message;
};

function $isSameValue(a, b) {
  if (a === b) return a !== 0 || 1 / a === 1 / b;
  return a !== a && b !== b;
}

function $format(value) {
  if (typeof value === "string") return JSON.stringify(value);
  if (typeof value === "bigint") return String(value) + "n";
  if (value === null) return "null";
  if (value === undefined) return "undefined";
  if (typeof value === "symbol") return value.toString();
  return String(value);
}

function assert(condition, message) {
  if (condition === true) return;
  throw new Test262Error(
    (message ? message + " " : "") + "Expected true but got " + $format(condition),
  );
}

Function.prototype.sameValue = function (actual, expected, message) {
  if ($isSameValue(actual, expected)) return;
  throw new Test262Error(
    (message ? message + " " : "") +
      "Expected SameValue(" + $format(actual) + ", " + $format(expected) + ")"
  );
};

Function.prototype.notSameValue = function (actual, unexpected, message) {
  if (!$isSameValue(actual, unexpected)) return;
  throw new Test262Error(
    (message ? message + " " : "") + "Expected not SameValue(" + $format(actual) + ")"
  );
};

Function.prototype.throws = function (expected, fn, message) {
  if (typeof fn !== "function") {
    throw new Test262Error("assert.throws requires a function");
  }
  var prefix = message ? message + " " : "";
  try {
    fn();
  } catch (error) {
    if (error instanceof expected) return;
    throw new Test262Error(
      prefix + "Expected " + expected.name + " but got " + $format(error)
    );
  }
  throw new Test262Error(prefix + "Expected " + expected.name + " but nothing was thrown");
};

Function.prototype.compareArray = function (actual, expected, message) {
  if (!Array.isArray(actual)) {
    throw new Test262Error((message ? message + " " : "") + "Actual value is not an array");
  }
  if (actual.length !== expected.length) {
    throw new Test262Error(
      (message ? message + " " : "") +
        "Array lengths differ: " + actual.length + " !== " + expected.length
    );
  }
  for (var i = 0; i < expected.length; i++) {
    if (!$isSameValue(actual[i], expected[i])) {
      throw new Test262Error(
        (message ? message + " " : "") +
          "Array elements differ at " + i + ": " + $format(actual[i]) + " !== " + $format(expected[i])
      );
    }
  }
};

function $DONE(error) {
  if (error) {
    console.log("Test262:AsyncTestFailure");
    throw error;
  }
  console.log("Test262:AsyncTestComplete");
}
