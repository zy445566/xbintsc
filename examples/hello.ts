// A small showcase of the language subset xbtsc compiles today.

function fib(n: number): number {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

function greet(name: string, times: number = 1): void {
  let i = 0;
  while (i < times) {
    console.log("Hello, " + name + "!");
    i += 1;
  }
}

const numbers = [1, 2, 3, 4, 5];
let total = 0;
for (const n of numbers) {
  total += n;
}

greet("world", 2);
console.log("fib(10) =", fib(10));
console.log("sum =", total);

const createCounter = () => {
  let count = 0;
  return () => {
    count += 1;
    return count;
  };
};
const next = createCounter();
console.log("counter:", next(), next(), next());
