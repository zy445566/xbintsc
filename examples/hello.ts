// A tour of the language subset xbintsc compiles today.
// Run it with: xbintsc run examples/hello.ts

// --- Functions: recursion, default and rest parameters ---------------------
function fib(n: number): number {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

function greet(name: string, times: number = 1): string {
  const parts: string[] = [];
  for (let i = 0; i < times; i += 1) {
    parts.push(`Hello, ${name}!`);
  }
  return parts.join(" ");
}

function sum(...nums: number[]): number {
  return nums.reduce((acc, n) => acc + n, 0);
}

console.log(greet("world", 2));
console.log(`fib(10) = ${fib(10)}, sum(1..5) = ${sum(1, 2, 3, 4, 5)}`);

// --- Arrays and higher-order functions -------------------------------------
const numbers = [5, 3, 8, 1, 9, 2];
const evens = numbers.filter((n) => n % 2 === 0);
const doubled = numbers.map((n) => n * 2);
const sorted = numbers.slice().sort((a, b) => a - b);
console.log("evens   =", evens.join(", "));
console.log("doubled =", doubled.join(", "));
console.log("sorted  =", sorted.join(", "));

// --- Objects, spread and Object helpers ------------------------------------
const user = { name: "ada", age: 36 };
const profile = { ...user, roles: ["math", "code"] };
console.log(`${profile.name} (${profile.age}) roles=${profile.roles.join("+")}`);
console.log("keys:", Object.keys(profile).join(", "));
for (const key in user) {
  console.log(`  for...in ${key} = ${user[key]}`);
}

// --- Closures capture variables by reference -------------------------------
function makeCounter(start: number = 0) {
  let count = start;
  return {
    inc: () => (count += 1),
    value: () => count,
  };
}
const counter = makeCounter(10);
counter.inc();
counter.inc();
console.log("counter =", counter.value());

// --- Classes: fields, methods, statics, inheritance, instanceof -------------
class Animal {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
  speak(): string {
    return `${this.name} makes a sound`;
  }
  static kind(): string {
    return "animal";
  }
}

class Dog extends Animal {
  constructor(name: string) {
    super(name);
  }
  speak(): string {
    return `${super.speak()} (woof)`;
  }
}

const rex = new Dog("Rex");
console.log(rex.speak());
console.log("instanceof:", rex instanceof Dog, rex instanceof Animal, Animal.kind());

// --- switch ----------------------------------------------------------------
function size(n: number): string {
  switch (n) {
    case 0:
      return "none";
    case 1:
    case 2:
      return "few";
    default:
      return "many";
  }
}
console.log("size:", size(0), size(2), size(9));

// --- try / catch / finally -------------------------------------------------
function risky(n: number): number {
  let result = 0;
  try {
    if (n < 0) throw "negative input";
    result = n * n;
  } catch (err) {
    console.log("  caught:", err);
    result = -1;
  } finally {
    console.log("  cleanup for", n);
  }
  return result;
}
console.log("risky(4) =", risky(4));
console.log("risky(-1) =", risky(-1));

// --- Optional chaining and nullish coalescing ------------------------------
const maybe: any = {};
console.log("optional:", maybe?.nested?.value, maybe.nested ?? "fallback");

// --- Map / Set / JSON ------------------------------------------------------
const scores = new Map<string, number>();
scores.set("ada", 98);
scores.set("bob", 87);
const uniq = new Set<number>();
uniq.add(3);
uniq.add(1);
uniq.add(3);
console.log("map size =", scores.size, "ada =", scores.get("ada"));
console.log("set size =", uniq.size, "has 1 =", uniq.has(1));
console.log("json =", JSON.stringify({ name: user.name, score: scores.get("ada") }));
console.log("parsed =", JSON.parse('{"ok":true,"n":7}').n);

// --- async / await and Promise ---------------------------------------------
async function compute(n: number): Promise<number> {
  return n * 10;
}

async function main(): Promise<void> {
  const value = await compute(42);
  console.log("awaited =", value);
  const all = await Promise.all([compute(1), compute(2)]);
  console.log("Promise.all =", all.join(","));
}

main();
