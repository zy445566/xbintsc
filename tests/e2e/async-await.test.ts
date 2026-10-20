/**
 * End-to-end tests for *real* `async`/`await` suspension.
 *
 * An `async` function runs its body on a stackful coroutine. When it awaits a
 * promise that has not settled yet, the coroutine suspends and the event loop
 * (timers, sockets, ...) takes over; the promise's settlement resumes the
 * coroutine exactly where it stopped. These tests therefore depend on work that
 * can only complete after the program body returns.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";
import { makeDifferential } from "./differential-helpers.js";

const prelude = `
function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}
`;

describeE2E("end-to-end real async/await", (harness) => {
  const { runProgram } = harness;
  const { diff } = makeDifferential(harness);

  diff(
    "resumes an async function after a timer settles its promise",
    `${prelude}
const order: string[] = [];
async function main(): Promise<void> {
  order.push("start");
  await sleep(5);
  order.push("timer");
  const value = await new Promise<number>((resolve) => setTimeout(() => resolve(7), 1));
  order.push("value:" + value);
}
main().then(() => {
  order.push("done");
  console.log(order.join(","));
});
console.log("body");`,
  );

  it("keeps the program alive until an awaited timer resolves", () => {
    const source = `${prelude}
async function main(): Promise<void> {
  await sleep(10);
  console.log("after");
}
main();
console.log("before");`;
    expect(runProgram(source)).toBe("before\nafter");
  });

  it("runs independent async functions concurrently", () => {
    const source = `${prelude}
const order: string[] = [];
async function task(name: string, ms: number): Promise<void> {
  await sleep(ms);
  order.push(name);
}
async function main(): Promise<void> {
  await Promise.all([task("slow", 8), task("fast", 1)]);
  console.log(order.join(","));
}
main();`;
    expect(runProgram(source)).toBe("fast,slow");
  });

  it("rejects when an awaited timer promise rejects and is catchable", () => {
    const source = `${prelude}
function fail(ms: number): Promise<number> {
  return new Promise<number>((_resolve, reject) => setTimeout(() => reject("boom"), ms));
}
async function main(): Promise<void> {
  try {
    await fail(2);
    console.log("unreachable");
  } catch (error) {
    console.log("caught", error);
  }
  try {
    await fail(1);
  } finally {
    console.log("finally");
  }
}
main();`;
    expect(runProgram(source)).toBe("caught boom\nfinally");
  });

  it("rejects the returned promise when the body throws after resuming", () => {
    const source = `${prelude}
async function boom(): Promise<void> {
  await sleep(1);
  throw new Error("late");
}
boom().catch((error: any) => console.log("rejected", error.message));`;
    expect(runProgram(source)).toBe("rejected late");
  });

  it("suspends across loops, closures and nested async calls", () => {
    const source = `${prelude}
async function twice(n: number): Promise<number> {
  await sleep(1);
  return n * 2;
}
async function sum(limit: number): Promise<number> {
  let total = 0;
  for (let i = 0; i < limit; i++) {
    total += await twice(i);
  }
  return total;
}
async function main(): Promise<void> {
  const total = await sum(4);
  console.log("total", total);
}
main();`;
    expect(runProgram(source)).toBe("total 12");
  });

  it("resumes two coroutines waiting on the same promise", () => {
    const source = `${prelude}
async function main(): Promise<void> {
  const shared = new Promise<string>((resolve) => setTimeout(() => resolve("shared"), 3));
  const results = await Promise.all([
    (async () => "a:" + (await shared))(),
    (async () => "b:" + (await shared))(),
  ]);
  console.log(results.join(" "));
}
main();`;
    expect(runProgram(source)).toBe("a:shared b:shared");
  });

  it("awaits a promise resolved by a socket callback", () => {
    const source = `
import net from "node:net";

function once(socket: any, event: string): Promise<any> {
  return new Promise<any>((resolve) => socket.on(event, resolve));
}

async function main(): Promise<void> {
  const server = net.createServer((socket: any) => {
    socket.on("data", (data: any) => {
      socket.write("echo:" + data);
      socket.end();
    });
  });
  server.listen(0, () => {});
  await new Promise<void>((resolve) => server.on("listening", resolve));
  const client = net.connect(server.address().port, "127.0.0.1");
  client.write("ping");
  const data = await once(client, "data");
  await once(client, "end");
  server.close();
  console.log("client", data);
}
main();`;
    expect(runProgram(source, { extensions: true })).toBe("client echo:ping");
  });

  it("awaits fs/promises across an event-loop turn", () => {
    const base = `${JSON.stringify(harness.workdir)} + "/async-fs-" + Math.random().toString(36).slice(2)`;
    const source = `
import { promises as fsp } from "fs";

async function main(): Promise<void> {
  const base = ${base};
  await fsp.mkdir(base, { recursive: true });
  const order: string[] = [];
  setTimeout(() => order.push("timer"), 1);
  await fsp.writeFile(base + "/a.txt", "hello");
  order.push("write");
  const text = await fsp.readFile(base + "/a.txt", "utf8");
  order.push("read:" + text);
  await new Promise<void>((resolve) => setTimeout(resolve, 5));
  order.push("waited");
  console.log(order.join(","));
  await fsp.rm(base, { recursive: true, force: true });
}
main();`;
    expect(runProgram(source, { extensions: true })).toBe("write,read:hello,timer,waited");
  });

  it("keeps a pending await alive while unrelated timers fire", () => {
    const source = `${prelude}
const log: string[] = [];
async function main(): Promise<void> {
  setTimeout(() => log.push("early"), 1);
  await sleep(6);
  log.push("late");
  console.log(log.join(","));
}
main();`;
    expect(runProgram(source)).toBe("early,late");
  });

  it("runs async callbacks scheduled by then() and setTimeout()", () => {
    const source = `${prelude}
Promise.resolve().then(async () => {
  await sleep(1);
  console.log("then");
});
setTimeout(async () => {
  await sleep(1);
  console.log("timer");
}, 2);`;
    expect(runProgram(source)).toBe("then\ntimer");
  });

  it("awaits a top-level promise by blocking on the event loop", () => {
    const source = `${prelude}
await sleep(50);
console.log("top-level");`;
    expect(runProgram(source)).toBe("top-level");
  });

  it("keeps suspended coroutines reachable across collections", () => {
    const source = `${prelude}
async function worker(id: number): Promise<number> {
  let total = 0;
  for (let i = 0; i < 20; i++) {
    const junk: any[] = [];
    for (let j = 0; j < 10; j++) junk.push({ id, i, j, text: "x".repeat(40) + j });
    await sleep(i % 2);
    total += junk.length;
  }
  return total;
}
async function main(): Promise<void> {
  const results = await Promise.all([worker(1), worker(2)]);
  console.log("alive", results.join(","));
}
main();`;
    expect(runProgram(source, { env: { XT_GC_THRESHOLD: "1048576" }, timeout: 60000 })).toBe("alive 200,200");
  });
});
