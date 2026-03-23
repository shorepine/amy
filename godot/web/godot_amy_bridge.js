// godot_amy_bridge.js
// Thin bridge between Godot's JavaScriptBridge and AMY's WASM module.
// AMY handles its own audio output via AudioWorklet on web —
// this bridge just provides init/send functions for GDScript to call.

(function() {
    var _audio_started = false;

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

    // Check if AMY WASM module is initialized and ready
    window.godot_amy_is_ready = function() {
        return typeof amy_add_message === 'function';
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
