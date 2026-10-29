# Roadworks

<img src="Mods/ParametricRoads/Resources/Icon128.png" alt="Roadworks" width="320">

**[Player guide](docs/USER_GUIDE.md)** · **[Report a bug](https://github.com/rthidfrev/Roadworks/issues/new?template=01-bug-report.yml)** · **[Suggest an idea](https://github.com/rthidfrev/Roadworks/issues/new?template=02-feature-request.yml)**

Build curved, sloped and banked roads from your own Satisfactory blueprints.
Place the start, add intermediate points, set the end and confirm the route.
Blueprints stay rigid: Roadworks positions and rotates copies along the path.

<!-- SCREENSHOT overview: A finished curved, sloped and banked road, photographed without the HUD.
When captured, replace this comment with: ![A finished curved, sloped and banked road, photographed without the HUD.](docs/media/roadworks-overview.jpg) -->

## What you can build

- **Editable points:** revisit Start, intermediate points and End to adjust heading, grade, bank and height.
- **Decoration layers:** add up to eight secondary blueprints, each with its own spacing, first placement and local offsets. Distances follow the actual 3D route.
- **Reusable presets:** save and load a base blueprint, route settings and decoration layers as local JSON files. Presets store the composition, not the placed route points.
- **Build Gun integration:** remappable shortcuts, native control hints and a deployable monitor with an interactive terminal.

<!-- SCREENSHOT layers: A curve with two decoration layers using different spacing or offsets.
When captured, replace this comment with: ![A curve with two decoration layers using different spacing or offsets.](docs/media/roadworks-layers.jpg) -->

## Get started

Use a short road-slice blueprint you have already created or imported.

1. Select a short road-slice blueprint in the Build Gun and choose **Roadworks**.
2. Place the start, add intermediate points if needed, then set the end.
3. Adjust the shape and configure decoration layers in the terminal.
4. Confirm construction and wait for the progress count to finish.

Wheel adjusts the selected value; **Left Shift + Wheel** changes the setting,
**Numpad 2** selects a point and **Numpad 3** opens the terminal. All shortcuts
can be remapped under **Options → Controls → Roadworks**.
See the [player guide](docs/USER_GUIDE.md) for blueprint setup, layers and presets.

<!-- SCREENSHOT points: A route preview with Start, intermediate points and End, with a readable monitor.
When captured, replace this comment with: ![A route preview with Start, intermediate points and End, with a readable monitor.](docs/media/roadworks-points.jpg) -->

<!-- SCREENSHOT presets: The preset selector and a composition reused on another route.
When captured, replace this comment with: ![The preset selector and a composition reused on another route.](docs/media/roadworks-presets.jpg) -->

## Requirements and compatibility

The current candidate targets **Satisfactory CL 513885** and **SML 3.12.0**.
Windows client binaries are packaged for Steam and Epic; local playtesting has
been on Steam through Proton.

**Not yet verified:** reloading a save after removing Roadworks/SML, multiplayer
with a vanilla host, and Epic runtime use. Roadworks places native buildings;
blueprints containing another mod's objects retain that dependency.

## Transparency

**Network activity:** Roadworks makes no external network connections. It sends no telemetry or
analytics and does not contact external APIs or AI services. Presets are stored
locally. Multiplayer construction uses Satisfactory's normal game communication.

**AI usage:** Generative AI assisted with source code, development scripts, UI design,
documentation and this listing text. The Roadworks logo was AI-generated.
The monitor model, procedural textures and synthesized audio were created with
AI-assisted Blender/Python workflows. Roadworks does not run generative AI in-game.

## Feedback

Check [existing reports and ideas](https://github.com/rthidfrev/Roadworks/issues?q=is%3Aissue) before opening an issue.
A GitHub account is required to submit feedback. Screenshots, logs and technical
details can be added later.

## Source and licenses

- [Build and test](docs/DEVELOPMENT.md) · [Architecture](docs/REQUIREMENTS.md) · [FICSIT.app listing copy](docs/FICSIT_APP.md)
- Plugin source and worldless tests: `Mods/ParametricRoads/`. The internal reference stays stable for existing bindings and presets.
- Current monitor V2 model, textures, FBXs and audio: `art/road-monitor/`.
- Code, scripts and documentation: [MIT](LICENSE). Original monitor assets: [CC BY 4.0](ASSET_LICENSE.md). [Third-party notices](THIRD_PARTY_NOTICES.md).
