import test from 'node:test';
import assert from 'node:assert/strict';
import { tools, toolByName } from '../catalog.mjs';

test('catalog is unique, bounded and annotated', () => {
  assert.ok(tools.length >= 40);
  assert.equal(toolByName.size, tools.length);
  for (const tool of tools) {
    assert.match(tool.definition.name, /^velos_[a-z_]+$/);
    assert.equal(tool.definition.inputSchema.type, 'object');
    assert.equal(tool.definition.inputSchema.additionalProperties, false);
    assert.equal(typeof tool.definition.annotations.readOnlyHint, 'boolean');
    assert.equal(tool.definition.annotations.openWorldHint, false);
  }
});
test('strict schemas reject unsafe IDs, unknown fields and oversized batches', () => {
  const parse = (name, args) => toolByName.get(name).schema.safeParse(args).success;
  assert.equal(parse('velos_entity_get', { entity: Number.MAX_SAFE_INTEGER + 1 }), false);
  assert.equal(parse('velos_entity_get', { entity: '9007199254740992' }), true);
  assert.equal(parse('velos_entity_patch', { entity: '1', expected_revision: '2', fields: { mesh: { roughnes: 0.5 } } }), false);
  assert.equal(parse('velos_scene_transaction', { expected_revision: '1', operations: Array(257).fill({ op: 'delete', entity: '1' }) }), false);
  assert.equal(parse('velos_entity_transform', { entity: '1', expected_revision: '2', position: [1,2,3], rotation_degrees: [0,90,0] }), true);
  assert.equal(parse('velos_renderer_set', { ray_budget_mb: 257 }), false);
});