// amy_connector.js
// helpers for JS to communicate with AMY, including webMIDI

var amy_add_message = null;
var amy_c_api = null;
var amy_reset_sysclock = null
var amy_module = null;
var amy_started = false;
var amy_live_start_web = null;
var audio_started = false;
var amy_sysclock = null;
var amy_module = null;
var midiOutputDevice = null;
var midiInputDevice = null;
var amy_audioin_toggle = false;




// Once AMY module is loaded, register its functions and start AMY (not yet audio, you need to click for that)
amyModule().then(async function(am) {
  // Table-driven C API bridge (send_wire, ticks_ms, dump_state, ...) —
  // amy_c_api_bind is defined in amy_c_api.generated.js, concatenated after
  // this file into docs/amy.js (this callback runs long after both load).
  // Exposed on globalThis for hosts like the Godot web export.
  amy_c_api = amy_c_api_bind(am);
  globalThis.amy_c_api = amy_c_api;
  amy_live_start_web = am.cwrap(
    'amy_live_start_web', null, null, {async: true}
  );
  amy_live_start_web_audioin = am.cwrap(
    'amy_live_start_web_audioin', null, null, {async: true}
  );
  amy_live_stop = am.cwrap(
    'amy_live_stop', null,  null, {async: true}
  );
  amy_start_web = am.cwrap(
    'amy_start_web', null, null
  );
  amy_start_web_no_synths = am.cwrap(
    'amy_start_web_no_synths', null, null
  );
  amy_add_message = amy_c_api.send_wire;
  amy_reset_sysclock = amy_c_api.reset_sysclock;
  amy_ticks = amy_c_api.sequencer_ticks;
  amy_sysclock = amy_c_api.ticks_ms;
  amy_process_single_midi_byte = amy_c_api.process_single_midi_byte;
  // If Godot bridge is present, let it control startup with config.
  // Otherwise start with defaults (standalone REPL mode).
  if (typeof window !== 'undefined' && window._amy_godot_bridge) {
    window._amy_godot_start_web = amy_start_web;
    window._amy_godot_start_web_no_synths = amy_start_web_no_synths;
  } else {
    amy_start_web();
  }
  amy_module = am;
});


async function amy_js_start() {
  // Don't run this twice
  if(amy_started) return;
  await start_midi();	
  await sleep_ms(200);
  // Start the audio worklet (miniaudio)
  if (amy_audioin_toggle) {
      await amy_live_start_web_audioin();
  } else {
      await amy_live_start_web();    
  }
  await sleep_ms(200);
  amy_started = true;
}



async function setup_midi_devices() {
    var midi_in = document.amyboard_settings.midi_input;
    var midi_out = document.amyboard_settings.midi_output;
    if(WebMidi.inputs.length > midi_in.selectedIndex) {
      if(midiInputDevice != null) midiInputDevice.destroy();
      midiInputDevice = WebMidi.getInputById(WebMidi.inputs[midi_in.selectedIndex].id);
      midiInputDevice.addListener("midimessage", e => {
        for(byte in e.message.data) {
          amy_process_single_midi_byte(e.message.data[byte], 1);
        }
      });
    }
    if(WebMidi.outputs.length > midi_out.selectedIndex) {
      if(midiOutputDevice != null) midiOutputDevice.destroy();
      midiOutputDevice = WebMidi.getOutputById(WebMidi.outputs[midi_out.selectedIndex].id);
    }
}

async function start_midi() {
  function onEnabled() {
    // Populate the dropdowns
    var midi_in = document.amyboard_settings.midi_input;
    var midi_out = document.amyboard_settings.midi_output;

    if(WebMidi.inputs.length>0) {
      midi_in.options.length = 0;
      WebMidi.inputs.forEach(input => {
        midi_in.options[midi_in.options.length] = new Option("MIDI in: " + input.name);
      });
    }

    if(WebMidi.outputs.length>0) {
      midi_out.options.length = 0;
      WebMidi.outputs.forEach(output => {
        midi_out.options[midi_out.options.length] = new Option("MIDI out: "+ output.name);
      });
    }
    // First run setup 
    setup_midi_devices();
  }
  if(typeof WebMidi != 'undefined') {
    if(WebMidi.supported) {
      WebMidi
        .enable({sysex:true})
        .then(onEnabled)
        .catch(err => console.log("MIDI: " + err));
    } else {
      document.getElementById('midi-input-panel').style.display='none';
      document.getElementById('midi-output-panel').style.display='none';
    }
  }
}


async function sleep_ms(ms) {
    await new Promise((r) => setTimeout(r, ms));
}


async function toggle_audioin() {
    if(!amy_started) await sleep_ms(1000);
    await amy_live_stop();
    if (document.getElementById('amy_audioin').checked) {
        amy_audioin_toggle = true;
        await amy_live_start_web_audioin();
    } else {
        amy_audioin_toggle = false;
        await amy_live_start_web();
    }
}


