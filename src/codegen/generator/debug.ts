/**
 * DWARF/CodeView metadata for `xbintsc build --debug`.
 *
 * The generated IR declares a compile unit, one `!DISubprogram` per compiled
 * function and one cached `!DILocation` per statement position. With `-g`,
 * clang turns these into DWARF (Linux/macOS) or CodeView (Windows), so a native
 * debugger maps instructions back to the TypeScript source.
 *
 * Metadata is only emitted when debug is enabled: the nodes are large, and the
 * default IR must stay byte-identical for the self-hosting fixpoint.
 */

import type { Node } from "../../ast/nodes.js";
import type { FunctionInfo } from "../../binder/binder.js";
import { escapeBytes, utf8Bytes } from "./tables.js";
import type { Generator } from "./generator.js";

export interface DebugMethods {
  /** Allocate a numbered metadata node (`!0`, `!1`, …). */
  metadataId(this: Generator): string;
  /** Allocate a metadata node and register its definition. */
  addMetadata(this: Generator, definition: string): string;
  /** `!DIFile` for a source path, normalising separators like `source_filename`. */
  debugFileFor(this: Generator, fileName: string): string;
  /** `!DISubprogram` for a compiled function (created on first use). */
  debugSubprogramFor(this: Generator, fn: FunctionInfo): string | undefined;
  /**
   * Point subsequent instructions at `node`'s source position. Locations are
   * cached per function/line/column so a statement emits one metadata node
   * instead of one per instruction.
   */
  setDebugLocation(this: Generator, node: Node): void;
  /** Module-level metadata declarations and definitions (`--debug`). */
  debugMetadataLines(this: Generator): string[];
}

function escapeMetadata(text: string): string {
  return escapeBytes(utf8Bytes(text));
}

/**
 * Create the compile unit and module flags once. LLVM requires the `Debug Info
 * Version` flag for any `!dbg` attachment, and clang only emits DWARF/CodeView
 * for a module that declares a compile unit.
 */
function ensureDebugUnit(self: Generator): void {
  if (self.debugUnit !== undefined) return;
  const entryFile = self.debugFileFor(self.sourceFile.fileName);
  const unit = self.addMetadata(
    'distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: ' +
      `${entryFile}, producer: "xbintsc", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)`,
  );
  self.debugUnit = unit;
  self.debugFlags.push(
    self.addMetadata('!{i32 7, !"Dwarf Version", i32 4}'),
    self.addMetadata('!{i32 2, !"Debug Info Version", i32 3}'),
  );
  const types = self.addMetadata("!{null}");
  self.debugSubroutineType = self.addMetadata(`!DISubroutineType(types: ${types})`);
}

export const debugMethods: DebugMethods = {
  metadataId() {
    return `!${this.metadataCounter++}`;
  },

  addMetadata(definition) {
    const id = this.metadataId();
    this.metadata.push(`${id} = ${definition}`);
    return id;
  },

  debugFileFor(fileName) {
    const normalized = fileName.split("\\").join("/");
    const existing = this.debugFiles.get(normalized);
    if (existing) return existing;
    const slash = normalized.lastIndexOf("/");
    const directory = slash >= 0 ? normalized.slice(0, slash) : ".";
    const name = slash >= 0 ? normalized.slice(slash + 1) : normalized;
    const id = this.addMetadata(
      `!DIFile(filename: "${escapeMetadata(name)}", directory: "${escapeMetadata(directory)}")`,
    );
    this.debugFiles.set(normalized, id);
    return id;
  },

  debugSubprogramFor(fn) {
    if (!this.debug) return undefined;
    const existing = this.debugSubprograms.get(fn.id);
    if (existing) return existing;
    ensureDebugUnit(this);
    const file = this.debugFileFor(this.fileNameOf(fn.node));
    const position = this.positionOf(fn.node);
    const displayName = fn.isModule
      ? "module"
      : fn.isConstructor && fn.classInfo
        ? `new ${this.displayNameOf(fn.classInfo.name)}`
        : this.displayNameOf(fn.name.length > 0 ? fn.name : "anonymous");
    const subprogram = this.addMetadata(
      'distinct !DISubprogram(name: ' +
        `"${escapeMetadata(displayName)}", scope: ${this.debugUnit}, file: ${file}, ` +
        `line: ${position.line}, type: ${this.debugSubroutineType}, scopeLine: ${position.line}, ` +
        `spFlags: DISPFlagDefinition, unit: ${this.debugUnit})`,
    );
    this.debugSubprograms.set(fn.id, subprogram);
    return subprogram;
  },

  setDebugLocation(node) {
    if (!this.debug || this.current.debugSubprogram === undefined) return;
    const position = this.positionOf(node);
    const key = `${this.current.debugSubprogram}:${position.line}:${position.column}`;
    let location = this.debugLocations.get(key);
    if (!location) {
      location = this.addMetadata(
        `!DILocation(line: ${position.line}, column: ${position.column}, scope: ${this.current.debugSubprogram})`,
      );
      this.debugLocations.set(key, location);
    }
    this.current.debugLocation = location;
  },

  debugMetadataLines() {
    if (!this.debug || this.debugUnit === undefined) return [];
    return [
      `!llvm.dbg.cu = !{${this.debugUnit}}`,
      `!llvm.module.flags = !{${this.debugFlags.join(", ")}}`,
      ...this.metadata,
      "",
    ];
  },
};
