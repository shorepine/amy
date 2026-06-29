# AMY Development Notes

## Releases

Releases are cut **automatically on every merge to `main`** by `.github/workflows/release.yml`:
1. It bumps the **patch** number in `library.properties` (e.g. `1.2.7` → `1.2.8`). Because `main` is protected (PR required, `enforce_admins` on), the bump can't be pushed directly — the workflow opens and merges a `bump-X` PR with the built-in `GITHUB_TOKEN` (whose pushes/merges don't re-trigger workflows, so it can't loop).
2. It creates the GitHub release with a plain version tag (e.g. `1.2.8`, NOT `v1.2.8`) — Arduino requires this format — which the Arduino Library Manager picks up.
3. The Godot addon build (`godot-addon.yml`) attaches `amy-godot-addon.zip`. A `GITHUB_TOKEN`-created tag can't fire `godot-addon.yml`'s `push: tags` trigger, so the release workflow instead dispatches it via `workflow_dispatch` at the new tag (the one event `GITHUB_TOKEN` *can* trigger). No PAT is involved.

The bump PR also carries **freshly rebuilt web assets**: the workflow runs `make deploy-web` and stages `docs/amy.js` / `amy.wasm` into that same bump commit, so the tagged release — and the Godot zip, which copies `docs/amy.*` — always ship web built from that exact source. (`docs/enable-threads.js` is static and the MicroPython `docs/micropython.*` REPL files are a separate build; neither is rebuilt here.)

To **skip** a release for a given merge, put `[skip release]` in the merge commit message. To cut a release manually, the same three steps work by hand (bump `library.properties` via PR, `gh release create <ver>`, then the Godot build).

## Godot GDExtension

- Local builds use `setup_godot.sh` which generates the SConstruct, C++ source, and `amy.gdextension` inline, and copies the GDScript wrapper straight from `godot/amy.gd` (it no longer embeds its own copy)
- CI builds use the `godot/` directory (SConstruct, src/, etc.) directly via `.github/workflows/godot-addon.yml`
- **The `send()` kwarg map in `godot/amy.gd` is generated, not hand-edited.** `scripts/gen_amy_gd_api.py` mirrors `amy/__init__.py`'s `_KW_MAP_LIST` (the source of truth, shared with the JS build's `gen_amy_js_api.py`) into the `# BEGIN/END GENERATED` region. Run `make godot-api` after changing `_KW_MAP_LIST`; it's regenerated on release (`release.yml`) and a `c-cpp.yml` job fails any PR where it (or its GDScript syntax) has drifted
- `amy_midi.c` is excluded from the Godot build because it conflicts with platform stubs. Any new functions added to `amy_midi.c` (or other excluded files) that are called from core AMY code need stub implementations added in both:
  - `godot/src/amy_platform_stubs.c` (for CI builds)
  - The `amy_platform_stubs.c` section of `setup_godot.sh` (for local builds)
- macOS builds produce a `.framework` bundle; the `gh auth` token needs `workflow` scope to push workflow file changes

## Web REPL (tutorial & docs)

The live Python REPL at `docs/tutorial.html` runs MicroPython in the browser with the `amy` Python module frozen in. Updating it requires rebuilding MicroPython from the `shorepine/tulipcc` repo:

1. In `tulipcc`, update the amy submodule to latest: `cd tulipcc && git submodule update --remote amy`
2. Rebuild MicroPython: `cd tulip/amyrepl && make clean && make`
3. Copy the output to amy's docs: `cp build-standard/tulip/obj/micropython.mjs build-standard/tulip/obj/micropython.wasm <amy-repo>/docs/`
4. The browser AMY build (`docs/amy.js`, `docs/amy.wasm`) is now rebuilt and committed automatically on every release by `.github/workflows/release.yml` (it runs `make deploy-web` and folds the output into the version-bump commit it tags), so you don't need `make deploy-web` by hand for a release. Run `cd <amy-repo> && make web && make deploy-web` locally only to preview those changes before merging. This automation covers only `docs/amy.*` — the `docs/micropython.*` REPL files above are still the separate tulipcc build.

The `docs/amy.js` file is a concatenation of the Emscripten build (`build/amy.js`), `src/amy_connector.js`, and `build/amy_api.generated.js`. The JS API is auto-generated from `amy/__init__.py` by `scripts/gen_amy_js_api.py`.

## GitHub Auth

- The `bwhitman` account needs `workflow` scope on its token to push changes to `.github/workflows/` files. Run `gh auth refresh -h github.com -s workflow` if pushes are rejected.
