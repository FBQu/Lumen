# Lumen Agent API

`LumenAgent` is a headless host that lets an AI agent (or any tool) build and run a game through JSON.
It reads one request per line on stdin and writes one response per line on stdout.

```
request:  {"cmd": "entity.create", "args": {...}, "requestId": <optional, echoed back>}
response: {"ok": true, "result": ...}   or   {"ok": false, "error": "message"}
```

The same protocol is available in-process through `Lumen::AgentSession::Execute(json)`. Commands never throw;
failed commands leave the scene unchanged. Entity ids are decimal strings (64-bit values).

## Entity JSON

```json
{ "id": "123", "name": "Ball",
  "transform": { "translation": [0,5,0], "rotation": [0,0,0], "scale": [1,1,1] },
  "rigidbody": { "type": "dynamic|kinematic|static", "mass": 1, "friction": 0.5, "restitution": 0,
                 "gravityScale": 1, "linearDamping": 0.05, "angularDamping": 0.05, "fixedRotation": false },
  "collider":  { "shape": "box|sphere|capsule", "halfExtents": [0.5,0.5,0.5], "radius": 0.5, "halfHeight": 0.5, "offset": [0,0,0] },
  "script":    { "source": "return { OnUpdate = function(self, dt) end }" },
  "meshRenderer": { "primitive": "cube|sphere|plane",
                    "material": { "baseColor": [1,1,1,1], "metallic": 0, "roughness": 0.5, "emissive": [0,0,0] },
                    "meshAsset": "0", "materialAsset": "0", "castShadows": true },
  "camera": { "fovDegrees": 60, "near": 0.1, "far": 1000 },
  "directionalLight": { "color": [1,1,1], "intensity": 3 } }
```
`meshAsset` / `materialAsset` are asset ids (decimal strings, "0" = none); when set they override `primitive` / `material`.
Cameras and lights look down the entity's local -Z axis, so rotate the entity to aim them.
Rotation is Euler radians. A collider without a rigidbody is static. All fields are optional on create.

## Commands

| Command | Args | Result |
|---|---|---|
| `ping`, `help` | | `"pong"`, list of command names |
| `scene.get` | | the full scene JSON |
| `scene.load` (edit mode) | `scene` | `{entityCount}`; atomic, the old scene is kept on error |
| `scene.clear` (edit mode) | | `{entityCount}` |
| `entity.create` | entity fields (no `id`) | `{id}` |
| `entity.get` | `id` | entity JSON |
| `entity.set` | `id` + JSON merge patch (RFC 7386): objects merge, `null` removes a component | entity JSON |
| `entity.destroy` | `id` | `{entityCount}` |
| `entity.find` | `name` | `{id}` |
| `entity.list` | | `[{id, name}]` |
| `asset.import_gltf` | `path` (.gltf/.glb) | `{model, name, nodes, primitives, warnings}`; idempotent; ids derive from the path, so use project-relative paths |
| `asset.list` | | `{meshes, materials, textures}` |
| `asset.instantiate` | `model`, optional `transform {translation, rotation, scale}` | `{entities: [{id, name}]}`, one entity per mesh primitive |
| `prefab.create` | `name`, `entity` (id) | `{name}`; stores a copy of the entity as a template saved with the scene |
| `prefab.spawn` | `name`, optional `overrides` (JSON merge patch, e.g. `{"name": "Bullet", "transform": {"translation": [0,1,0]}}`) | `{id, name}` |
| `prefab.list`, `prefab.get`, `prefab.delete` | `name` (get/delete) | names / template JSON / `{prefabs}` |
| `play.start` | | `{playing}`; snapshots the edit scene, starts physics and scripts |
| `play.step` | `frames` (1..100000, default 1), `dt` (default 1/60) | `{frame, entityCount}` |
| `play.state` | | `{playing, frame, entityCount}` |
| `play.stop` | | restores the edit scene exactly |
| `script.eval` (play mode) | `code` | the returned value as text, or `null` |
| `physics.raycast` (play mode) | `origin`, `direction`, `maxDistance` | `{entity, point, normal, distance}` or `null` |
| `render.screenshot` | `path` and/or `inline: true`, optional `width`/`height` (64..4096, default 1280x720), `camera` (entity id; default: first camera) | `{width, height, device, path?, png_base64?}`; renders the live scene (edit or play state) |
| `render.set` | any of `exposure`, `ambient[3]`, `clearColor[3]`, `environmentIntensity`, `showBackground`, `enableShadows`, `shadowDistance`, `shadowSoftness`, `enableAmbientOcclusion`, `aoRadius`, `aoIntensity`, `aoPower` | the current settings; validated atomically |
| `render.set_environment` | `source`: `"sky"` (procedural), `"none"`, or a `.hdr` path (Poly Haven HDRIs work); optional `intensity`, `showBackground` | `{environment}` |
| `log.get` | `clear` (bool) | recent engine log lines, including script errors |

## Lua scripting API (inside scripts and `script.eval`)

A script returns a table with optional `OnCreate(self)`, `OnUpdate(self, dt)`, `OnDestroy(self)`.

- `vec3.new(x,y,z)`, fields `x y z`, `+ - *`, `:Length()`, `:Normalized()`
- `self.Transform.Translation / Rotation / Scale`, `self.Name`, `self.ID`, `self:IsValid()`
- `Scene.CreateEntity(name)`, `Scene.DestroyEntity(e)`, `Scene.FindEntityByName(n)`, `Scene.FindEntityByID(id)`, `Scene.GetEntityCount()`
- `e:AddRigidbody{Type=,Mass=,...}`, `e:AddCollider{Shape=,HalfExtents=,Radius=,...}`, `e:HasRigidbody()`, `e:HasCollider()`
- `Physics.SetGravity/GetGravity/AddForce/AddImpulse/SetVelocity/GetVelocity/Raycast`
- `e.MeshRenderer` (`Primitive`, `BaseColor` vec4, `Metallic`, `Roughness`, `Emissive` vec3, `CastShadows`), `e.Camera` (`FovDegrees`, `Near`, `Far`), `e.DirectionalLight` (`Color`, `Intensity`);
  `e:AddMeshRenderer{...}`, `e:AddCamera{...}`, `e:AddDirectionalLight{...}`, `e:SetScript(source)`, `e:Has<Component>()`, `e:RemoveComponent("MeshRenderer"|"Rigidbody"|"Collider"|"Camera"|"DirectionalLight"|"Script")`
- `Scene.Spawn(prefab, {Name=, Translation=, Rotation=, Scale=})`, `Scene.HasPrefab(name)`, `Scene.GetAllEntities()`
- `vec4.new(x,y,z,w)`
- `Log.Info/Warn/Error/Trace(message)`

Every callback and `script.eval` call is limited to about 5 million VM instructions; runaway loops are aborted with an error.

The sandbox exposes only base, math, string, table, utf8 and coroutine libraries.

## Example session

```
{"cmd":"entity.create","args":{"name":"Ground","transform":{"translation":[0,-0.5,0]},"collider":{"halfExtents":[50,0.5,50]}}}
{"cmd":"entity.create","args":{"name":"Ball","transform":{"translation":[0,5,0]},"collider":{"shape":"sphere"},"rigidbody":{}}}
{"cmd":"play.start"}
{"cmd":"play.step","args":{"frames":120}}
{"cmd":"entity.get","args":{"id":"<ball id>"}}
{"cmd":"play.stop"}
```
