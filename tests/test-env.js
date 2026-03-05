#!/usr/bin/env bun
/**
 * Environment variable test for bun-termux
 * 
 * This script tests that environment variables are properly passed through
 * the wrapper to Bun. Can be used standalone or with 'bun build --compile'.
 * 
 * Expected env var: BUN_TERMUX_TEST_VAR
 * Set it to any value before running this script.
 */

const TEST_VAR_NAME = "BUN_TERMUX_TEST_VAR";

const value = process.env[TEST_VAR_NAME];

if (value === undefined) {
  console.error(`FAIL: Environment variable ${TEST_VAR_NAME} is not set`);
  console.error("   Set it before running: export BUN_TERMUX_TEST_VAR=hello");
  process.exit(1);
}

console.log(`PASS: ${TEST_VAR_NAME}=${value}`);
console.log("   Environment variables are working correctly through the wrapper!");
process.exit(0);
