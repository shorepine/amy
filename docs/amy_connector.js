// amy_connector.js
// helpers for JS to communicate with AMY, including webMIDI

var amy_add_message = null;
var amy_reset_sysclock = null
var amy_module = null;
var everything_started = false;
var mp = null;
var editors = [];
var run_at_starts = [];
var amy_live_start_web = null;
var audio_started = false;
var amy_sysclock = null;
var amy_module = null;
var midiOutputDevice = null;
var midiInputDevice = null;
var amy_audioin_toggle = false;


// Once AMY module is loaded, register its functions and start AMY (not yet audio, you need to click for that)
amyModule().then(async function(am) {
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
  amy_add_message = am.cwrap(
    'amy_add_message', null, ['string']
  );
  amy_reset_sysclock = am.cwrap(
    'amy_reset_sysclock', null, null
  );
  amy_ticks = am.cwrap(
    'sequencer_ticks', 'number', [null]
  );
  amy_sysclock = am.cwrap(
    'amy_sysclock', 'number', [null]
  );
  amy_process_single_midi_byte = am.cwrap(
    'amy_process_single_midi_byte', null, ['number, number']
  );
  amy_start_web_no_synths();
  amy_module = am;
});

async function run_async(code) {
  await mp.runPythonAsync(code);
}

async function start_python_and_audio() {
  // Don't run this twice
  if(everything_started) return;
  
  // Start the audio worklet (miniaudio)
  if(amy_audioin_toggle) {
      await amy_live_start_web_audioin();
  } else {
      await amy_live_start_web();    
  }
  await sleep_ms(200);
  // Let micropython call an exported AMY function
  await mp.registerJsModule('amy_js_message', amy_add_message);

  // time.sleep on this would block the browser from executing anything, so we override it to a JS thing
  mp.registerJsModule("time", {
    sleep: async (s) => await new Promise((r) => setTimeout(r, s * 1000)),
  });
  await sleep_ms(200);

  // Set up the micropython context, like _boot.py. 
  await mp.runPythonAsync(`
    import amy, amy_js_message, time
    amy.override_send = amy_js_message
  `);

  await sleep_ms(200);
  everything_started = true;
  for(i=0;i<run_at_starts.length;i++) {
    if(run_at_starts[i]) {
      runCodeBlock(i);
    }
  }
}

async function resetAMY() {
  if(everything_started) {
    await mp.runPythonAsync('amy.reset()\n');
  }
}      

async function print_error(text) {
    document.getElementById("python-output-text").innerHTML = "<pre>"+text+"</pre>";
    let myModal = new bootstrap.Modal(document.getElementById('myModal'), {backdrop:false});
    myModal.show();            
};

async function runCodeBlock(index) {
  if(!everything_started) await start_python_and_audio();
  var py = editors[index].getValue();
  await amy_add_message("S16384Z");
  try {
    mp.runPythonAsync(py);
  } catch (e) {
    await print_error(e.message);
  }
}

// Create editor block for notebook mode.
function create_editor(element, index) {
  code = element.textContent;
  element.innerHTML = `
  <div>
    <section class="input">
      <div><textarea id="code-${index}" name="code-${index}"></textarea></div> 
    </section>
    <div class="align-self-center my-3"> 
      <button type="button" class="btn btn-sm btn-success" onclick="runCodeBlock(${index})">►</button> 
      <button type="button" class="btn btn-sm btn-danger" onclick="resetAMY()">Reset</button> 
    </div>
  </div>`;

  const editor = CodeMirror.fromTextArea(document.getElementById(`code-${index}`), { 
    mode: { 
      name: "python", 
      version: 3, 
      singleLineStringErrors: false,
      lint: false
    }, 
    lineNumbers: true, 
    indentUnit: 4, 
    matchBrackets: true,
    spellCheck: false,
    autocorrect: false,
    theme: "solarized dark",
    lint: false,
  }); 

  run_at_start = false;
  if(element.classList.contains("preload-python")) {
    run_at_start = true;
  }
  editor.setSize(null,200);
  editor.setValue(code.trim()); 
  run_at_starts.push(run_at_start);
  editors.push(editor);
}


// Called from AMY to update AMYboard about what tick it is, for the sequencer
function amy_sequencer_js_hook(tick) {
    mp.tulipTick(tick);
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


async function sleep_ms(ms) {
    await new Promise((r) => setTimeout(r, ms));
}


async function toggle_audioin() {
    if(!audio_started) await sleep_ms(1000);
    await amy_live_stop();
    if (document.getElementById('amy_audioin').checked) {
        amy_audioin_toggle = true;
        await amy_live_start_web_audioin();
    } else {
        amy_audioin_toggle = false;
        await amy_live_start_web();
    }
}

