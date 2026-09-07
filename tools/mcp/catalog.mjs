import { z } from 'zod';

const text = z.string().max(2048);
const id = z.union([z.string().regex(/^[0-9]+$/), z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER)]);
const reference = z.union([id, z.string().regex(/^@[A-Za-z0-9_-]{1,64}$/)]);
const revision = z.string().regex(/^[0-9]+$/).describe('Exact revision from the most recent scene/status read; stale edits are rejected.');
const vector = z.array(z.number().min(-1e6).max(1e6)).length(3);
const color = z.array(z.number().min(0).max(1)).length(3);
const object = shape => z.object(shape).strict();
const material = object({
  asset: text.optional(), color: z.array(z.number().min(0).max(1)).length(4).optional(),
  roughness: z.number().min(0.04).max(1).optional(), metallic: z.number().min(0).max(1).optional(),
  shadow: z.boolean().optional(), unlit: z.boolean().optional(), textures: z.array(text).length(4).optional(),
  uvScale: z.array(z.number()).length(2).optional(), uvOffset: z.array(z.number()).length(2).optional(),
  emission: color.optional(), emissionStrength: z.number().min(0).max(100).optional(),
  normalStrength: z.number().min(0).max(4).optional(), alphaCutoff: z.number().min(0).max(1).optional(),
  surface: z.number().int().min(0).max(2).optional(), doubleSided: z.boolean().optional()
});
const light = object({
  kind: z.enum(['directional', 'point', 'spot']).optional(), color: color.optional(), direction: vector.optional(),
  intensity: z.number().min(0).max(1000).optional(), range: z.number().min(0.1).max(10000).optional(),
  innerAngle: z.number().min(0.1).max(175).optional(), outerAngle: z.number().min(0.2).max(179).optional()
});
const body = object({ motion: z.enum(['static', 'dynamic']).optional(), mass: z.number().min(0.01).max(100000).optional(), restitution: z.number().min(0).max(1).optional() });
const variableValues = z.record(z.string().regex(/^[A-Za-z_][A-Za-z0-9_]{0,63}$/), z.number().min(-1e6).max(1e6).nullable());
const nodeFields = {
  kind: z.enum(['begin_play','tick','key_pressed','translate','rotate','velocity','set_variable','add_variable','branch','set_visible','set_color']),
  position: z.array(z.number().min(-1e6).max(1e6)).length(2).optional(), vector: vector.optional(), value: z.number().min(-1e6).max(1e6).optional(),
  variable: z.string().max(64).optional(), key: z.string().max(16).optional(), comparison: z.enum(['less','less_equal','equal','not_equal','greater_equal','greater']).optional(), visible: z.boolean().optional()
};
const nodeId = z.number().int().min(1).max(1000000);
const node = object({ id: nodeId, ...nodeFields });
const graph = object({ enabled: z.boolean().optional(), nodes: z.array(node).max(128), links: z.array(object({ from: nodeId, to: nodeId, output: z.enum(['next','true','false']).optional() })).max(256) });
const fields = object({
  name: z.string().min(1).max(128).optional(), visible: z.boolean().optional(), position: vector.optional(),
  rotation: z.array(z.number()).length(4).optional(), scale: vector.optional(),
  mesh: material.nullable().optional(), light: light.nullable().optional(), body: body.nullable().optional(),
  spin: z.number().min(-3600).max(3600).nullable().optional(), keyboardDrive: z.number().min(0.1).max(100).nullable().optional(), behavior: graph.nullable().optional()
});
const settings = object({ name: z.string().min(1).max(128).optional(), mode: z.enum(['2d', '3d']).optional(),
  ambient: z.number().min(0).max(4).optional(), shadows: z.boolean().optional(), rayTracedShadows: z.boolean().optional(), variables: variableValues.optional(),
  view: object({ target: vector, yaw: z.number().min(-1000).max(1000), pitch: z.number().min(-1.48).max(1.48), distance: z.number().min(0.3).max(180) }).nullable().optional() });
const transform = { entity: reference, space: z.enum(['local', 'world']).optional(), position: vector.optional(),
  rotation_degrees: vector.optional(), scale: vector.optional(), translate: vector.optional(), rotate_degrees: vector.optional(), scale_factor: vector.optional() };
const create = { name: z.string().min(1).max(128).optional(), primitive: text.optional(), fields: fields.optional(), as: z.string().regex(/^[A-Za-z0-9_-]{1,64}$/).optional() };
const operations = z.array(z.discriminatedUnion('op', [
  object({ op: z.literal('create'), ...create }), object({ op: z.literal('patch'), entity: reference, fields }),
  object({ op: z.literal('transform'), ...transform }), object({ op: z.literal('delete'), entity: reference }),
  object({ op: z.literal('duplicate'), entity: reference }),
  object({ op: z.literal('reparent'), entity: reference, parent: reference, preserve_world: z.boolean().optional() }),
  object({ op: z.literal('settings'), fields: settings })
])).min(1).max(256);
const edit = { expected_revision: revision, dry_run: z.boolean().optional() };
const page = { offset: z.number().int().nonnegative().optional(), limit: z.number().int().min(1).max(1000).optional() };

export const tools = [];
function add(method, description, shape = {}, readOnly = false, destructive = false) {
  const name = `velos_${method.replaceAll('.', '_')}`;
  const schema = object(shape);
  tools.push({ method, schema, definition: { name, description, inputSchema: z.toJSONSchema(schema, { target: 'draft-7' }),
    annotations: { readOnlyHint: readOnly, destructiveHint: destructive, idempotentHint: readOnly, openWorldHint: false } } });
}

add('system.status', 'Read the live scene revision, selection, saved/dirty state, simulation state and private control connection counters.', {}, true);
add('system.capabilities', 'Discover implemented features, component classes, graph node types, numeric limits, coordinate conventions and explicit unsupported/excluded capabilities before planning a game.', {}, true);
add('classes.list', 'Discover every native component class, storage key and default field value. This is the current engine schema, not arbitrary C++ reflection.', {}, true);
add('scene.get', 'Read a page of live scene entities and all supported component values. IDs and revisions are lossless decimal strings.', page, true);
add('entity.get', 'Read one entity, its component values and row-major world transform matrix.', { entity: id }, true);
add('scene.transaction', 'Apply 1-256 scene operations atomically as one undo command. Create aliases use as:"player" then entity:"@player". Errors roll back the entire batch. dry_run validates without applying. All authored edits require the latest revision.', {
  ...edit, operations, label: z.string().min(1).max(128).optional()
});
add('entity.create', 'Create an empty entity or built-in/imported mesh, with optional material, light, physics and gameplay fields.', { ...edit, ...create });
add('entity.patch', 'Edit entity/component fields without replacing unspecified values. Set an optional component to null to remove it; identity and transform are required.', { ...edit, entity: id, fields });
add('entity.transform', 'Position, rotate and resize an entity in local or world space. Rotation uses degrees [pitch,yaw,roll]. translate/rotate_degrees/scale_factor apply relative changes.', { ...edit, ...transform });
add('entity.reparent', 'Reparent an entity, optionally preserving its world transform. Cycles and unsupported shear fail atomically. Parent 0 detaches to the root.', { ...edit, entity: id, parent: id, preserve_world: z.boolean().optional() });
add('entity.duplicate', 'Duplicate one entity and its components. Children are not duplicated. Returns the new entity ID.', { ...edit, entity: id });
add('entity.delete', 'Delete an entity and its descendants as one undoable transaction.', { ...edit, entity: id }, false, true);
add('component.set', 'Add or patch a supported component. Discover native storage keys/defaults with classes_list. Use entity_patch for multiple components.', { ...edit, entity: id, component: z.enum(['mesh','light','body','spin','keyboardDrive','behavior']), value: z.union([material,light,body,graph,z.number()]) });
add('component.remove', 'Remove an optional component from an entity; required identity/transform components cannot be removed.', { ...edit, entity: id, component: z.enum(['mesh','light','body','spin','keyboardDrive','behavior']) }, false, true);
add('scene.settings', 'Change the scene name, 2D/3D mode, ambient illumination and raster/DXR shadow policy.', { ...edit, fields: settings });
add('variables.get', 'Read the scene numeric gameplay variables and revision.', {}, true);
add('variables.set', 'Add/change scene numeric variables, or use null to remove unused variables. Graph references are validated. Edits are undoable and persist into exported games.', { ...edit, values: variableValues });
add('graph.catalog', 'Discover executable gameplay node types, output ports and limits. Graphs are acyclic and operate on their owning entity.', {}, true);
add('graph.get', 'Read an entity gameplay graph, persistent node layout, scene variables and revision.', { entity: id }, true);
add('graph.set', 'Replace an entity gameplay graph atomically. Tick translate/rotate actions use units/degrees per second multiplied by value or a referenced variable. Physics bodies use velocity nodes.', { ...edit, entity: id, graph });
add('graph.add_node', 'Add a typed node to an entity behavior graph. The node ID must be unique within the graph.', { ...edit, entity: id, node });
add('graph.update_node', 'Edit node fields or its persistent canvas position without changing its ID.', { ...edit, entity: id, node_id: nodeId, fields: object(nodeFields).partial() });
add('graph.remove_node', 'Remove a node and every attached execution link, undoably.', { ...edit, entity: id, node_id: nodeId }, false, true);
add('graph.connect', 'Connect an execution output to another node. Ordinary outputs are next; branch outputs are true/false. Duplicate outputs, cycles and inputs into event nodes are rejected.', { ...edit, entity: id, from: nodeId, to: nodeId, output: z.enum(['next','true','false']).optional() });
add('graph.disconnect', 'Remove an execution connection from the specified output.', { ...edit, entity: id, from: nodeId, output: z.enum(['next','true','false']).optional() }, false, true);
add('graph.state', 'Read executed behavior nodes and their last simulation tick for live visual debugging.', {}, true);
add('graph.relationships', 'Read a paginated machine-readable scene relationship graph with parent edges and complete component data.', page, true);
add('scene.generate', 'Generate an editable grid or ring of 1-256 objects as one undo command. Grid spacing is separation; ring spacing is radius. More elaborate scenes use scene_transaction or scene_replace.', {
  ...edit, layout: z.enum(['grid','ring']).optional(), count: z.number().int().min(1).max(256).optional(), columns: z.number().int().min(1).max(256).optional(),
  spacing: z.number().min(0.01).max(1000).optional(), origin: vector.optional(), primitive: text.optional(), name: z.string().max(100).optional(), fields: fields.optional()
});
add('scene.new', 'Replace the authored scene with an empty/workshop template. Unsaved changes require explicit discard_changes. The current save destination is retained; use project_save to choose a new destination.', {
  ...edit, name: z.string().min(1).max(128).optional(), mode: z.enum(['2d','3d']).optional(), template: z.enum(['empty','workshop']).optional(), discard_changes: z.boolean().optional()
}, false, true);
add('scene.replace', 'Validate and replace a complete native schema-1 scene JSON document, undoably. Use this for externally generated scenes. Asset references must already exist inside the allowed workspace.', {
  ...edit, scene: z.record(z.string(),z.unknown()), discard_changes: z.boolean().optional()
}, false, true);
add('history.undo', 'Undo one authored transaction. Not available during simulation or an active UI gesture.', { expected_revision: revision }, false, true);
add('history.redo', 'Redo one authored transaction.', { expected_revision: revision });
add('project.list', 'List supported project/asset files and directories inside the configured workspace. No arbitrary file contents, hidden files, device paths or links are exposed.', { ...page, directory: text.optional() }, true);
add('project.open', 'Open a .velos scene within the allowed workspace. Unsaved changes require explicit discard_changes.', { path: text, expected_revision: revision, discard_changes: z.boolean().optional() }, false, true);
add('project.save', 'Atomically save a .velos scene. Save As copies referenced assets. An existing different scene requires overwrite:true. All paths are workspace-relative.', { path: text.optional(), expected_revision: revision, overwrite: z.boolean().optional() }, false, true);
add('assets.list', 'List built-in meshes and referenced project assets with their workspace-relative paths.', {}, true);
add('assets.import_model', 'Start a background static GLB import from a workspace-relative source. Optionally create an entity (default true). Returns a job ID; inspect jobs_get. Unsupported formats fail explicitly.', { path: text, name: z.string().max(128).optional(), create_entity: z.boolean().optional(), expected_revision: revision });
add('assets.import_texture', 'Cook and assign an image map to an existing material. Slots: 0 albedo, 1 normal, 2 ORM, 3 emissive. Source must be inside the workspace. Returns a background job; source/settings determine the cache key.', {
  path: text, entity: id, slot: z.number().int().min(0).max(3), expected_revision: revision
});
add('jobs.get', 'Read a background job status/result/error. States: running, cancelling, cancelled, succeeded, failed. The most recent 32 jobs are retained.', { job: z.string().regex(/^[0-9]+$/) }, true);
add('jobs.cancel', 'Cancel a pending model/texture import before it applies to the scene. Decoding finishes off-thread; already completed edits and exports cannot be cancelled.', { job: z.string().regex(/^[0-9]+$/) });
add('build.export', 'Export the saved scene and referenced assets into an empty workspace directory with the standalone runtime, shaders, notices and integrity manifest. Does not compile arbitrary code. Returns a job ID.', { directory: text, expected_revision: revision }, false, true);
add('build.verify', 'Verify the complete file whitelist and hashes of an exported runtime package.', { directory: text }, true);
add('editor.select', 'Select an entity in the actual editor and optionally frame it in the viewport; entity 0 clears selection.', { entity: id, focus: z.boolean().optional() });
add('editor.graph', 'Open the live Scene, Classes or Logic node canvas in the native editor. Logic nodes and execution links edit the real behavior graph; classes show component composition, not arbitrary C++ source editing.', {
  view: z.enum(['scene','classes','logic']), entity: id.optional(), arrange: z.boolean().optional(), expected_revision: revision.optional()
});
add('editor.focus', 'Focus a native editor panel or docked tab without changing the authored scene.', { panel: z.enum(['Viewport','Graphs','Scene','Inspector','Assets','Performance','Console','Assistant']) });
add('camera.get', 'Read the editor viewport camera, target, orbit angles, distance and mode.', {}, true);
add('camera.set', 'Set the viewport camera. save_to_scene:true plus expected_revision also stores this camera undoably for exported games; normal navigation is not persisted. To switch 2D/3D mode, update scene settings.', {
  target: vector.optional(), yaw_degrees: z.number().min(-36000).max(36000).optional(), pitch_degrees: z.number().min(-84).max(84).optional(), distance: z.number().min(0.3).max(180).optional(),
  save_to_scene: z.boolean().optional(), expected_revision: revision.optional()
});
add('simulation.state', 'Read Play/Pause state, fixed timestep, body count and scene revision.', {}, true);
add('simulation.play', 'Start or resume the real Jolt/gameplay simulation. Stop restores the authored snapshot. Invalid physics configurations fail explicitly.');
add('simulation.pause', 'Pause a running simulation without restoring authored state.');
add('simulation.stop', 'Stop simulation and restore the authored scene snapshot.', {}, false, true);
add('simulation.step', 'Start paused simulation if needed and advance 1-120 fixed ticks deterministically.', { steps: z.number().int().min(1).max(120).optional() });
add('simulation.velocity', 'Set X/Z planar velocity on a dynamic physics body during Play/Pause. For persistent keyboard movement, author the keyboardDrive component.', { entity: id, horizontal: z.number().min(-100).max(100).optional(), forward: z.number().min(-100).max(100).optional() });
add('simulation.input', 'Set up to sixteen held virtual keys for fixed-step gameplay tests. Supports A-Z, 0-9, Space, Enter and arrows. Empty keys releases input; Stop clears it. No global OS keystrokes are injected.', { keys: z.array(z.string().max(16)).max(16) });
add('renderer.get', 'Inspect GPU adapter, active shadow fallback, timings, memory, draw/triangle counters and graphics settings.', {}, true);
add('renderer.set', 'Adjust exposure, resolution, shadow size, DXR budget, batching, LODs, wireframe, grid and VSync. DXR remains capability-gated.', {
  exposure: z.number().min(0.1).max(8).optional(), resolution_scale: z.number().min(0.25).max(1.5).optional(), shadow_resolution: z.union([z.literal(512),z.literal(1024),z.literal(2048)]).optional(),
  ray_budget_mb: z.number().int().min(0).max(256).optional(), instancing: z.boolean().optional(), lods: z.boolean().optional(), lod_bias: z.number().min(0.25).max(4).optional(),
  wireframe: z.boolean().optional(), grid: z.boolean().optional(), vsync: z.boolean().optional()
});
add('renderer.reload_shaders', 'Reload the fixed engine HLSL shader entries while preserving last-good pipelines on failure; does not run shell commands.');
add('viewport.capture', 'Save an actual GPU-readback PNG of the editor. Returns rendered_revision/frame; pass expected_revision to reject stale images. Optional include_image returns PNG image content to the MCP client.', {
  path: text, expected_revision: revision.optional(), overwrite: z.boolean().optional(), include_image: z.boolean().optional()
}, false, true);
add('editor.logs', 'Read bounded editor log messages and D3D12 validation errors. Does not expose AI credentials or general filesystem logs.', { limit: z.number().int().min(0).max(250).optional() }, true);
add('editor.close', 'Close the editor. Requires current revision and an explicit discard_changes flag when unsaved authored data exists; use project_save first.', {
  expected_revision: revision, discard_changes: z.boolean().optional()
}, false, true);

export const toolByName = new Map(tools.map(tool => [tool.definition.name, tool]));