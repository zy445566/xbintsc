/**
 * Shared types for the source-level module bundler.
 */

import type { SourceFileNode } from "../../ast/nodes.js";
import type { BindResult } from "../../binder/binder.js";

export interface ModuleRecord {
  readonly path: string;
  readonly text: string;
  readonly sourceFile: SourceFileNode;
  readonly bind: BindResult;
  readonly prefix: string;
  /** exported name -> final (renamed) local name */
  readonly exports: Map<string, string>;
  /** original local name of every top-level symbol */
  readonly originalNames: Map<number, string>;
  /** final name of every top-level symbol */
  readonly finalNames: Map<number, string>;
}

export interface BundleResult {
  readonly sourceFile: SourceFileNode;
  readonly text: string;
  readonly moduleCount: number;
}

export interface PackageJson {
  readonly main?: string;
  readonly module?: string;
  readonly exports?: unknown;
}

export type DependencyResolution =
  | { readonly kind: "external" }
  | { readonly kind: "file"; readonly path: string }
  | { readonly kind: "missing" };
