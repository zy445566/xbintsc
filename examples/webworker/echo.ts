// Web Worker API (`--ext webworker`): the worker script is this binary, so the
// program branches on `isMainThread`. The parent talks through `onmessage`, the
// worker through `self.onmessage` / `self.postMessage`.
if (isMainThread) {
  const worker = new Worker(import.meta.filename, { name: "echo" });
  worker.onmessage = (event) => {
    console.log("reply", event.data);
    worker.terminate();
  };
  worker.postMessage("hello worker");
} else {
  self.onmessage = (event) => {
    self.postMessage(String(event.data).toUpperCase());
  };
}

// run with
// `npx tsx src/cli/main.ts run examples/webworker/echo.ts --ext webworker`
