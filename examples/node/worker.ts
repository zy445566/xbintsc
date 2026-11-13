// worker_threads: the worker is the same executable re-run as a child process.
// It branches on `isMainThread`, and the two sides exchange structured-cloned
// values over the control channel.
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

if (isMainThread) {
  const data = "some data";
  const worker = new Worker(import.meta.filename, { workerData: data });
  worker.on("message", (msg) => {
    console.log("Reply from Thread:", msg);
    worker.terminate();
  });
  // The worker answers the first message and then waits for more.
  worker.postMessage("uppercase please");
} else {
  parentPort.on("message", () => {
    parentPort.postMessage(btoa(String(workerData).toUpperCase()));
  });
}

// run with `node --import tsx examples/node/worker.ts` or
// `npx tsx src/cli/main.ts run examples/node/worker.ts --ext node`
