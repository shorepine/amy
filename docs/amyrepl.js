
// Manage Micropython and AMY running together, for a web REPL experience
var mp = null;
var editors = [];
var run_at_starts = [];
var python_started = false;


async function start_python_and_amy() {
  await amy_js_start();
  await start_python();
}

async function run_async(code) {
  await mp.runPythonAsync(code);
}


async function start_python() {
  // Don't run this twice
  if(python_started) return;
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
  await sleep_ms(200);
  python_started = true;

  for(i=0;i<run_at_starts.length;i++) {
    if(run_at_starts[i]) {
      await runCodeBlock(i);
    }
  }
}

async function resetAMY() {
  if(python_started && amy_started) {
    await mp.runPythonAsync('amy.reset()\n');
  }
}      

async function print_error(text) {
    document.getElementById("python-output-text").innerHTML = "<pre>"+text+"</pre>";
    let myModal = new bootstrap.Modal(document.getElementById('myModal'), {backdrop:false});
    myModal.show();            
};

async function runCodeBlock(index) {
  // This is the case that someone clicks the green run button as their first click, so we have to wait for python/amy to start up
  if(!(python_started && amy_started)) await sleep_ms(1000);
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
  editor.setSize(null,150);
  editor.setValue(code.trim()); 
  run_at_starts.push(run_at_start);
  editors.push(editor);
}


// Called from AMY to update AMYboard about what tick it is, for the sequencer
function amy_sequencer_js_hook(tick) {
    mp.tulipTick(tick);
}


