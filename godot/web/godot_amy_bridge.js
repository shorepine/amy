// godot_amy_bridge.js
// Thin bridge between Godot's JavaScriptBridge and AMY's WASM module.
// AMY handles its own audio output via AudioWorklet on web —
// this bridge just provides init/send functions for GDScript to call.

(function() {
    var _audio_started = false;
    var _amy_configured = false;

    // Tell amy_connector.js we're in Godot mode — don't auto-start AMY.
    window._amy_godot_bridge = true;

    // Start AMY's AudioWorklet (must be called from user gesture context)
    async function _try_start_audio() {
        if (_audio_started) return;
        if (typeof amy_live_start_web !== 'function') return;
        try {
            await amy_live_start_web();
            _audio_started = true;
            console.log("[AMY Bridge] Audio started via user gesture");
            // Remove listeners once started
            document.removeEventListener('click', _try_start_audio);
            document.removeEventListener('keydown', _try_start_audio);
            document.removeEventListener('touchstart', _try_start_audio);
        } catch(e) {
            console.error("[AMY Bridge] Audio start failed:", e);
        }
    }

    // Register for user gesture events (Web Audio API requirement)
    document.addEventListener('click', _try_start_audio);
    document.addEventListener('keydown', _try_start_audio);
    document.addEventListener('touchstart', _try_start_audio);

    // -- Functions called from GDScript via JavaScriptBridge.eval() --

    // Configure and start AMY with Godot config options.
    // Called from _init_web() before checking ready state.
    // default_synths: bool, startup_bleep: bool
    window.godot_amy_configure = function(default_synths, startup_bleep) {
        if (_amy_configured) return;
        _amy_configured = true;

        // Wait for WASM module to expose the start functions, then call
        function _try_configure() {
            if (window._amy_godot_start_web && window._amy_godot_start_web_no_synths) {
                if (default_synths) {
                    if (!startup_bleep) {
                        // Start with synths but no bleep: use amy_start_web then
                        // immediately silence the bleep osc
                        window._amy_godot_start_web();
                        if (typeof amy_add_message === 'function') {
                            amy_add_message("v0l0");
                        }
                    } else {
                        window._amy_godot_start_web();
                    }
                } else {
                    window._amy_godot_start_web_no_synths();
                }
                console.log("[AMY Bridge] Configured: default_synths=" + default_synths + " startup_bleep=" + startup_bleep);
            } else {
                setTimeout(_try_configure, 50);
            }
        }
        _try_configure();
    };

    // Check if AMY WASM module is initialized and ready
    window.godot_amy_is_ready = function() {
        return _amy_configured && typeof amy_add_message === 'function';
    };

    // Check if AudioWorklet is running
    window.godot_amy_audio_started = function() {
        return _audio_started;
    };

    // Send a wire message to AMY (e.g. "v0w0f440l1")
    window.godot_amy_send = function(msg) {
        if (typeof amy_add_message === 'function') {
            amy_add_message(msg);
        }
    };
})();
