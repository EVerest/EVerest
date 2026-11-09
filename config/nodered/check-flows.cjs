// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
// Run with: node config/nodered/check-flows.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const files = [
  'config-sil-flow.json',
  'config-sil-dc-flow.json',
  'config-sil-dc-bpt-flow.json',
  'config-sil-two-evse-flow.json',
  'config-sil-energy-management-flow.json',
  '../../applications/utils/docker/everest-docker-image/nodered/flows/config-sil-two-evse-flow.json',
];

function checkFlow(file) {
  const flow = JSON.parse(fs.readFileSync(path.join(__dirname, file)));
  const ids = new Set(flow.map((node) => node.id));
  assert.equal(ids.size, flow.length, 'duplicate node ids');
  for (const node of flow) {
    for (const wire of (node.wires || []).flat()) {
      assert(ids.has(wire), `missing wire target ${wire}`);
    }
  }
  const configs = flow.filter((node) => node.name === 'Configure session then plug');
  assert(configs.length, 'missing configuration function');
  const instances = new Set();
  for (const config of configs) {
    assert.equal(config.outputs, 2, `${config.id}: configuration errors need a dashboard output`);
    assert(config.wires[1].some((id) => flow.find((node) => node.id === id).type === 'ui_toast'),
      `${config.id}: missing dashboard toast`);
    const trigger = flow.find((node) => node.topic?.endsWith('/m2e/configure_session') &&
      node.wires?.flat().includes(config.id));
    assert(trigger, `${config.id}: missing configure_session trigger`);
    const base = trigger.topic.slice(0, -'/m2e/configure_session'.length);
    const instance = base.split('/').pop();
    assert(!instances.has(instance), `${config.id}: duplicate configuration instance ${instance}`);
    instances.add(instance);
    function run(status) {
      const values = new Map();
      const sent = [];
      const warnings = [];
      let timeout;
      const sandbox = {
        context: {get: (key) => values.get(key), set: (key, value) => values.set(key, value)},
        node: {warn: (value) => warnings.push(value), send: (value) => sent.push(value)},
        setTimeout: (fn) => { timeout = fn; return 1; }, clearTimeout: () => { timeout = undefined; },
      };
      const invoke = (msg) => vm.runInNewContext(`(function(msg) {${config.func}\n})(msg)`, {...sandbox, msg});
      const start = invoke({topic: `${base}/m2e/configure_session`});
      assert.equal(start[0].topic, `${base}/m2e/configure_session`);
      if (status) {
        const result = invoke({topic: `${base}/e2m/command_ack`, payload: {command: 'configure_session', status}});
        if (status === 'Accepted') {
          assert.equal(result[0].topic, `everest_api/1/ev_driver_sim/${instance}_driver/m2e/plug`);
          assert.equal(warnings.length, 0);
        } else {
          assert.equal(result[0], null);
          assert.match(result[1].payload, /rejected/i);
          assert.equal(warnings.length, 1);
        }
      } else {
        assert(timeout, 'missing acknowledgement must time out');
        timeout();
        assert.equal(sent[0][0], null);
        assert.match(sent[0][1].payload, /timed out/i);
        assert.equal(warnings.length, 1);
        assert.equal(invoke({topic: `${base}/e2m/command_ack`, payload: {command: 'configure_session', status: 'Accepted'}}), null);
      }
    }
    run('Accepted');
    run('Rejected');
    run();
  }
  for (const trigger of flow.filter((node) => node.topic?.endsWith('/m2e/configure_session'))) {
    assert(configs.some((config) => trigger.wires?.flat().includes(config.id)),
      `${trigger.id}: configure_session trigger must reach a configuration node`);
  }
  for (const selector of flow.filter((node) => node.type === 'ui_dropdown' && node.topic === 'session_mode')) {
    assert(configs.some((config) => selector.wires?.flat().includes(config.id)),
      `${selector.id}: session_mode selector must reach a configuration node`);
    const options = selector.options;
    if (!options.some((entry) => JSON.parse(entry.value).mode.startsWith('Ac'))) continue;
    for (const label of ['AC ISO15118-20', 'AC ISO15118-20 BPT']) {
      const entry = options.find((entry) => entry.label === label);
      assert(entry, `${selector.id}: missing ${label}`);
      const option = JSON.parse(entry.value);
      assert.equal(option.mode, 'AcIsoD20');
      assert.equal(option.params.charging_current_a, 16);
      assert.equal(option.params.three_phases, true);
      if (label.endsWith('BPT')) assert.equal(option.params.bpt.discharge_minimal_soc, 20);
    }
  }
  console.log(`${file}: passed`);
}

for (const file of files) {
  try {
    checkFlow(file);
  } catch (error) {
    console.error(`${file}: ${error.message}`);
    process.exitCode = 1;
  }
}
