/**
 * The `Proxy` subset: an object target plus `get`, `set`, `has`,
 * `deleteProperty` and `ownKeys` traps, with everything else forwarded to the
 * target.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (Proxy)", (harness) => {
  const { runProgram } = harness;

  it("routes reads, writes, `in` and own keys through the handler", () => {
    const source = `
      const target: any = { name: "world" };
      const proxy: any = new Proxy(target, {
        get(object: any, property: string): any {
          if (property === "name") return "proxy";
          return object[property];
        },
        set(object: any, property: string, value: any): boolean {
          object["set_" + property] = value;
          return true;
        },
        has(_object: any, property: string): boolean {
          return property === "name";
        },
        ownKeys(): string[] {
          return ["name"];
        },
      });
      console.log("get", proxy.name);
      proxy.extra = 7;
      console.log("set", JSON.stringify(target));
      console.log("has", "name" in proxy, "extra" in proxy);
      console.log("keys", JSON.stringify(Object.keys(proxy)));
      console.log("typeof", typeof proxy);
    `;
    expect(runProgram(source)).toBe(
      [
        "get proxy",
        'set {"name":"world","set_extra":7}',
        "has true false",
        'keys ["name"]',
        "typeof object",
      ].join("\n"),
    );
  });

  it("forwards to the target when a trap is absent", () => {
    const source = `
      const target: any = { a: 1 };
      const proxy: any = new Proxy(target, {});
      console.log("read", proxy.a, "has", "a" in proxy);
      proxy.b = 2;
      console.log("write", JSON.stringify(target), Object.keys(proxy).join(","));
      delete proxy.a;
      console.log("delete", "a" in target);
      const filtered: any = new Proxy({ keep: 1, drop: 2 }, {
        ownKeys(): string[] { return ["keep"]; },
      });
      console.log("filtered", Object.keys(filtered).join(","));
    `;
    expect(runProgram(source)).toBe(
      [
        "read 1 has true",
        'write {"a":1,"b":2} a,b',
        "delete false",
        "filtered keep",
      ].join("\n"),
    );
  });
});
