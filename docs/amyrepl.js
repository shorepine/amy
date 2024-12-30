// amyrepl.js
var amy_play_message = null;
var amy_live_start = null;
var amy_reset_sysclock = null
var amy_module = null;
var everything_started = false;
var mp = null;
var term = null;
var editors = [];

// Once AMY module is loaded, do...
amyModule().then(async function(am) {
  amy_module = am;
  amy_live_start = amy_module.cwrap(
    'amy_live_start', null, null, {async: true}    
  );
  amy_start = amy_module.cwrap(
    'amy_start', null, ['number', 'number', 'number']
  );
  amy_play_message = amy_module.cwrap(
    'amy_play_message', null, ['string']
  );
  amy_reset_sysclock = amy_module.cwrap(
    'amy_reset_sysclock', null, null
  );
  amy_start(1,1,1,1);
});

async function start_term_and_repl() {
  // Clear the terminal
  await term.clear();
  // Tell MP to start serving a REPL
  await mp.replInit();
}

async function start_python() {
  // Let micropython call an exported AMY function
  await mp.registerJsModule('amy_js_message', amy_play_message);

  // time.sleep on this would block the browser from executing anything, so we override it to a JS thing
  mp.registerJsModule("time", {
    sleep: async (s) => await new Promise((r) => setTimeout(r, s * 1000)),
  });

  // Set up the micropython context, like _boot.py. 
  await mp.runPythonAsync(`
    import amy, amy_js_message, time, tulip_piano
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
  await amy_live_start();
  await start_python();
  // Wait 200ms on first run only before playing amy commands back to avoid clicks
  await new Promise((r) => setTimeout(r, 200));
  everything_started = true;
}

async function resetAMY() {
  if(everything_started) {
    await mp.runPythonAsync('amy.reset()\n');
  }
}      

async function runCodeBlock(index) {
  if(!everything_started) await start_python_and_audio();
  var py = editors[index].getValue();
  console.log(py)
  amy_reset_sysclock();
  await mp.runPythonAsync(py);
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
      <button type="button" class="btn btn-sm btn-danger" onclick="resetAMY()">◼︎</button> 
    </div>
  </div>`;

  const editor = CodeMirror.fromTextArea(document.getElementById(`code-${index}`), { 
    mode: { 
      name: "python", 
      version: 3, 
      singleLineStringErrors: false
    }, 
    lineNumbers: false, 
    indentUnit: 4, 
    matchBrackets: true
  }); 
  editor.setSize(null,200);
  editor.setValue(code.trim()); 
  editors.push(editor);
}



