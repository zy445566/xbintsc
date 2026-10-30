/**
 * Resolving optional link features for one build.
 *
 * The registry knows which features the *enabled* extensions can provide; this
 * module decides which of them a program may use (from {@link ProgramUsage}),
 * applies the `--feature` / `--no-feature` overrides, and turns an unavailable
 * archive into either a warning (auto-detected) or an error (explicitly forced).
 *
 * Keeping the decision here — not in codegen — means the produced LLVM IR is
 * independent of the feature set: the only difference is which registration
 * objects and archives reach the linker.
 */

import { DiagnosticCode, type DiagnosticBag } from "../diagnostics/diagnostic.js";
import type { ExtensionRegistry } from "../extensions/registry.js";
import type { LinkFeature, ProgramUsage } from "../extensions/link-feature.js";

/** The user's explicit feature overrides (`--feature` / `--no-feature`). */
export interface LinkFeatureRequest {
  readonly features?: readonly string[];
  readonly noFeatures?: readonly string[];
}

/** One active feature, with everything the driver needs to link it. */
export interface ResolvedLinkFeature {
  readonly name: string;
  readonly description: string;
  readonly runtimeSources: readonly string[];
  readonly nativeObjects: readonly string[];
  readonly linkerFlags: readonly string[];
}

/** `--feature all` / `--no-feature all`; specific names override it. */
const ALL = "all";

/** Human-readable list of the features the enabled extensions provide. */
function availableList(names: readonly string[]): string {
  if (names.length === 0) return "none (enable an extension that provides one, e.g. --ext gui)";
  return names.join(", ");
}

/**
 * Pick the active features for `usage`.
 *
 * Precedence: a specific `--feature x` beats `--no-feature all`, and a specific
 * `--no-feature x` beats `--feature all`; the same name on both sides is an
 * error. Diagnosed errors are left in `diagnostics`; the caller must stop when
 * it has errors.
 */
export function resolveLinkFeatures(
  registry: ExtensionRegistry,
  usage: ProgramUsage,
  request: LinkFeatureRequest,
  diagnostics: DiagnosticBag,
): readonly ResolvedLinkFeature[] {
  const declarations = registry.linkFeatures();
  const byName = new Map<string, LinkFeature>();
  for (const declaration of declarations) byName.set(declaration.feature.name, declaration.feature);
  const available = [...byName.keys()].sort();
  const requested = new Set(request.features ?? []);
  const disabled = new Set(request.noFeatures ?? []);

  for (const name of [...requested, ...disabled]) {
    if (name === ALL || byName.has(name)) continue;
    diagnostics.error(
      DiagnosticCode.LinkFeatureUnavailable,
      `unknown link feature '${name}'; available: ${availableList(available)}`,
    );
  }
  if (requested.has(ALL) && disabled.has(ALL)) {
    diagnostics.error(
      DiagnosticCode.LinkFeatureUnavailable,
      "link features cannot be both forced and disabled with 'all' (--feature all --no-feature all)",
    );
  }
  if (diagnostics.hasErrors) return [];

  const forceAll = requested.has(ALL);
  const disableAll = disabled.has(ALL);
  const resolved: ResolvedLinkFeature[] = [];

  for (const { feature } of declarations) {
    const specificForced = requested.has(feature.name);
    const specificBlocked = disabled.has(feature.name);
    if (specificForced && specificBlocked) {
      diagnostics.error(
        DiagnosticCode.LinkFeatureUnavailable,
        `link feature '${feature.name}' is both forced (--feature) and disabled (--no-feature)`,
      );
      continue;
    }
    const forced = specificForced || (forceAll && !specificBlocked);
    const blocked = specificBlocked || (disableAll && !specificForced);
    if (!forced && blocked) continue;
    if (!forced && !feature.used(usage)) continue;

    let runtimeSources: readonly string[];
    let nativeObjects: readonly string[];
    try {
      runtimeSources = feature.runtimeSources?.() ?? [];
      nativeObjects = feature.nativeObjects?.() ?? [];
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      if (forced) {
        diagnostics.error(DiagnosticCode.LinkFeatureUnavailable, message);
      } else {
        diagnostics.warning(
          DiagnosticCode.LinkFeatureUnavailable,
          `link feature '${feature.name}' was detected but is unavailable: ${message}`,
        );
      }
      continue;
    }

    resolved.push({
      name: feature.name,
      description: feature.description,
      runtimeSources: [...runtimeSources],
      nativeObjects: [...nativeObjects],
      linkerFlags: [...(feature.linkerFlags?.() ?? [])],
    });
  }

  return resolved;
}
