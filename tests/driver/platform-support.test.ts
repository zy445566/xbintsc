import { afterEach, describe, expect, it } from "vitest";
import {
  applyMacosDeploymentTarget,
  LINUX_MINIMUM_GLIBC,
  MACOS_MINIMUM_VERSION,
  supportFloor,
  WINDOWS_MINIMUM_VERSION,
} from "../../src/driver/platform-support.js";

const previousDeployment = process.env.MACOSX_DEPLOYMENT_TARGET;
const previousCmake = process.env.CMAKE_OSX_DEPLOYMENT_TARGET;

afterEach(() => {
  if (previousDeployment === undefined) delete process.env.MACOSX_DEPLOYMENT_TARGET;
  else process.env.MACOSX_DEPLOYMENT_TARGET = previousDeployment;
  if (previousCmake === undefined) delete process.env.CMAKE_OSX_DEPLOYMENT_TARGET;
  else process.env.CMAKE_OSX_DEPLOYMENT_TARGET = previousCmake;
});

describe("platform support floor", () => {
  it("names the documented floor for each supported host", () => {
    expect(supportFloor("darwin")).toBe(`macOS ${MACOS_MINIMUM_VERSION}+`);
    expect(supportFloor("win32")).toBe(`Windows ${WINDOWS_MINIMUM_VERSION}+`);
    expect(supportFloor("linux")).toBe(`glibc ${LINUX_MINIMUM_GLIBC}+`);
  });

  it("marks an untested host instead of claiming support", () => {
    expect(supportFloor("freebsd")).toContain("unverified");
  });

  it("sets the macOS deployment target only on darwin", () => {
    delete process.env.MACOSX_DEPLOYMENT_TARGET;
    delete process.env.CMAKE_OSX_DEPLOYMENT_TARGET;

    applyMacosDeploymentTarget("linux");
    expect(process.env.MACOSX_DEPLOYMENT_TARGET).toBeUndefined();

    applyMacosDeploymentTarget("darwin");
    expect(process.env.MACOSX_DEPLOYMENT_TARGET).toBe(MACOS_MINIMUM_VERSION);
    expect(process.env.CMAKE_OSX_DEPLOYMENT_TARGET).toBe(MACOS_MINIMUM_VERSION);
  });

  it("keeps an explicit deployment target", () => {
    process.env.MACOSX_DEPLOYMENT_TARGET = "10.15";
    process.env.CMAKE_OSX_DEPLOYMENT_TARGET = "10.15";
    applyMacosDeploymentTarget("darwin");
    expect(process.env.MACOSX_DEPLOYMENT_TARGET).toBe("10.15");
    expect(process.env.CMAKE_OSX_DEPLOYMENT_TARGET).toBe("10.15");
  });
});
