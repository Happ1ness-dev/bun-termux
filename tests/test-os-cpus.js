const os = require('os');

console.log('Testing os.cpus()...');

try {
  const cpus = os.cpus();
  console.log('os.cpus() returned:', typeof cpus, Array.isArray(cpus) ? 'array' : 'not array');
  console.log('Length:', cpus.length);
  
  if (cpus.length > 0) {
    console.log('First CPU:', cpus[0]);
    console.log('Accessing model...');
    const model = cpus[0].model;
    console.log('Model:', model);
  }
} catch (e) {
  console.error('FAIL: Error:', e.message);
  console.error('Code:', e.code);
  process.exit(1);
}

console.log('PASS: Done!');
