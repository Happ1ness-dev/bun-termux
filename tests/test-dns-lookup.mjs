import dns from "node:dns/promises";

async function isOnline() {
  return dns.lookup("github.com").then(
    () => true,
    () => false
  );
}

console.log("Testing internet connectivity...");
const online = await isOnline();
console.log("Online:", online);

if (!online) {
  console.error("FAIL: Unable to connect to the internet.");
  process.exit(1);
}

console.log("PASS: Internet connection verified!");
