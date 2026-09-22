// @ts-check
/**
 * ESLint flat config.
 *
 * The code base is strict TypeScript (no `any`), so the config layers the
 * recommended JS + typescript-eslint rule sets and tightens a few rules that
 * matter for a compiler (unused variables, explicit `any`, `const`).
 */
import eslint from "@eslint/js";
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
      "examples/**",
      "**/*.d.ts",
      "**/*.js",
    ],
  },
  eslint.configs.recommended,
  ...tseslint.configs.recommended,
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
