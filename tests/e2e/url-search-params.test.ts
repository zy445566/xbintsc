/**
 * URLSearchParams: parse, mutate and serialize an
 * application/x-www-form-urlencoded query string.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("URLSearchParams", (harness) => {
  it("parses, mutates and serializes query strings like Node", () => {
    harness.expectSameOutputAsNode(`
      const params = new URLSearchParams("a=1&b=hello+world&b=2");
      console.log(params.get("a"), params.getAll("b").join(","), params.has("c"), params.toString());

      params.append("c", "x y");
      params.set("a", "9");
      params.delete("b");
      console.log(params.toString(), params.size);
      params.sort();
      console.log(params.toString());

      console.log(new URLSearchParams("q=%E4%B8%AD%E6%96%87").get("q"));
      console.log(new URLSearchParams([["x", "1"], ["x", "2"]]).toString());
      console.log(Array.from(params.keys()).join(","), Array.from(params.values()).join(","));

      const collected: string[] = [];
      params.forEach((value: string, key: string) => collected.push(key + "=" + value));
      console.log(collected.join("&"));
    `);
  });

  it("is iterable and feeds Object.fromEntries, spread and for...of", () => {
    /* The Saltline page reads its query string with
     * `Object.fromEntries(new URLSearchParams(location.search))`. The params
     * object had no `[Symbol.iterator]` and `fromEntries` only accepted an
     * array, so that expression silently produced `{}` and every query switch
     * (`?show=`, `?q=`, …) was ignored. */
    harness.expectSameOutputAsNode(`
      const params = new URLSearchParams("?show=render&q=low&x=1");
      console.log(JSON.stringify(Object.fromEntries(params)));
      console.log(JSON.stringify([...params]));
      console.log(JSON.stringify(Array.from(params)));
      console.log(JSON.stringify(Object.fromEntries([["b", 2]])));
      console.log(JSON.stringify(Object.fromEntries(new Map([["a", 1]]))));
      const seen: string[] = [];
      for (const [key, value] of params) seen.push(key + "=" + value);
      console.log(seen.join("&"));
      console.log(JSON.stringify(Object.fromEntries([[undefined, "u"]])));
    `);
  });
});
