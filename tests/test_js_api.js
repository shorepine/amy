"use strict";

const assert = require("node:assert/strict");
const path = require("node:path");

require(path.join(__dirname, "..", "src", "amy_api.generated.js"));

assert.equal(
  amy_message({sequence_control: [7, 0.625, 48]}),
  "HC7,0.625,48Z"
);
assert.equal(
  amy_message({ticks: [0, 48, 3], sequence_control: [7, 1, 1]}),
  "H0,48,3HC7,1,1Z"
);
assert.equal(amy_message({sequence_reset: 7}), "HR7Z");
assert.equal(
  amy_message({sequence_control: [7, AMY.SEQUENCE_CONTROL_GATE, 24, 1]}),
  "HC7,2,24,1Z"
);

console.log("JavaScript reusable-sequence API checks passed");
