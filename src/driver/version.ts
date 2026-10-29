/**
 * The compiler's own version.
 *
 * It is part of the incremental cache key, so it must be bumped for every
 * release (a test enforces that it matches `package.json`); a stale version
 * would let an upgraded compiler reuse binaries produced by the previous one.
 *
 * Keep this constant in its own module: the pre-commit hook bumps it on
 * `main`/`master` only (see `.githooks/pre-commit`), so feature branches never
 * change this line and merging two of them cannot conflict here.
 */
export const COMPILER_VERSION = "0.5.44";
