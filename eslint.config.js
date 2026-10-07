// @ts-check
/**
 * ESLint flat config.
 *
 * The code base is strict TypeScript (no `any`), so the config layers the
 * recommended JS + typescript-eslint rule sets and tightens a few rules that
 * matter for a compiler (unused variables, explicit `any`, `const`).
 */
import eslint from "@eslint/js";
import globals from "globals";
import tseslint from "typescript-eslint";

export default tseslint.config(
  {
    ignores: [
      "build/**",
      "coverage/**",
      "dist/**",
      "node_modules/**",
      "runtime/**",
      "scratch/**",
      "vendor/**",
      "examples/**",
      "**/*.d.ts",
    ],
  },
  eslint.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ["**/*.js", "**/*.mjs", "**/*.cjs"],
    languageOptions: {
      globals: globals.node,
    },
  },
  {
    /* Applies to every linted code file (ts/tsx/js/mjs/cjs).
       Keep files focused: split by responsibility instead of growing a monolith. */
    rules: {
      "max-lines": ["error", { max: 600 }],
    },
  },
  {
    files: ["**/*.ts"],
    rules: {
      "@typescript-eslint/no-explicit-any": "error",
      "@typescript-eslint/no-unused-vars": [
        "error",
        { argsIgnorePattern: "^_", varsIgnorePattern: "^_", caughtErrorsIgnorePattern: "^_" },
      ],
      /* The generator/parser use the class + interface declaration-merging
         pattern to build one API out of several method groups. */
      "@typescript-eslint/no-unsafe-declaration-merging": "off",
      "prefer-const": "error",
      eqeqeq: ["error", "always", { null: "ignore" }],
    },
  },
);
