# Roadworks

<img src="Mods/ParametricRoads/Resources/Icon128.png" alt="Roadworks" width="320">

**[Player guide](docs/USER_GUIDE.md)** · **[Report a bug](https://github.com/rthidfrev/Roadworks/issues/new?template=01-bug-report.yml)** · **[Suggest an idea](https://github.com/rthidfrev/Roadworks/issues/new?template=02-feature-request.yml)**

A Satisfactory mod for building curved, sloped and banked roads from blueprints.
Place a start, add intermediate points, set the end and confirm. Add up to eight
independent decoration layers with 3D spacing and offsets, then save the
composition as a reusable JSON preset.

The tool provides remappable Build Gun controls and a deployable monitor with
an English retro worksheet. Blueprints remain rigid; buildings are positioned
and rotated rather than stretched.

## Requirements

Satisfactory **CL 513885**, **SML 3.12.0**, Windows client (Steam or Epic binaries).
The Windows game can run through Proton. Save reload after removing the mod/SML,
vanilla-host multiplayer and Epic runtime use remain unverified.

- [Build and test](docs/DEVELOPMENT.md)
- [Architecture constraints](docs/REQUIREMENTS.md)

## Feedback

Check [existing reports and ideas](https://github.com/rthidfrev/Roadworks/issues?q=is%3Aissue) before opening an issue.
A GitHub account is required to submit feedback; sign in when prompted.
Screenshots, logs and technical details can be added later.

## Source layout

`Mods/ParametricRoads/` contains the plugin and worldless tests. `art/road-monitor/`
contains the current V2 model, baked textures, Unreal FBXs and original audio.
`scripts/` contains build, verification and protected local installation tools.
Generated output and private SDK dependencies are excluded. The internal plugin
reference remains `ParametricRoads` to preserve existing bindings and presets.

Original code/scripts/docs: [MIT](LICENSE). Original models/textures/audio:
[CC BY 4.0](ASSET_LICENSE.md). See [third-party notices](THIRD_PARTY_NOTICES.md).
