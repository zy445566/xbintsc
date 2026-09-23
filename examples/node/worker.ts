// threads.mjs
import { Worker, isMainThread,
  workerData, parentPort } from 'node:worker_threads';

if (isMainThread) {
  const data = 'some data';
  const worker = new Worker(import.meta.filename, { workerData: data });
  worker.on('message', msg => console.log('Reply from Thread:', msg));
} else {
  const source = workerData;
  parentPort.postMessage(btoa(source.toUpperCase()));
}

// run with `node threads.mjs`