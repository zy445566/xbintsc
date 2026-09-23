// tests.mjs
import assert from 'node:assert';
import test from 'node:test';

test('that 1 is equal 1', () => {
  assert.strictEqual(1, 1);
});

test('that throws as 1 is not equal 2', () => {
  // throws an exception because 1 != 2
  assert.strictEqual(1, 2);
});

// run with `node tests.mjs`