import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    include: ["tests/**/*.test.ts"],
    environment: "node",
    testTimeout: 60_000,
    hookTimeout: 120_000,
    pool: "forks",
    reporters: ["default"],
    coverage: {
      provider: "v8",
      // Only the compiler's own sources count. Without an explicit `include`
      // the V8 provider also instruments vendored toolchain files, the bin
      // shim and build output (`coverage.all` defaults to true), which are not
      // ours and drag the totals down to a meaningless number.
      include: ["src/**/*.ts"],
      exclude: ["src/**/*.d.ts"],
      // High bar for a compiler: essentially every statement/function must be
      // exercised. Thresholds are global and sit a few points below the current
      // numbers so a single platform-specific branch on one of the CI runners
      // cannot flip an otherwise green run red.
      thresholds: {
        statements: 90,
        lines: 90,
        functions: 90,
        branches: 85,
      },
    },
  },
});
