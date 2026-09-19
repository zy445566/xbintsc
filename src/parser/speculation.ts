/**
 * Thrown internally to unwind a speculative parse; never escapes the parser.
 */
export class SpeculationError extends Error {
  constructor() {
    super("speculation failed");
    this.name = "SpeculationError";
  }
}
