# AMY Development Notes

## Releases

When creating a new release:
1. Update the version in `library.properties` to match the release tag
2. Create the GitHub release with a plain version tag (e.g. `1.2.3`, NOT `v1.2.3`) — Arduino requires this format
3. The Godot addon build workflow triggers automatically on tag push and attaches `amy-godot-addon.zip` to the release

## Godot GDExtension

- The build is defined in `setup_godot.sh` which generates the SConstruct, C++ source, and GDScript wrapper inline
- `amy_midi.c` is excluded from the Godot build because it conflicts with platform stubs. Any new functions added to `amy_midi.c` that are called from `amy.c` need stub implementations added in the `amy_platform_stubs.c` section of `setup_godot.sh`
- macOS builds produce a `.framework` bundle; the `gh auth` token needs `workflow` scope to push workflow file changes

## GitHub Auth

- The `bwhitman` account needs `workflow` scope on its token to push changes to `.github/workflows/` files. Run `gh auth refresh -h github.com -s workflow` if pushes are rejected.
- SSH pushes to `shorepine/amy` use the `brianklay` key by default; use HTTPS with `gh auth token` for `bwhitman` access.
