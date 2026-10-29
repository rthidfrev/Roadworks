# FICSIT.app listing

Fields and Markdown ready for the FICSIT.app editor. Screenshot comments mark
future captures and stay invisible in the rendered description.

## Listing fields

| Field | Value |
| --- | --- |
| Name | Roadworks |
| Mod reference | `ParametricRoads` |
| Tags | `buildgun`, `blueprints`, `qol`, `decoration` |
| Source | https://github.com/rthidfrev/Roadworks |
| Icon | [Approved Roadworks logo](../Mods/ParametricRoads/Resources/Icon128.png) |

The reference matches the existing plugin. Keep `vanilla-friendly` and
`multiplayer` off until their in-game compatibility checks are complete.

### Short description

Build curved, sloped and banked roads from blueprints, with editable points, spaced decoration layers and reusable presets.

## Full description

Copy the Markdown between the markers into the full-description field.

<!-- BEGIN FICSIT DESCRIPTION -->
# Roadworks

Build curved, sloped and banked roads from your own Satisfactory blueprints.
Place the start, add intermediate points, set the end and confirm the route.
Blueprints stay rigid: Roadworks positions and rotates copies along the path.

<!-- SCREENSHOT overview: A finished curved, sloped and banked road, photographed without the HUD.
When captured, replace this comment with: ![A finished curved, sloped and banked road, photographed without the HUD.](https://raw.githubusercontent.com/rthidfrev/Roadworks/main/docs/media/roadworks-overview.jpg) -->

## Features

- **Editable points:** revisit Start, intermediate points and End to adjust heading, grade, bank and height.
- **Decoration layers:** add up to eight secondary blueprints, each with its own spacing, first placement and local offsets. Distances follow the actual 3D route.
- **Reusable presets:** save and load a base blueprint, route settings and decoration layers as local JSON files. Presets store the composition, not the placed route points.
- **Build Gun integration:** remappable shortcuts, native control hints and a deployable monitor with an interactive terminal.

<!-- SCREENSHOT layers: A curve with two decoration layers using different spacing or offsets.
When captured, replace this comment with: ![A curve with two decoration layers using different spacing or offsets.](https://raw.githubusercontent.com/rthidfrev/Roadworks/main/docs/media/roadworks-layers.jpg) -->

## Get started

Use a short road-slice blueprint you have already created or imported.

1. Select a short road-slice blueprint in the Build Gun and choose **Roadworks**.
2. Place the start, add intermediate points if needed, then set the end.
3. Adjust the shape and configure decoration layers in the terminal.
4. Confirm construction and wait for the progress count to finish.

Wheel adjusts the selected value; **Left Shift + Wheel** changes the setting,
**Numpad 2** selects a point and **Numpad 3** opens the terminal. Shortcuts are
remappable under **Options → Controls → Roadworks**.
[Read the player guide](https://github.com/rthidfrev/Roadworks/blob/main/docs/USER_GUIDE.md).

<!-- SCREENSHOT points: A route preview with Start, intermediate points and End, with a readable monitor.
When captured, replace this comment with: ![A route preview with Start, intermediate points and End, with a readable monitor.](https://raw.githubusercontent.com/rthidfrev/Roadworks/main/docs/media/roadworks-points.jpg) -->

<!-- SCREENSHOT presets: The preset selector and a composition reused on another route.
When captured, replace this comment with: ![The preset selector and a composition reused on another route.](https://raw.githubusercontent.com/rthidfrev/Roadworks/main/docs/media/roadworks-presets.jpg) -->

## Requirements and compatibility

The current candidate targets **Satisfactory CL 513885** and **SML 3.12.0**.
Windows client binaries are packaged for Steam and Epic; local playtesting has
been on Steam through Proton.

**Not yet verified:** reloading a save after removing Roadworks/SML, multiplayer
with a vanilla host, and Epic runtime use. Roadworks places native buildings;
blueprints containing another mod's objects retain that dependency.

## Feedback and source

[Report a bug](https://github.com/rthidfrev/Roadworks/issues/new?template=01-bug-report.yml) · [Suggest an idea](https://github.com/rthidfrev/Roadworks/issues/new?template=02-feature-request.yml) · [Source code](https://github.com/rthidfrev/Roadworks)

Code, scripts and documentation are MIT-licensed. Original monitor assets are
CC BY 4.0. See the repository's [licenses and notices](https://github.com/rthidfrev/Roadworks#source-and-licenses).
<!-- END FICSIT DESCRIPTION -->

## Network Activity Transparency

Roadworks makes no external network connections. It sends no telemetry or
analytics and does not contact external APIs or AI services. Presets are stored
locally. Multiplayer construction uses Satisfactory's normal game communication.

## AI Usage Transparency

Generative AI assisted with source code, development scripts, UI design,
documentation and this listing text. The Roadworks logo was AI-generated.
The monitor model, procedural textures and synthesized audio were created with
AI-assisted Blender/Python workflows. Roadworks does not run generative AI in-game.

## Captures to add

Save real gameplay captures under `docs/media/` and replace the matching comments
in this description and the README with the supplied image markup.

| File | Content |
| --- | --- |
| `roadworks-overview.jpg` | A finished curved, sloped and banked road, photographed without the HUD. |
| `roadworks-points.jpg` | A route preview with Start, intermediate points and End, with a readable monitor. |
| `roadworks-layers.jpg` | A curve with two decoration layers using different spacing or offsets. |
| `roadworks-presets.jpg` | The preset selector and a composition reused on another route. |

Tags checked against the [public API](https://ficsit.app/api-docs) on 2026-10-29.
Disclosure fields follow the [FICSIT.app Content Policy](https://ficsit.app/content-policy).
