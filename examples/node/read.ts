// crypto.mjs
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';

const hasher = createHash('sha1');

hasher.setEncoding('hex');
// ensure you have a `package.json` file for this test!
hasher.write(await readFile('package.json'));
hasher.end();

const fileHash = hasher.read();
console.log(`fileHash=${fileHash}`)
// run with `node crypto.mjs`