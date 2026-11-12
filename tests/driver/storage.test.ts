import { describe, expect, it } from "vitest";
import { resolveStorage, storageSource } from "../../src/driver/storage.js";

describe("resolveStorage", () => {
  it("defaults to memory", () => {
    expect(resolveStorage(undefined)).toEqual({ backend: "memory", path: "" });
    expect(resolveStorage({ backend: "memory" })).toEqual({ backend: "memory", path: "" });
  });

  it("keeps an explicit file backend and its path", () => {
    expect(resolveStorage({ backend: "file", path: "/tmp/app.db" })).toEqual({
      backend: "file",
      path: "/tmp/app.db",
    });
  });
});

describe("storageSource", () => {
  it("embeds a memory backend with an empty path", () => {
    const source = storageSource({ backend: "memory", path: "" });
    expect(source).toContain("xt_storage_backend = 0u");
    expect(source).toContain("xt_storage_path_len = 0ull");
    expect(source).toContain("0x00,");
  });

  it("embeds a file backend with the UTF-8 path bytes and a trailing NUL", () => {
    const source = storageSource({ backend: "file", path: "/tmp/app.db" });
    expect(source).toContain("xt_storage_backend = 1u");
    expect(source).toContain("xt_storage_path_len = 11ull");
    expect(source).toContain("0x2f, 0x74, 0x6d, 0x70, 0x2f, 0x61, 0x70, 0x70, 0x2e, 0x64, 0x62, 0x00,");
  });
});
