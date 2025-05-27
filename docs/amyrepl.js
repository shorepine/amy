// amyrepl.js
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
//  amy_get_input_buffer = am.cwrap(
//    'amy_get_input_buffer', null, ['number']
//  );
//  amy_set_input_buffer = am.cwrap(
//    'amy_set_external_input_buffer', null, ['number']
//  );
  amy_process_single_midi_byte = am.cwrap(
    'amy_process_single_midi_byte', null, ['number, number']
  );
  amy_start_web_no_synths();
  amy_module = am;
});

async function start_python() {
  // Let micropython call an exported AMY function
  await mp.registerJsModule('amy_js_message', amy_add_message);

  // time.sleep on this would block the browser from executing anything, so we override it to a JS thing
  mp.registerJsModule("time", {
    sleep: async (s) => await new Promise((r) => setTimeout(r, s * 1000)),
  });

  // Set up the micropython context, like _boot.py. 
  await mp.runPythonAsync(`
    import amy, amy_js_message, time
    amy.override_send = amy_js_message
  `);
}

async function run_async(code) {
  await mp.runPythonAsync(code);
}

async function start_python_and_audio() {
  // Don't run this twice
  if(everything_started) return;
  // Start the audio worklet (miniaudio)
  await amy_live_start_web();
  await start_python();
  // Wait 200ms on first run only before playing amy commands back to avoid clicks
  await new Promise((r) => setTimeout(r, 200));
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
  amy_reset_sysclock();
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




