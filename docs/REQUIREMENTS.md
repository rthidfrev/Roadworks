# Architecture constraints

- Extend the native blueprint/Build Gun workflow with start, up to 30 intermediate
  points, end and confirmation. Editing a previous point preserves later points.
- Per-point settings: heading, grade, bank and height. Route settings: tension,
  overlap, slice length, width, thickness, edge-step tolerance, deck plane and axis.
- Cubic Hermite spans use an adaptive arc-length table and transported frames.
  Internally, distances are centimetres, with X forward, Y right and Z up.
  Initial travel is opposite the selected blueprint's native forward arrow.
- Blueprint slices remain rigid. Deck dimensions approximate a continuous slab;
  decorations should not determine its inferred thickness. Limits are 45° grade,
  60° bank, 256 road slices and a shared preview-member budget.
- Decoration layers use cumulative 3D centreline distance, independent of slice
  overlap. Each has its own source, spacing, first distance, local offsets,
  enabled state and grade/bank following. Maximum eight layers.
- Presets store the composition and global settings, not world positions/points.
  Validate JSON and referenced blueprints before replacing a preview. Updates
  retain a backup outside the plugin directory.
- Persist only native buildings. Custom monitor, previews and input state remain
  local/transient. Blueprints containing another mod's objects retain that dependency.
- Respect native cost/refusal checks. Send one construction job at a time and wait
  for acknowledgement; cancellation stops future requests without dismantling
  confirmed buildings. Do not retry an uncertain native transaction automatically.
- Preserve scoped session routing while the native Build Gun temporarily replaces
  its hologram. Strict original-hologram matching applies outside that scope.
- The V2 monitor uses a world-space WidgetComponent and Panini-aware materials.
  Its native input host forwards to virtual Slate controls. The compact monitor
  is passive; expanded text entry suspends placement shortcuts. Restore native
  presentation/input ownership on exit. All authored UI text is English.
- Never bypass SML network checks. Required multiplayer qualification is a vanilla
  host without SML, a builder client with SML and this mod, and unmodded observers.
  Save/removal and multiplayer guarantees require human in-game tests.
