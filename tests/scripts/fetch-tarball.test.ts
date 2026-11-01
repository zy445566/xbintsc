import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { downloadTarball } from "../../scripts/fetch-tarball.js";

vi.mock("node:child_process", () => ({ spawnSync: vi.fn() }));

const spawn = vi.mocked(spawnSync);

const directories: string[] = [];

beforeEach(() => {
  spawn.mockReset();
});

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

/** Make the mocked curl write `content` to its `-o` target and report `status`. */
function respond(status: number, content = "tarball"): void {
  spawn.mockImplementationOnce((_command: string, args?: readonly string[]) => {
    const argv = args ?? [];
    const output = argv[argv.indexOf("-o") + 1]!;
    if (status === 0) writeFileSync(output, content);
    return { status } as unknown as ReturnType<typeof spawnSync>;
  });
}

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-fetch-"));
  directories.push(directory);
  return directory;
}

describe("downloadTarball", () => {
  it("writes the tarball through a .part file", () => {
    const destination = join(temporaryDirectory(), "ffmpeg.tar.xz");
    respond(0, "release");

    downloadTarball(destination, "https://example.invalid/ffmpeg.tar.xz", "FFmpeg");

    expect(readFileSync(destination, "utf8")).toBe("release");
    expect(existsSync(`${destination}.part`)).toBe(false);
    expect(spawn).toHaveBeenCalledTimes(1);
  });

  it("retries a transient TLS failure", () => {
    const destination = join(temporaryDirectory(), "ffmpeg.tar.xz");
    respond(35);
    respond(0, "release");

    downloadTarball(destination, "https://example.invalid/ffmpeg.tar.xz", "FFmpeg", { retryDelayMs: 0 });

    expect(spawn).toHaveBeenCalledTimes(2);
    expect(readFileSync(destination, "utf8")).toBe("release");
  });

  it("fails fast on a status a retry cannot fix", () => {
    const destination = join(temporaryDirectory(), "ffmpeg.tar.xz");
    respond(22); // curl: the server answered 404

    expect(() => downloadTarball(destination, "https://example.invalid/ffmpeg.tar.xz", "FFmpeg", { retryDelayMs: 0 }))
      .toThrow(/status 22/);

    expect(spawn).toHaveBeenCalledTimes(1);
    expect(existsSync(destination)).toBe(false);
    expect(existsSync(`${destination}.part`)).toBe(false);
  });

  it("gives up after the last attempt without leaving a partial file", () => {
    const destination = join(temporaryDirectory(), "ffmpeg.tar.xz");
    respond(35);
    respond(35);

    expect(() => downloadTarball(destination, "https://example.invalid/ffmpeg.tar.xz", "FFmpeg", {
      attempts: 2,
      retryDelayMs: 0,
    })).toThrow(/status 35/);

    expect(spawn).toHaveBeenCalledTimes(2);
    expect(existsSync(`${destination}.part`)).toBe(false);
  });
});
