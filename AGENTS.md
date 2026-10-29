# Roadworks

Read `docs/REQUIREMENTS.md`, `docs/DEVELOPMENT.md` and `environment.lock.json`
before modifying the plugin.

- Maintain authored sources in `Mods/ParametricRoads`; the SDK uses a symlink.
  `.local/` is a private dependency/cache directory and `dist/` contains output.
- Use the pinned CSS engine and SML. FactoryGame C++ bodies in the starter are
  generated stubs; inspect declarations before choosing APIs.
- In-game testing belongs to the human maintainer. Do not launch/control the
  game, start PIE, or implement automated gameplay qualification.
- Source review, mathematical/worldless tests, editor asset checks, builds and
  packaging are permitted. After a coherent C++ change, check the Linux editor
  build and Windows package. These do not establish gameplay compatibility.
- Author Blender models through MCP for Blender. Preserve V2 geometry, UVs and
  mechanical pivots when changing export metadata.
- Produce native persistent buildings; keep custom preview/input/display state
  transient. Preserve SML network checks and native cost/placement validation.
- Preserve saves, blueprints, presets, settings and unrelated mods. Local
  plugin-only installation is permitted with a backup/receipt and the game closed.
- Do not push, upload or publish unless explicitly requested. Keep private paths,
  personal identifiers, credentials and generated review material out of source.
