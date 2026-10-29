# Player guide

## Place a road

Select a short road-slice blueprint and choose **Roadworks**. Place the start,
add intermediate points if needed, set the end, then confirm. Cancel goes back
one action; during construction it stops remaining requests. Already confirmed
buildings remain built. Wait for the final progress count before starting again.

| Action | Default |
| --- | --- |
| Adjust selected value | Wheel |
| Select setting | Left Shift + Wheel |
| Next setting | Numpad 1 |
| Select Start / P1… / End | Numpad 2 |
| Add point | Numpad 0 |
| Fine step ×0.1 | Numpad Decimal |
| Increase / decrease | Numpad + / − |
| Open terminal | Numpad 3 |

Remap actions under **Options → Controls → Roadworks**. Current bindings appear
in the native Build Gun hints. Monitor audio has its own slider under
**Options → Audio → Roadworks**.

## Edit settings

The expanded terminal uses the same worksheet as the compact monitor. Select a
point in the left tree, then edit its heading, grade, bank or height. AUTO marks
an automatically derived heading/grade; editing pins that component. Whole-route
settings apply to every span. Click a label to show its simplified 2D diagram.

Enter a number or use **− / +**; dragging is disabled. Commit with Enter or focus
loss. Use the Fine step checkbox in the terminal. Escape finishes a text edit
first, then closes the terminal. The close cross returns to placement without
resetting points. File/Edit/View/Tools/Help are decorative.

Angles use degrees; dimensions/offsets use metres, except **Max edge step** uses
centimetres. Positive bank lowers the road's right edge. Height offsets the
captured point anchor. Verify inferred deck dimensions and axis: pillars/rails
can make an automatic slab estimate too large.

## Layers and presets

Use **Layers…** to add secondary blueprints. Select a layer, choose **Change…**,
browse categories/subcategories or search, then choose **Use blueprint**.
Cancel keeps the current source. Each layer has independent spacing, first
placement, Forward/Right/Up offsets and grade/bank following. Distances follow
the 3D centreline; offsets use the road's local frame. The library updates
automatically while open. Disable a missing/refused layer before confirming.

Use **Presets… → Save new** to store the base blueprint, global settings and all
layers. **Update selected** replaces a preset and retains its previous JSON as
`.bak`. Loading starts a new route; an existing preview requires confirmation.
Missing or renamed blueprints block loading without changing that preview.

Files live in `ParametricRoads/Presets` (the legacy storage folder) under the game's Saved directory, normally
`%LOCALAPPDATA%/FactoryGame/Saved` on Windows or its equivalent in the Proton prefix.
The footer tooltip shows the resolved folder. Share the GUID-named JSON file and
copy its referenced blueprints separately. Names must match exactly. Schema v1
uses explicit metre fields; each file is limited to 64 KiB, at most 256 are listed.

## Compatibility checks

Before relying on save independence, use a copy of a save containing native
buildings: build straight/curved/sloped/banked sections, save, exit and reload
without this mod, then without SML. Check collision, driving and dismantling,
save under another name and reload again. Other mods' buildings keep their dependencies.

For multiplayer, test a vanilla host without SML, a builder client with SML and
Roadworks, and unmodded observers. Check construction, costs, late join,
reconnect and host save/reload. These scenarios remain unqualified; build success
does not replace them. Include game/SML/mod versions and the displayed refusal
or progress count when reporting a bug.
