@tool
extends EditorScript
## AMY Synthesizer Addon - Web Export Setup
##
## Run this script once (via File > Run) to copy web export files
## to the correct locations in your project.
##
## It copies:
##   - enable-threads.js  → project root (service worker needs root scope)
##   - web_audio/          → project root (referenced by custom HTML shell)
##   - Sets up export preset if one doesn't exist

func _run() -> void:
	var addon_dir: String = "res://addons/amy/web/"
	var dst_web: String = "res://web_audio/"
	var dst_root: String = "res://"

	print("=== AMY Web Export Setup ===")

	# Copy enable-threads.js to project root
	_copy_file(addon_dir + "enable-threads-root.js", dst_root + "enable-threads.js")

	# Copy web audio files
	var web_files: Array[String] = [
		"amy.js", "amy.wasm", "amy.aw.js", "amy.ww.js",
		"godot_amy_bridge.js", "enable-threads.js"
	]

	# Create web_audio directory
	DirAccess.make_dir_absolute(ProjectSettings.globalize_path(dst_web))

	# Create .gdignore in web_audio
	var gdignore: FileAccess = FileAccess.open(dst_web + ".gdignore", FileAccess.WRITE)
	if gdignore:
		gdignore.close()
		print("  Created web_audio/.gdignore")

	for fname in web_files:
		_copy_file(addon_dir + fname, dst_web + fname)

	# Copy custom HTML shell to export/
	DirAccess.make_dir_absolute(ProjectSettings.globalize_path("res://export/"))
	_copy_file(addon_dir + "custom_shell.html", "res://export/custom_shell.html")

	print("")
	print("=== Setup Complete ===")
	print("")
	print("Next steps:")
	print("  1. In Export dialog, set Custom HTML Shell to: res://export/custom_shell.html")
	print("  2. Set Exclude Filter to: addons/amy/bin/*,addons/amy/src/*,addons/amy/amy_src/*,addons/amy/web/*,addons/amy/SConstruct,addons/amy/install.gd,addons/amy/amy.gdextension")
	print("  3. Export for Web as usual!")
	print("")

func _copy_file(src: String, dst: String) -> void:
	var src_path: String = ProjectSettings.globalize_path(src)
	var dst_path: String = ProjectSettings.globalize_path(dst)

	var file_in: FileAccess = FileAccess.open(src, FileAccess.READ)
	if file_in == null:
		push_warning("Could not read: " + src)
		return

	var content: PackedByteArray = file_in.get_buffer(file_in.get_length())
	file_in.close()

	var file_out: FileAccess = FileAccess.open(dst, FileAccess.WRITE)
	if file_out == null:
		push_warning("Could not write: " + dst)
		return

	file_out.store_buffer(content)
	file_out.close()
	print("  Copied: " + src.get_file() + " -> " + dst)
