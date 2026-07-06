var amyModule = (() => {
  var _scriptName = typeof document != 'undefined' ? document.currentScript?.src : undefined;
  return (
async function(moduleArg = {}) {
  var moduleRtn;

var Module=moduleArg;var readyPromiseResolve,readyPromiseReject;var readyPromise=new Promise((resolve,reject)=>{readyPromiseResolve=resolve;readyPromiseReject=reject});var ENVIRONMENT_IS_WASM_WORKER=globalThis.name=="em-ww";var ENVIRONMENT_IS_AUDIO_WORKLET=typeof AudioWorkletGlobalScope!=="undefined";if(ENVIRONMENT_IS_AUDIO_WORKLET)ENVIRONMENT_IS_WASM_WORKER=true;var ENVIRONMENT_IS_WEB=typeof window=="object";var ENVIRONMENT_IS_WORKER=typeof WorkerGlobalScope!="undefined";var ENVIRONMENT_IS_NODE=typeof process=="object"&&process.versions?.node&&process.type!="renderer";if(ENVIRONMENT_IS_NODE){var worker_threads=require("worker_threads");global.Worker=worker_threads.Worker;ENVIRONMENT_IS_WORKER=!worker_threads.isMainThread;ENVIRONMENT_IS_WASM_WORKER=ENVIRONMENT_IS_WORKER&&worker_threads["workerData"]=="em-ww"}var arguments_=[];var thisProgram="./this.program";var quit_=(status,toThrow)=>{throw toThrow};if(typeof __filename!="undefined"){_scriptName=__filename}else if(ENVIRONMENT_IS_WORKER){_scriptName=self.location.href}var scriptDirectory="";function locateFile(path){if(Module["locateFile"]){return Module["locateFile"](path,scriptDirectory)}return scriptDirectory+path}var readAsync,readBinary;if(ENVIRONMENT_IS_NODE){var fs=require("fs");var nodePath=require("path");scriptDirectory=__dirname+"/";readBinary=filename=>{filename=isFileURI(filename)?new URL(filename):filename;var ret=fs.readFileSync(filename);return ret};readAsync=async(filename,binary=true)=>{filename=isFileURI(filename)?new URL(filename):filename;var ret=fs.readFileSync(filename,binary?undefined:"utf8");return ret};if(process.argv.length>1){thisProgram=process.argv[1].replace(/\\/g,"/")}arguments_=process.argv.slice(2);quit_=(status,toThrow)=>{process.exitCode=status;throw toThrow}}else if(ENVIRONMENT_IS_WEB||ENVIRONMENT_IS_WORKER){try{scriptDirectory=new URL(".",_scriptName).href}catch{}{if(ENVIRONMENT_IS_WORKER){readBinary=url=>{var xhr=new XMLHttpRequest;xhr.open("GET",url,false);xhr.responseType="arraybuffer";xhr.send(null);return new Uint8Array(xhr.response)}}readAsync=async url=>{if(isFileURI(url)){return new Promise((resolve,reject)=>{var xhr=new XMLHttpRequest;xhr.open("GET",url,true);xhr.responseType="arraybuffer";xhr.onload=()=>{if(xhr.status==200||xhr.status==0&&xhr.response){resolve(xhr.response);return}reject(xhr.status)};xhr.onerror=reject;xhr.send(null)})}var response=await fetch(url,{credentials:"same-origin"});if(response.ok){return response.arrayBuffer()}throw new Error(response.status+" : "+response.url)}}}else{}var defaultPrint=console.log.bind(console);var defaultPrintErr=console.error.bind(console);if(ENVIRONMENT_IS_NODE){var utils=require("util");var stringify=a=>typeof a=="object"?utils.inspect(a):a;defaultPrint=(...args)=>fs.writeSync(1,args.map(stringify).join(" ")+"\n");defaultPrintErr=(...args)=>fs.writeSync(2,args.map(stringify).join(" ")+"\n")}var out=defaultPrint;var err=defaultPrintErr;var wasmBinary;var wasmMemory;var wasmModule;var ABORT=false;var EXITSTATUS;var HEAP8,HEAPU8,HEAP16,HEAPU16,HEAP32,HEAPU32,HEAPF32,HEAP64,HEAPU64,HEAPF64;var runtimeInitialized=false;var isFileURI=filename=>filename.startsWith("file://");function growMemViews(){if(wasmMemory.buffer!=HEAP8.buffer){updateMemoryViews()}}var wasmModuleReceived;if(ENVIRONMENT_IS_NODE&&ENVIRONMENT_IS_WASM_WORKER){var parentPort=worker_threads["parentPort"];parentPort.on("message",msg=>global.onmessage?.({data:msg}));Object.assign(globalThis,{self:global,postMessage:msg=>parentPort["postMessage"](msg)})}var wwParams;function startWasmWorker(props){wwParams=props;wasmMemory=props.wasmMemory;updateMemoryViews();wasmModuleReceived(props.wasm);props.wasm=props.memMemory=0}if(ENVIRONMENT_IS_WASM_WORKER&&!ENVIRONMENT_IS_AUDIO_WORKLET){if(ENVIRONMENT_IS_NODE){var wrappedHandlers=new WeakMap;globalThis.onmessage=null;function wrapMsgHandler(h){var f=wrappedHandlers.get(h);if(!f){f=msg=>h({data:msg});wrappedHandlers.set(h,f)}return f}Object.assign(globalThis,{addEventListener:(name,handler)=>parentPort["on"](name,wrapMsgHandler(handler)),removeEventListener:(name,handler)=>parentPort["off"](name,wrapMsgHandler(handler))})}onmessage=d=>{onmessage=null;startWasmWorker(d.data)}}if(ENVIRONMENT_IS_AUDIO_WORKLET){function createWasmAudioWorkletProcessor(audioParams){class WasmAudioWorkletProcessor extends AudioWorkletProcessor{constructor(args){super();let opts=args.processorOptions;this.callback=getWasmTableEntry(opts.callback);this.userData=opts.userData;this.samplesPerChannel=opts.samplesPerChannel}static get parameterDescriptors(){return audioParams}process(inputList,outputList,parameters){let numInputs=inputList.length,numOutputs=outputList.length,numParams=0,i,j,k,dataPtr,bytesPerChannel=this.samplesPerChannel*4,stackMemoryNeeded=(numInputs+numOutputs)*12,oldStackPtr=stackSave(),inputsPtr,outputsPtr,outputDataPtr,paramsPtr,didProduceAudio,paramArray;for(i of inputList)stackMemoryNeeded+=i.length*bytesPerChannel;for(i of outputList)stackMemoryNeeded+=i.length*bytesPerChannel;for(i in parameters)stackMemoryNeeded+=parameters[i].byteLength+8,++numParams;inputsPtr=stackAlloc(stackMemoryNeeded);k=inputsPtr>>2;dataPtr=inputsPtr+numInputs*12;for(i of inputList){(growMemViews(),HEAPU32)[k+0]=i.length;(growMemViews(),HEAPU32)[k+1]=this.samplesPerChannel;(growMemViews(),HEAPU32)[k+2]=dataPtr;k+=3;for(j of i){(growMemViews(),HEAPF32).set(j,dataPtr>>2);dataPtr+=bytesPerChannel}}outputsPtr=dataPtr;k=outputsPtr>>2;outputDataPtr=(dataPtr+=numOutputs*12)>>2;for(i of outputList){(growMemViews(),HEAPU32)[k+0]=i.length;(growMemViews(),HEAPU32)[k+1]=this.samplesPerChannel;(growMemViews(),HEAPU32)[k+2]=dataPtr;k+=3;dataPtr+=bytesPerChannel*i.length}paramsPtr=dataPtr;k=paramsPtr>>2;dataPtr+=numParams*8;for(i=0;paramArray=parameters[i++];){(growMemViews(),HEAPU32)[k+0]=paramArray.length;(growMemViews(),HEAPU32)[k+1]=dataPtr;k+=2;(growMemViews(),HEAPF32).set(paramArray,dataPtr>>2);dataPtr+=paramArray.length*4}if(didProduceAudio=this.callback(numInputs,inputsPtr,numOutputs,outputsPtr,numParams,paramsPtr,this.userData)){for(i of outputList){for(j of i){for(k=0;k<this.samplesPerChannel;++k){j[k]=(growMemViews(),HEAPF32)[outputDataPtr++]}}}}stackRestore(oldStackPtr);return!!didProduceAudio}}return WasmAudioWorkletProcessor}var messagePort;class BootstrapMessages extends AudioWorkletProcessor{constructor(arg){super();startWasmWorker(arg.processorOptions);messagePort=this.port;messagePort.onmessage=async msg=>{let d=msg.data;if(d["_wpn"]){registerProcessor(d["_wpn"],createWasmAudioWorkletProcessor(d.audioParams));messagePort.postMessage({_wsc:d.callback,args:[d.contextHandle,1,d.userData]})}else if(d["_wsc"]){getWasmTableEntry(d["_wsc"])(...d.args)}}}process(){}}registerProcessor("em-bootstrap",BootstrapMessages)}function updateMemoryViews(){var b=wasmMemory.buffer;HEAP8=new Int8Array(b);HEAP16=new Int16Array(b);Module["HEAPU8"]=HEAPU8=new Uint8Array(b);HEAPU16=new Uint16Array(b);HEAP32=new Int32Array(b);HEAPU32=new Uint32Array(b);HEAPF32=new Float32Array(b);HEAPF64=new Float64Array(b);HEAP64=new BigInt64Array(b);HEAPU64=new BigUint64Array(b)}function initMemory(){if(ENVIRONMENT_IS_WASM_WORKER){return}if(Module["wasmMemory"]){wasmMemory=Module["wasmMemory"]}else{var INITIAL_MEMORY=Module["INITIAL_MEMORY"]||134217728;wasmMemory=new WebAssembly.Memory({initial:INITIAL_MEMORY/65536,maximum:32768,shared:true})}updateMemoryViews()}function preRun(){if(Module["preRun"]){if(typeof Module["preRun"]=="function")Module["preRun"]=[Module["preRun"]];while(Module["preRun"].length){addOnPreRun(Module["preRun"].shift())}}callRuntimeCallbacks(onPreRuns)}function initRuntime(){runtimeInitialized=true;if(ENVIRONMENT_IS_WASM_WORKER)return _wasmWorkerInitializeRuntime();wasmExports["D"]()}function postRun(){if(ENVIRONMENT_IS_WASM_WORKER){return}if(Module["postRun"]){if(typeof Module["postRun"]=="function")Module["postRun"]=[Module["postRun"]];while(Module["postRun"].length){addOnPostRun(Module["postRun"].shift())}}callRuntimeCallbacks(onPostRuns)}var runDependencies=0;var dependenciesFulfilled=null;function addRunDependency(id){runDependencies++;Module["monitorRunDependencies"]?.(runDependencies)}function removeRunDependency(id){runDependencies--;Module["monitorRunDependencies"]?.(runDependencies);if(runDependencies==0){if(dependenciesFulfilled){var callback=dependenciesFulfilled;dependenciesFulfilled=null;callback()}}}function abort(what){Module["onAbort"]?.(what);what="Aborted("+what+")";err(what);ABORT=true;what+=". Build with -sASSERTIONS for more info.";var e=new WebAssembly.RuntimeError(what);readyPromiseReject(e);throw e}var wasmBinaryFile;function findWasmBinary(){return locateFile("amy.wasm")}function getBinarySync(file){if(file==wasmBinaryFile&&wasmBinary){return new Uint8Array(wasmBinary)}if(readBinary){return readBinary(file)}throw"both async and sync fetching of the wasm failed"}async function getWasmBinary(binaryFile){if(!wasmBinary){try{var response=await readAsync(binaryFile);return new Uint8Array(response)}catch{}}return getBinarySync(binaryFile)}async function instantiateArrayBuffer(binaryFile,imports){try{var binary=await getWasmBinary(binaryFile);var instance=await WebAssembly.instantiate(binary,imports);return instance}catch(reason){err(`failed to asynchronously prepare wasm: ${reason}`);abort(reason)}}async function instantiateAsync(binary,binaryFile,imports){if(!binary&&typeof WebAssembly.instantiateStreaming=="function"&&!isFileURI(binaryFile)&&!ENVIRONMENT_IS_NODE){try{var response=fetch(binaryFile,{credentials:"same-origin"});var instantiationResult=await WebAssembly.instantiateStreaming(response,imports);return instantiationResult}catch(reason){err(`wasm streaming compile failed: ${reason}`);err("falling back to ArrayBuffer instantiation")}}return instantiateArrayBuffer(binaryFile,imports)}function getWasmImports(){assignWasmImports();return{a:wasmImports}}async function createWasm(){function receiveInstance(instance,module){wasmExports=instance.exports;wasmExports=Asyncify.instrumentWasmExports(wasmExports);wasmTable=wasmExports["G"];wasmModule=module;removeRunDependency("wasm-instantiate");return wasmExports}addRunDependency("wasm-instantiate");function receiveInstantiationResult(result){return receiveInstance(result["instance"],result["module"])}var info=getWasmImports();if(Module["instantiateWasm"]){return new Promise((resolve,reject)=>{Module["instantiateWasm"](info,(mod,inst)=>{resolve(receiveInstance(mod,inst))})})}if(ENVIRONMENT_IS_WASM_WORKER){return new Promise(resolve=>{wasmModuleReceived=module=>{var instance=new WebAssembly.Instance(module,getWasmImports());resolve(receiveInstance(instance,module))}})}wasmBinaryFile??=findWasmBinary();try{var result=await instantiateAsync(wasmBinary,wasmBinaryFile,info);var exports=receiveInstantiationResult(result);return exports}catch(e){readyPromiseReject(e);return Promise.reject(e)}}class ExitStatus{name="ExitStatus";constructor(status){this.message=`Program terminated with exit(${status})`;this.status=status}}var _wasmWorkerDelayedMessageQueue=[];var handleException=e=>{if(e instanceof ExitStatus||e=="unwind"){return EXITSTATUS}quit_(1,e)};var runtimeKeepaliveCounter=0;var keepRuntimeAlive=()=>noExitRuntime||runtimeKeepaliveCounter>0;var _proc_exit=code=>{EXITSTATUS=code;if(!keepRuntimeAlive()){Module["onExit"]?.(code);ABORT=true}quit_(code,new ExitStatus(code))};var exitJS=(status,implicit)=>{EXITSTATUS=status;_proc_exit(status)};var _exit=exitJS;var maybeExit=()=>{if(!keepRuntimeAlive()){try{_exit(EXITSTATUS)}catch(e){handleException(e)}}};var callUserCallback=func=>{if(ABORT){return}try{func();maybeExit()}catch(e){handleException(e)}};var wasmTableMirror=[];var wasmTable;var getWasmTableEntry=funcPtr=>{var func=wasmTableMirror[funcPtr];if(!func){wasmTableMirror[funcPtr]=func=wasmTable.get(funcPtr)}return func};var _wasmWorkerRunPostMessage=e=>{let data=e.data;let wasmCall=data["_wsc"];wasmCall&&callUserCallback(()=>getWasmTableEntry(wasmCall)(...data["x"]))};var _wasmWorkerAppendToQueue=e=>{_wasmWorkerDelayedMessageQueue.push(e)};var _wasmWorkerInitializeRuntime=()=>{noExitRuntime=1;__emscripten_wasm_worker_initialize(wwParams.stackLowestAddress,wwParams.stackSize);__embind_initialize_bindings();if(typeof AudioWorkletGlobalScope==="undefined"){removeEventListener("message",_wasmWorkerAppendToQueue);_wasmWorkerDelayedMessageQueue=_wasmWorkerDelayedMessageQueue.forEach(_wasmWorkerRunPostMessage);addEventListener("message",_wasmWorkerRunPostMessage)}};var callRuntimeCallbacks=callbacks=>{while(callbacks.length>0){callbacks.shift()(Module)}};var onPostRuns=[];var addOnPostRun=cb=>onPostRuns.push(cb);var onPreRuns=[];var addOnPreRun=cb=>onPreRuns.push(cb);var noExitRuntime=true;var stackRestore=val=>__emscripten_stack_restore(val);var stackSave=()=>_emscripten_stack_get_current();var UTF8Decoder=typeof TextDecoder!="undefined"?new TextDecoder:undefined;var UTF8ArrayToString=(heapOrArray,idx=0,maxBytesToRead=NaN)=>{var endIdx=idx+maxBytesToRead;var endPtr=idx;while(heapOrArray[endPtr]&&!(endPtr>=endIdx))++endPtr;if(endPtr-idx>16&&heapOrArray.buffer&&UTF8Decoder){return UTF8Decoder.decode(heapOrArray.buffer instanceof ArrayBuffer?heapOrArray.subarray(idx,endPtr):heapOrArray.slice(idx,endPtr))}var str="";while(idx<endPtr){var u0=heapOrArray[idx++];if(!(u0&128)){str+=String.fromCharCode(u0);continue}var u1=heapOrArray[idx++]&63;if((u0&224)==192){str+=String.fromCharCode((u0&31)<<6|u1);continue}var u2=heapOrArray[idx++]&63;if((u0&240)==224){u0=(u0&15)<<12|u1<<6|u2}else{u0=(u0&7)<<18|u1<<12|u2<<6|heapOrArray[idx++]&63}if(u0<65536){str+=String.fromCharCode(u0)}else{var ch=u0-65536;str+=String.fromCharCode(55296|ch>>10,56320|ch&1023)}}return str};var UTF8ToString=(ptr,maxBytesToRead)=>ptr?UTF8ArrayToString((growMemViews(),HEAPU8),ptr,maxBytesToRead):"";var ___assert_fail=(condition,filename,line,func)=>abort(`Assertion failed: ${UTF8ToString(condition)}, at: `+[filename?UTF8ToString(filename):"unknown filename",line,func?UTF8ToString(func):"unknown function"]);var __abort_js=()=>abort("");var embind_init_charCodes=()=>{var codes=new Array(256);for(var i=0;i<256;++i){codes[i]=String.fromCharCode(i)}embind_charCodes=codes};var embind_charCodes;var readLatin1String=ptr=>{var ret="";var c=ptr;while((growMemViews(),HEAPU8)[c]){ret+=embind_charCodes[(growMemViews(),HEAPU8)[c++]]}return ret};var awaitingDependencies={};var registeredTypes={};var typeDependencies={};var BindingError=class BindingError extends Error{constructor(message){super(message);this.name="BindingError"}};var throwBindingError=message=>{throw new BindingError(message)};function sharedRegisterType(rawType,registeredInstance,options={}){var name=registeredInstance.name;if(!rawType){throwBindingError(`type "${name}" must have a positive integer typeid pointer`)}if(registeredTypes.hasOwnProperty(rawType)){if(options.ignoreDuplicateRegistrations){return}else{throwBindingError(`Cannot register type '${name}' twice`)}}registeredTypes[rawType]=registeredInstance;delete typeDependencies[rawType];if(awaitingDependencies.hasOwnProperty(rawType)){var callbacks=awaitingDependencies[rawType];delete awaitingDependencies[rawType];callbacks.forEach(cb=>cb())}}function registerType(rawType,registeredInstance,options={}){return sharedRegisterType(rawType,registeredInstance,options)}var integerReadValueFromPointer=(name,width,signed)=>{switch(width){case 1:return signed?pointer=>(growMemViews(),HEAP8)[pointer]:pointer=>(growMemViews(),HEAPU8)[pointer];case 2:return signed?pointer=>(growMemViews(),HEAP16)[pointer>>1]:pointer=>(growMemViews(),HEAPU16)[pointer>>1];case 4:return signed?pointer=>(growMemViews(),HEAP32)[pointer>>2]:pointer=>(growMemViews(),HEAPU32)[pointer>>2];case 8:return signed?pointer=>(growMemViews(),HEAP64)[pointer>>3]:pointer=>(growMemViews(),HEAPU64)[pointer>>3];default:throw new TypeError(`invalid integer width (${width}): ${name}`)}};var __embind_register_bigint=(primitiveType,name,size,minRange,maxRange)=>{name=readLatin1String(name);const isUnsignedType=minRange===0n;let fromWireType=value=>value;if(isUnsignedType){const bitSize=size*8;fromWireType=value=>BigInt.asUintN(bitSize,value);maxRange=fromWireType(maxRange)}registerType(primitiveType,{name,fromWireType,toWireType:(destructors,value)=>{if(typeof value=="number"){value=BigInt(value)}return value},argPackAdvance:GenericWireTypeSize,readValueFromPointer:integerReadValueFromPointer(name,size,!isUnsignedType),destructorFunction:null})};var GenericWireTypeSize=8;var __embind_register_bool=(rawType,name,trueValue,falseValue)=>{name=readLatin1String(name);registerType(rawType,{name,fromWireType:function(wt){return!!wt},toWireType:function(destructors,o){return o?trueValue:falseValue},argPackAdvance:GenericWireTypeSize,readValueFromPointer:function(pointer){return this["fromWireType"]((growMemViews(),HEAPU8)[pointer])},destructorFunction:null})};var emval_freelist=[];var emval_handles=[0,1,,1,null,1,true,1,false,1];var __emval_decref=handle=>{if(handle>9&&0===--emval_handles[handle+1]){emval_handles[handle]=undefined;emval_freelist.push(handle)}};var Emval={toValue:handle=>{if(!handle){throwBindingError(`Cannot use deleted val. handle = ${handle}`)}return emval_handles[handle]},toHandle:value=>{switch(value){case undefined:return 2;case null:return 4;case true:return 6;case false:return 8;default:{const handle=emval_freelist.pop()||emval_handles.length;emval_handles[handle]=value;emval_handles[handle+1]=1;return handle}}}};function readPointer(pointer){return this["fromWireType"]((growMemViews(),HEAPU32)[pointer>>2])}var EmValType={name:"emscripten::val",fromWireType:handle=>{var rv=Emval.toValue(handle);__emval_decref(handle);return rv},toWireType:(destructors,value)=>Emval.toHandle(value),argPackAdvance:GenericWireTypeSize,readValueFromPointer:readPointer,destructorFunction:null};var __embind_register_emval=rawType=>registerType(rawType,EmValType);var floatReadValueFromPointer=(name,width)=>{switch(width){case 4:return function(pointer){return this["fromWireType"]((growMemViews(),HEAPF32)[pointer>>2])};case 8:return function(pointer){return this["fromWireType"]((growMemViews(),HEAPF64)[pointer>>3])};default:throw new TypeError(`invalid float width (${width}): ${name}`)}};var __embind_register_float=(rawType,name,size)=>{name=readLatin1String(name);registerType(rawType,{name,fromWireType:value=>value,toWireType:(destructors,value)=>value,argPackAdvance:GenericWireTypeSize,readValueFromPointer:floatReadValueFromPointer(name,size),destructorFunction:null})};var __embind_register_integer=(primitiveType,name,size,minRange,maxRange)=>{name=readLatin1String(name);const isUnsignedType=minRange===0;let fromWireType=value=>value;if(isUnsignedType){var bitshift=32-8*size;fromWireType=value=>value<<bitshift>>>bitshift;maxRange=fromWireType(maxRange)}registerType(primitiveType,{name,fromWireType,toWireType:(destructors,value)=>value,argPackAdvance:GenericWireTypeSize,readValueFromPointer:integerReadValueFromPointer(name,size,minRange!==0),destructorFunction:null})};var __embind_register_memory_view=(rawType,dataTypeIndex,name)=>{var typeMapping=[Int8Array,Uint8Array,Int16Array,Uint16Array,Int32Array,Uint32Array,Float32Array,Float64Array,BigInt64Array,BigUint64Array];var TA=typeMapping[dataTypeIndex];function decodeMemoryView(handle){var size=(growMemViews(),HEAPU32)[handle>>2];var data=(growMemViews(),HEAPU32)[handle+4>>2];return new TA((growMemViews(),HEAP8).buffer,data,size)}name=readLatin1String(name);registerType(rawType,{name,fromWireType:decodeMemoryView,argPackAdvance:GenericWireTypeSize,readValueFromPointer:decodeMemoryView},{ignoreDuplicateRegistrations:true})};var stringToUTF8Array=(str,heap,outIdx,maxBytesToWrite)=>{if(!(maxBytesToWrite>0))return 0;var startIdx=outIdx;var endIdx=outIdx+maxBytesToWrite-1;for(var i=0;i<str.length;++i){var u=str.charCodeAt(i);if(u>=55296&&u<=57343){var u1=str.charCodeAt(++i);u=65536+((u&1023)<<10)|u1&1023}if(u<=127){if(outIdx>=endIdx)break;heap[outIdx++]=u}else if(u<=2047){if(outIdx+1>=endIdx)break;heap[outIdx++]=192|u>>6;heap[outIdx++]=128|u&63}else if(u<=65535){if(outIdx+2>=endIdx)break;heap[outIdx++]=224|u>>12;heap[outIdx++]=128|u>>6&63;heap[outIdx++]=128|u&63}else{if(outIdx+3>=endIdx)break;heap[outIdx++]=240|u>>18;heap[outIdx++]=128|u>>12&63;heap[outIdx++]=128|u>>6&63;heap[outIdx++]=128|u&63}}heap[outIdx]=0;return outIdx-startIdx};var stringToUTF8=(str,outPtr,maxBytesToWrite)=>stringToUTF8Array(str,(growMemViews(),HEAPU8),outPtr,maxBytesToWrite);var lengthBytesUTF8=str=>{var len=0;for(var i=0;i<str.length;++i){var c=str.charCodeAt(i);if(c<=127){len++}else if(c<=2047){len+=2}else if(c>=55296&&c<=57343){len+=4;++i}else{len+=3}}return len};var __embind_register_std_string=(rawType,name)=>{name=readLatin1String(name);var stdStringIsUTF8=true;registerType(rawType,{name,fromWireType(value){var length=(growMemViews(),HEAPU32)[value>>2];var payload=value+4;var str;if(stdStringIsUTF8){var decodeStartPtr=payload;for(var i=0;i<=length;++i){var currentBytePtr=payload+i;if(i==length||(growMemViews(),HEAPU8)[currentBytePtr]==0){var maxRead=currentBytePtr-decodeStartPtr;var stringSegment=UTF8ToString(decodeStartPtr,maxRead);if(str===undefined){str=stringSegment}else{str+=String.fromCharCode(0);str+=stringSegment}decodeStartPtr=currentBytePtr+1}}}else{var a=new Array(length);for(var i=0;i<length;++i){a[i]=String.fromCharCode((growMemViews(),HEAPU8)[payload+i])}str=a.join("")}_free(value);return str},toWireType(destructors,value){if(value instanceof ArrayBuffer){value=new Uint8Array(value)}var length;var valueIsOfTypeString=typeof value=="string";if(!(valueIsOfTypeString||ArrayBuffer.isView(value)&&value.BYTES_PER_ELEMENT==1)){throwBindingError("Cannot pass non-string to std::string")}if(stdStringIsUTF8&&valueIsOfTypeString){length=lengthBytesUTF8(value)}else{length=value.length}var base=_malloc(4+length+1);var ptr=base+4;(growMemViews(),HEAPU32)[base>>2]=length;if(valueIsOfTypeString){if(stdStringIsUTF8){stringToUTF8(value,ptr,length+1)}else{for(var i=0;i<length;++i){var charCode=value.charCodeAt(i);if(charCode>255){_free(base);throwBindingError("String has UTF-16 code units that do not fit in 8 bits")}(growMemViews(),HEAPU8)[ptr+i]=charCode}}}else{(growMemViews(),HEAPU8).set(value,ptr)}if(destructors!==null){destructors.push(_free,base)}return base},argPackAdvance:GenericWireTypeSize,readValueFromPointer:readPointer,destructorFunction(ptr){_free(ptr)}})};var UTF16Decoder=typeof TextDecoder!="undefined"?new TextDecoder("utf-16le"):undefined;var UTF16ToString=(ptr,maxBytesToRead)=>{var idx=ptr>>1;var maxIdx=idx+maxBytesToRead/2;var endIdx=idx;while(!(endIdx>=maxIdx)&&(growMemViews(),HEAPU16)[endIdx])++endIdx;if(endIdx-idx>16&&UTF16Decoder)return UTF16Decoder.decode((growMemViews(),HEAPU16).buffer instanceof ArrayBuffer?(growMemViews(),HEAPU16).subarray(idx,endIdx):(growMemViews(),HEAPU16).slice(idx,endIdx));var str="";for(var i=idx;!(i>=maxIdx);++i){var codeUnit=(growMemViews(),HEAPU16)[i];if(codeUnit==0)break;str+=String.fromCharCode(codeUnit)}return str};var stringToUTF16=(str,outPtr,maxBytesToWrite)=>{maxBytesToWrite??=2147483647;if(maxBytesToWrite<2)return 0;maxBytesToWrite-=2;var startPtr=outPtr;var numCharsToWrite=maxBytesToWrite<str.length*2?maxBytesToWrite/2:str.length;for(var i=0;i<numCharsToWrite;++i){var codeUnit=str.charCodeAt(i);(growMemViews(),HEAP16)[outPtr>>1]=codeUnit;outPtr+=2}(growMemViews(),HEAP16)[outPtr>>1]=0;return outPtr-startPtr};var lengthBytesUTF16=str=>str.length*2;var UTF32ToString=(ptr,maxBytesToRead)=>{var i=0;var str="";while(!(i>=maxBytesToRead/4)){var utf32=(growMemViews(),HEAP32)[ptr+i*4>>2];if(utf32==0)break;++i;if(utf32>=65536){var ch=utf32-65536;str+=String.fromCharCode(55296|ch>>10,56320|ch&1023)}else{str+=String.fromCharCode(utf32)}}return str};var stringToUTF32=(str,outPtr,maxBytesToWrite)=>{maxBytesToWrite??=2147483647;if(maxBytesToWrite<4)return 0;var startPtr=outPtr;var endPtr=startPtr+maxBytesToWrite-4;for(var i=0;i<str.length;++i){var codeUnit=str.charCodeAt(i);if(codeUnit>=55296&&codeUnit<=57343){var trailSurrogate=str.charCodeAt(++i);codeUnit=65536+((codeUnit&1023)<<10)|trailSurrogate&1023}(growMemViews(),HEAP32)[outPtr>>2]=codeUnit;outPtr+=4;if(outPtr+4>endPtr)break}(growMemViews(),HEAP32)[outPtr>>2]=0;return outPtr-startPtr};var lengthBytesUTF32=str=>{var len=0;for(var i=0;i<str.length;++i){var codeUnit=str.charCodeAt(i);if(codeUnit>=55296&&codeUnit<=57343)++i;len+=4}return len};var __embind_register_std_wstring=(rawType,charSize,name)=>{name=readLatin1String(name);var decodeString,encodeString,readCharAt,lengthBytesUTF;if(charSize===2){decodeString=UTF16ToString;encodeString=stringToUTF16;lengthBytesUTF=lengthBytesUTF16;readCharAt=pointer=>(growMemViews(),HEAPU16)[pointer>>1]}else if(charSize===4){decodeString=UTF32ToString;encodeString=stringToUTF32;lengthBytesUTF=lengthBytesUTF32;readCharAt=pointer=>(growMemViews(),HEAPU32)[pointer>>2]}registerType(rawType,{name,fromWireType:value=>{var length=(growMemViews(),HEAPU32)[value>>2];var str;var decodeStartPtr=value+4;for(var i=0;i<=length;++i){var currentBytePtr=value+4+i*charSize;if(i==length||readCharAt(currentBytePtr)==0){var maxReadBytes=currentBytePtr-decodeStartPtr;var stringSegment=decodeString(decodeStartPtr,maxReadBytes);if(str===undefined){str=stringSegment}else{str+=String.fromCharCode(0);str+=stringSegment}decodeStartPtr=currentBytePtr+charSize}}_free(value);return str},toWireType:(destructors,value)=>{if(!(typeof value=="string")){throwBindingError(`Cannot pass non-string to C++ string type ${name}`)}var length=lengthBytesUTF(value);var ptr=_malloc(4+length+charSize);(growMemViews(),HEAPU32)[ptr>>2]=length/charSize;encodeString(value,ptr+4,length+charSize);if(destructors!==null){destructors.push(_free,ptr)}return ptr},argPackAdvance:GenericWireTypeSize,readValueFromPointer:readPointer,destructorFunction(ptr){_free(ptr)}})};var __embind_register_void=(rawType,name)=>{name=readLatin1String(name);registerType(rawType,{isVoid:true,name,argPackAdvance:0,fromWireType:()=>undefined,toWireType:(destructors,o)=>undefined})};var _emscripten_get_now;if(typeof performance!="undefined"&&performance.now){_emscripten_get_now=()=>performance.now()}else{_emscripten_get_now=Date.now}var nowIsMonotonic=typeof performance=="object"&&performance&&typeof performance["now"]=="function";var INT53_MAX=9007199254740992;var INT53_MIN=-9007199254740992;var bigintToI53Checked=num=>num<INT53_MIN||num>INT53_MAX?NaN:Number(num);var readEmAsmArgsArray=[];var readEmAsmArgs=(sigPtr,buf)=>{readEmAsmArgsArray.length=0;var ch;while(ch=(growMemViews(),HEAPU8)[sigPtr++]){var wide=ch!=105;wide&=ch!=112;buf+=wide&&buf%8?4:0;readEmAsmArgsArray.push(ch==112?(growMemViews(),HEAPU32)[buf>>2]:ch==106?(growMemViews(),HEAP64)[buf>>3]:ch==105?(growMemViews(),HEAP32)[buf>>2]:(growMemViews(),HEAPF64)[buf>>3]);buf+=wide?8:4}return readEmAsmArgsArray};var runEmAsmFunction=(code,sigPtr,argbuf)=>{var args=readEmAsmArgs(sigPtr,argbuf);return ASM_CONSTS[code](...args)};var _emscripten_asm_const_double=(code,sigPtr,argbuf)=>runEmAsmFunction(code,sigPtr,argbuf);var _emscripten_asm_const_int=(code,sigPtr,argbuf)=>runEmAsmFunction(code,sigPtr,argbuf);var _emscripten_set_main_loop_timing=(mode,value)=>{MainLoop.timingMode=mode;MainLoop.timingValue=value;if(!MainLoop.func){return 1}if(!MainLoop.running){MainLoop.running=true}if(mode==0){MainLoop.scheduler=function MainLoop_scheduler_setTimeout(){var timeUntilNextTick=Math.max(0,MainLoop.tickStartTime+value-_emscripten_get_now())|0;setTimeout(MainLoop.runner,timeUntilNextTick)};MainLoop.method="timeout"}else if(mode==1){MainLoop.scheduler=function MainLoop_scheduler_rAF(){MainLoop.requestAnimationFrame(MainLoop.runner)};MainLoop.method="rAF"}else if(mode==2){if(typeof MainLoop.setImmediate=="undefined"){if(typeof setImmediate=="undefined"){var setImmediates=[];var emscriptenMainLoopMessageId="setimmediate";var MainLoop_setImmediate_messageHandler=event=>{if(event.data===emscriptenMainLoopMessageId||event.data.target===emscriptenMainLoopMessageId){event.stopPropagation();setImmediates.shift()()}};addEventListener("message",MainLoop_setImmediate_messageHandler,true);MainLoop.setImmediate=func=>{setImmediates.push(func);if(ENVIRONMENT_IS_WORKER){Module["setImmediates"]??=[];Module["setImmediates"].push(func);postMessage({target:emscriptenMainLoopMessageId})}else postMessage(emscriptenMainLoopMessageId,"*")}}else{MainLoop.setImmediate=setImmediate}}MainLoop.scheduler=function MainLoop_scheduler_setImmediate(){MainLoop.setImmediate(MainLoop.runner)};MainLoop.method="immediate"}return 0};var setMainLoop=(iterFunc,fps,simulateInfiniteLoop,arg,noSetTiming)=>{MainLoop.func=iterFunc;MainLoop.arg=arg;var thisMainLoopId=MainLoop.currentlyRunningMainloop;function checkIsRunning(){if(thisMainLoopId<MainLoop.currentlyRunningMainloop){maybeExit();return false}return true}MainLoop.running=false;MainLoop.runner=function MainLoop_runner(){if(ABORT)return;if(MainLoop.queue.length>0){var start=Date.now();var blocker=MainLoop.queue.shift();blocker.func(blocker.arg);if(MainLoop.remainingBlockers){var remaining=MainLoop.remainingBlockers;var next=remaining%1==0?remaining-1:Math.floor(remaining);if(blocker.counted){MainLoop.remainingBlockers=next}else{next=next+.5;MainLoop.remainingBlockers=(8*remaining+next)/9}}MainLoop.updateStatus();if(!checkIsRunning())return;setTimeout(MainLoop.runner,0);return}if(!checkIsRunning())return;MainLoop.currentFrameNumber=MainLoop.currentFrameNumber+1|0;if(MainLoop.timingMode==1&&MainLoop.timingValue>1&&MainLoop.currentFrameNumber%MainLoop.timingValue!=0){MainLoop.scheduler();return}else if(MainLoop.timingMode==0){MainLoop.tickStartTime=_emscripten_get_now()}MainLoop.runIter(iterFunc);if(!checkIsRunning())return;MainLoop.scheduler()};if(!noSetTiming){if(fps>0){_emscripten_set_main_loop_timing(0,1e3/fps)}else{_emscripten_set_main_loop_timing(1,1)}MainLoop.scheduler()}if(simulateInfiniteLoop){throw"unwind"}};var MainLoop={running:false,scheduler:null,method:"",currentlyRunningMainloop:0,func:null,arg:0,timingMode:0,timingValue:0,currentFrameNumber:0,queue:[],preMainLoop:[],postMainLoop:[],pause(){MainLoop.scheduler=null;MainLoop.currentlyRunningMainloop++},resume(){MainLoop.currentlyRunningMainloop++;var timingMode=MainLoop.timingMode;var timingValue=MainLoop.timingValue;var func=MainLoop.func;MainLoop.func=null;setMainLoop(func,0,false,MainLoop.arg,true);_emscripten_set_main_loop_timing(timingMode,timingValue);MainLoop.scheduler()},updateStatus(){if(Module["setStatus"]){var message=Module["statusMessage"]||"Please wait...";var remaining=MainLoop.remainingBlockers??0;var expected=MainLoop.expectedBlockers??0;if(remaining){if(remaining<expected){Module["setStatus"](`{message} ({expected - remaining}/{expected})`)}else{Module["setStatus"](message)}}else{Module["setStatus"]("")}}},init(){Module["preMainLoop"]&&MainLoop.preMainLoop.push(Module["preMainLoop"]);Module["postMainLoop"]&&MainLoop.postMainLoop.push(Module["postMainLoop"])},runIter(func){if(ABORT)return;for(var pre of MainLoop.preMainLoop){if(pre()===false){return}}callUserCallback(func);for(var post of MainLoop.postMainLoop){post()}},nextRAF:0,fakeRequestAnimationFrame(func){var now=Date.now();if(MainLoop.nextRAF===0){MainLoop.nextRAF=now+1e3/60}else{while(now+2>=MainLoop.nextRAF){MainLoop.nextRAF+=1e3/60}}var delay=Math.max(MainLoop.nextRAF-now,0);setTimeout(func,delay)},requestAnimationFrame(func){if(typeof requestAnimationFrame=="function"){requestAnimationFrame(func);return}var RAF=MainLoop.fakeRequestAnimationFrame;RAF(func)}};var _emscripten_cancel_main_loop=()=>{MainLoop.pause();MainLoop.func=null};var EmAudio={};var EmAudioCounter=0;var emscriptenRegisterAudioObject=object=>{EmAudio[++EmAudioCounter]=object;return EmAudioCounter};var emscriptenGetAudioObject=objectHandle=>EmAudio[objectHandle];var _emscripten_create_audio_context=options=>{let ctx=window.AudioContext||window.webkitAudioContext;options>>=2;let opts=options?{latencyHint:(growMemViews(),HEAPU32)[options]?UTF8ToString((growMemViews(),HEAPU32)[options]):void 0,sampleRate:(growMemViews(),HEAP32)[options+1]||void 0}:void 0;return ctx&&emscriptenRegisterAudioObject(new ctx(opts))};var emscriptenGetContextQuantumSize=contextHandle=>128;var _emscripten_create_wasm_audio_worklet_node=(contextHandle,name,options,callback,userData)=>{options>>=2;function readChannelCountArray(heapIndex,numOutputs){let channelCounts=[];while(numOutputs--)channelCounts.push((growMemViews(),HEAPU32)[heapIndex++]);return channelCounts}let opts=options?{numberOfInputs:(growMemViews(),HEAP32)[options],numberOfOutputs:(growMemViews(),HEAP32)[options+1],outputChannelCount:(growMemViews(),HEAPU32)[options+2]?readChannelCountArray((growMemViews(),HEAPU32)[options+2]>>2,(growMemViews(),HEAP32)[options+1]):void 0,processorOptions:{callback,userData,samplesPerChannel:emscriptenGetContextQuantumSize(contextHandle)}}:void 0;return emscriptenRegisterAudioObject(new AudioWorkletNode(EmAudio[contextHandle],UTF8ToString(name),opts))};var _emscripten_create_wasm_audio_worklet_processor_async=(contextHandle,options,callback,userData)=>{options>>=2;let audioParams=[],numAudioParams=(growMemViews(),HEAPU32)[options+1],audioParamDescriptors=(growMemViews(),HEAPU32)[options+2]>>2,i=0;while(numAudioParams--){audioParams.push({name:i++,defaultValue:(growMemViews(),HEAPF32)[audioParamDescriptors++],minValue:(growMemViews(),HEAPF32)[audioParamDescriptors++],maxValue:(growMemViews(),HEAPF32)[audioParamDescriptors++],automationRate:["a","k"][(growMemViews(),HEAPU32)[audioParamDescriptors++]]+"-rate"})}EmAudio[contextHandle].audioWorklet.bootstrapMessage.port.postMessage({_wpn:UTF8ToString((growMemViews(),HEAPU32)[options]),audioParams,contextHandle,callback,userData})};var _emscripten_destroy_audio_context=contextHandle=>{EmAudio[contextHandle].suspend();delete EmAudio[contextHandle]};var _emscripten_destroy_web_audio_node=objectHandle=>{EmAudio[objectHandle].disconnect();delete EmAudio[objectHandle]};var getHeapMax=()=>2147483648;var alignMemory=(size,alignment)=>Math.ceil(size/alignment)*alignment;var growMemory=size=>{var b=wasmMemory.buffer;var pages=(size-b.byteLength+65535)/65536|0;try{wasmMemory.grow(pages);updateMemoryViews();return 1}catch(e){}};var _emscripten_resize_heap=requestedSize=>{var oldSize=(growMemViews(),HEAPU8).length;requestedSize>>>=0;if(requestedSize<=oldSize){return false}var maxHeapSize=getHeapMax();if(requestedSize>maxHeapSize){return false}for(var cutDown=1;cutDown<=4;cutDown*=2){var overGrownHeapSize=oldSize*(1+.2/cutDown);overGrownHeapSize=Math.min(overGrownHeapSize,requestedSize+100663296);var newSize=Math.min(maxHeapSize,alignMemory(Math.max(requestedSize,overGrownHeapSize),65536));var replacement=growMemory(newSize);if(replacement){return true}}return false};var _emscripten_set_main_loop=(func,fps,simulateInfiniteLoop)=>{var iterFunc=()=>dynCall_v(func);setMainLoop(iterFunc,fps,simulateInfiniteLoop)};var safeSetTimeout=(func,timeout)=>setTimeout(()=>{callUserCallback(func)},timeout);var _emscripten_sleep=ms=>Asyncify.handleSleep(wakeUp=>safeSetTimeout(wakeUp,ms));_emscripten_sleep.isAsync=true;var _wasmWorkersID=1;var _EmAudioDispatchProcessorCallback=e=>{let data=e.data;let wasmCall=data["_wsc"];wasmCall&&getWasmTableEntry(wasmCall)(...data.args)};var stackAlloc=sz=>__emscripten_stack_alloc(sz);var _emscripten_start_wasm_audio_worklet_thread_async=(contextHandle,stackLowestAddress,stackSize,callback,userData)=>{let audioContext=EmAudio[contextHandle],audioWorklet=audioContext.audioWorklet;let audioWorkletCreationFailed=()=>{((a1,a2,a3)=>dynCall_viii(callback,a1,a2,a3))(contextHandle,0,userData)};if(!audioWorklet){return audioWorkletCreationFailed()}audioWorklet.addModule(locateFile("amy.js")).then(()=>{audioWorklet.bootstrapMessage=new AudioWorkletNode(audioContext,"em-bootstrap",{processorOptions:{wwID:_wasmWorkersID++,wasm:wasmModule,wasmMemory,stackLowestAddress,stackSize}});audioWorklet.bootstrapMessage.port.onmessage=_EmAudioDispatchProcessorCallback;((a1,a2,a3)=>dynCall_viii(callback,a1,a2,a3))(contextHandle,1,userData)}).catch(audioWorkletCreationFailed)};var _fd_close=fd=>52;function _fd_seek(fd,offset,whence,newOffset){offset=bigintToI53Checked(offset);return 70}var printCharBuffers=[null,[],[]];var printChar=(stream,curr)=>{var buffer=printCharBuffers[stream];if(curr===0||curr===10){(stream===1?out:err)(UTF8ArrayToString(buffer));buffer.length=0}else{buffer.push(curr)}};var _fd_write=(fd,iov,iovcnt,pnum)=>{var num=0;for(var i=0;i<iovcnt;i++){var ptr=(growMemViews(),HEAPU32)[iov>>2];var len=(growMemViews(),HEAPU32)[iov+4>>2];iov+=8;for(var j=0;j<len;j++){printChar(fd,(growMemViews(),HEAPU8)[ptr+j])}num+=len}(growMemViews(),HEAPU32)[pnum>>2]=num;return 0};var runAndAbortIfError=func=>{try{return func()}catch(e){abort(e)}};var runtimeKeepalivePush=()=>{runtimeKeepaliveCounter+=1};var runtimeKeepalivePop=()=>{runtimeKeepaliveCounter-=1};var Asyncify={instrumentWasmImports(imports){var importPattern=/^(invoke_.*|__asyncjs__.*)$/;for(let[x,original]of Object.entries(imports)){if(typeof original=="function"){let isAsyncifyImport=original.isAsync||importPattern.test(x)}}},instrumentWasmExports(exports){var ret={};for(let[x,original]of Object.entries(exports)){if(typeof original=="function"){ret[x]=(...args)=>{Asyncify.exportCallStack.push(x);try{return original(...args)}finally{if(!ABORT){var y=Asyncify.exportCallStack.pop();Asyncify.maybeStopUnwind()}}}}else{ret[x]=original}}return ret},State:{Normal:0,Unwinding:1,Rewinding:2,Disabled:3},state:0,StackSize:128e3,currData:null,handleSleepReturnValue:0,exportCallStack:[],callStackNameToId:{},callStackIdToName:{},callStackId:0,asyncPromiseHandlers:null,sleepCallbacks:[],getCallStackId(funcName){var id=Asyncify.callStackNameToId[funcName];if(id===undefined){id=Asyncify.callStackId++;Asyncify.callStackNameToId[funcName]=id;Asyncify.callStackIdToName[id]=funcName}return id},maybeStopUnwind(){if(Asyncify.currData&&Asyncify.state===Asyncify.State.Unwinding&&Asyncify.exportCallStack.length===0){Asyncify.state=Asyncify.State.Normal;runAndAbortIfError(_asyncify_stop_unwind);if(typeof Fibers!="undefined"){Fibers.trampoline()}}},whenDone(){return new Promise((resolve,reject)=>{Asyncify.asyncPromiseHandlers={resolve,reject}})},allocateData(){var ptr=_malloc(12+Asyncify.StackSize);Asyncify.setDataHeader(ptr,ptr+12,Asyncify.StackSize);Asyncify.setDataRewindFunc(ptr);return ptr},setDataHeader(ptr,stack,stackSize){(growMemViews(),HEAPU32)[ptr>>2]=stack;(growMemViews(),HEAPU32)[ptr+4>>2]=stack+stackSize},setDataRewindFunc(ptr){var bottomOfCallStack=Asyncify.exportCallStack[0];var rewindId=Asyncify.getCallStackId(bottomOfCallStack);(growMemViews(),HEAP32)[ptr+8>>2]=rewindId},getDataRewindFuncName(ptr){var id=(growMemViews(),HEAP32)[ptr+8>>2];var name=Asyncify.callStackIdToName[id];return name},getDataRewindFunc(name){var func=wasmExports[name];return func},doRewind(ptr){var name=Asyncify.getDataRewindFuncName(ptr);var func=Asyncify.getDataRewindFunc(name);return func()},handleSleep(startAsync){if(ABORT)return;if(Asyncify.state===Asyncify.State.Normal){var reachedCallback=false;var reachedAfterCallback=false;startAsync((handleSleepReturnValue=0)=>{if(ABORT)return;Asyncify.handleSleepReturnValue=handleSleepReturnValue;reachedCallback=true;if(!reachedAfterCallback){return}Asyncify.state=Asyncify.State.Rewinding;runAndAbortIfError(()=>_asyncify_start_rewind(Asyncify.currData));if(typeof MainLoop!="undefined"&&MainLoop.func){MainLoop.resume()}var asyncWasmReturnValue,isError=false;try{asyncWasmReturnValue=Asyncify.doRewind(Asyncify.currData)}catch(err){asyncWasmReturnValue=err;isError=true}var handled=false;if(!Asyncify.currData){var asyncPromiseHandlers=Asyncify.asyncPromiseHandlers;if(asyncPromiseHandlers){Asyncify.asyncPromiseHandlers=null;(isError?asyncPromiseHandlers.reject:asyncPromiseHandlers.resolve)(asyncWasmReturnValue);handled=true}}if(isError&&!handled){throw asyncWasmReturnValue}});reachedAfterCallback=true;if(!reachedCallback){Asyncify.state=Asyncify.State.Unwinding;Asyncify.currData=Asyncify.allocateData();if(typeof MainLoop!="undefined"&&MainLoop.func){MainLoop.pause()}runAndAbortIfError(()=>_asyncify_start_unwind(Asyncify.currData))}}else if(Asyncify.state===Asyncify.State.Rewinding){Asyncify.state=Asyncify.State.Normal;runAndAbortIfError(_asyncify_stop_rewind);_free(Asyncify.currData);Asyncify.currData=null;Asyncify.sleepCallbacks.forEach(callUserCallback)}else{abort(`invalid state: ${Asyncify.state}`)}return Asyncify.handleSleepReturnValue},handleAsync:startAsync=>Asyncify.handleSleep(wakeUp=>{startAsync().then(wakeUp)})};var getCFunc=ident=>{var func=Module["_"+ident];return func};var writeArrayToMemory=(array,buffer)=>{(growMemViews(),HEAP8).set(array,buffer)};var stringToUTF8OnStack=str=>{var size=lengthBytesUTF8(str)+1;var ret=stackAlloc(size);stringToUTF8(str,ret,size);return ret};var ccall=(ident,returnType,argTypes,args,opts)=>{var toC={string:str=>{var ret=0;if(str!==null&&str!==undefined&&str!==0){ret=stringToUTF8OnStack(str)}return ret},array:arr=>{var ret=stackAlloc(arr.length);writeArrayToMemory(arr,ret);return ret}};function convertReturnValue(ret){if(returnType==="string"){return UTF8ToString(ret)}if(returnType==="boolean")return Boolean(ret);return ret}var func=getCFunc(ident);var cArgs=[];var stack=0;if(args){for(var i=0;i<args.length;i++){var converter=toC[argTypes[i]];if(converter){if(stack===0)stack=stackSave();cArgs[i]=converter(args[i])}else{cArgs[i]=args[i]}}}var previousAsync=Asyncify.currData;var ret=func(...cArgs);function onDone(ret){runtimeKeepalivePop();if(stack!==0)stackRestore(stack);return convertReturnValue(ret)}var asyncMode=opts?.async;runtimeKeepalivePush();if(Asyncify.currData!=previousAsync){return Asyncify.whenDone().then(onDone)}ret=onDone(ret);if(asyncMode)return Promise.resolve(ret);return ret};var cwrap=(ident,returnType,argTypes,opts)=>{var numericArgs=!argTypes||argTypes.every(type=>type==="number"||type==="boolean");var numericRet=returnType!=="string";if(numericRet&&numericArgs&&!opts){return getCFunc(ident)}return(...args)=>ccall(ident,returnType,argTypes,args,opts)};embind_init_charCodes();Module["requestAnimationFrame"]=MainLoop.requestAnimationFrame;Module["pauseMainLoop"]=MainLoop.pause;Module["resumeMainLoop"]=MainLoop.resume;MainLoop.init();{initMemory();if(Module["noExitRuntime"])noExitRuntime=Module["noExitRuntime"];if(Module["print"])out=Module["print"];if(Module["printErr"])err=Module["printErr"];if(Module["wasmBinary"])wasmBinary=Module["wasmBinary"];if(Module["arguments"])arguments_=Module["arguments"];if(Module["thisProgram"])thisProgram=Module["thisProgram"]}Module["ccall"]=ccall;Module["cwrap"]=cwrap;var ASM_CONSTS={4498612:($0,$1)=>{if(typeof amy_shared_open==="function"){return amy_shared_open(UTF8ToString($0),UTF8ToString($1))}return 0},4498733:$0=>{if(typeof amy_shared_close==="function"){amy_shared_close($0)}},4498807:($0,$1,$2,$3)=>{if(typeof amy_shared_read==="function"){return amy_shared_read($0,$1,$2,$3)}return 0},4498908:($0,$1,$2)=>{if(typeof amy_shared_write==="function"){return amy_shared_write($0,$1,$2)}return 0},4499007:$0=>{if(typeof amy_shared_close==="function"){amy_shared_close($0)}},4499081:$0=>{if(typeof amy_sequencer_js_hook==="function"){amy_sequencer_js_hook($0)}},4499164:()=>{if(typeof cv_1_voltage!="undefined"){return cv_1_voltage}else{return 0}},4499251:()=>{if(typeof cv_2_voltage!="undefined"){return cv_2_voltage}else{return 0}},4499338:($0,$1,$2,$3,$4)=>{if(typeof window==="undefined"||(window.AudioContext||window.webkitAudioContext)===undefined){return 0}if(typeof window.miniaudio==="undefined"){window.miniaudio={referenceCount:0};window.miniaudio.device_type={};window.miniaudio.device_type.playback=$0;window.miniaudio.device_type.capture=$1;window.miniaudio.device_type.duplex=$2;window.miniaudio.device_state={};window.miniaudio.device_state.stopped=$3;window.miniaudio.device_state.started=$4;miniaudio.devices=[];miniaudio.track_device=function(device){for(var iDevice=0;iDevice<miniaudio.devices.length;++iDevice){if(miniaudio.devices[iDevice]==null){miniaudio.devices[iDevice]=device;return iDevice}}miniaudio.devices.push(device);return miniaudio.devices.length-1};miniaudio.untrack_device_by_index=function(deviceIndex){miniaudio.devices[deviceIndex]=null;while(miniaudio.devices.length>0){if(miniaudio.devices[miniaudio.devices.length-1]==null){miniaudio.devices.pop()}else{break}}};miniaudio.untrack_device=function(device){for(var iDevice=0;iDevice<miniaudio.devices.length;++iDevice){if(miniaudio.devices[iDevice]==device){return miniaudio.untrack_device_by_index(iDevice)}}};miniaudio.get_device_by_index=function(deviceIndex){return miniaudio.devices[deviceIndex]};miniaudio.unlock_event_types=function(){return["touchend","click"]}();miniaudio.unlock=function(){for(var i=0;i<miniaudio.devices.length;++i){var device=miniaudio.devices[i];if(device!=null&&device.webaudio!=null&&device.state===window.miniaudio.device_state.started){device.webaudio.resume().then(()=>{Module._ma_device__on_notification_unlocked(device.pDevice)},error=>{console.error("Failed to resume audiocontext",error)})}}miniaudio.unlock_event_types.map(function(event_type){document.removeEventListener(event_type,miniaudio.unlock,true)})};miniaudio.unlock_event_types.map(function(event_type){document.addEventListener(event_type,miniaudio.unlock,true)})}window.miniaudio.referenceCount+=1;return 1},4501496:()=>{if(typeof window.miniaudio!=="undefined"){window.miniaudio.referenceCount-=1;if(window.miniaudio.referenceCount===0){delete window.miniaudio}}},4501660:()=>navigator.mediaDevices!==undefined&&navigator.mediaDevices.getUserMedia!==undefined,4501764:()=>{try{var temp=new(window.AudioContext||window.webkitAudioContext);var sampleRate=temp.sampleRate;temp.close();return sampleRate}catch(e){return 0}},4501935:$0=>miniaudio.track_device({webaudio:emscriptenGetAudioObject($0),state:1}),4502024:($0,$1)=>{var getUserMediaResult=0;var audioWorklet=emscriptenGetAudioObject($0);var audioContext=emscriptenGetAudioObject($1);navigator.mediaDevices.getUserMedia({audio:true,video:false}).then(function(stream){audioContext.streamNode=audioContext.createMediaStreamSource(stream);audioContext.streamNode.connect(audioWorklet);audioWorklet.connect(audioContext.destination);getUserMediaResult=0}).catch(function(error){console.log("navigator.mediaDevices.getUserMedia Failed: "+error);getUserMediaResult=-1});return getUserMediaResult},4502586:($0,$1)=>{var audioWorklet=emscriptenGetAudioObject($0);var audioContext=emscriptenGetAudioObject($1);audioWorklet.connect(audioContext.destination);return 0},4502746:$0=>emscriptenGetAudioObject($0).sampleRate,4502798:$0=>{var device=miniaudio.get_device_by_index($0);if(device.streamNode!==undefined){device.streamNode.disconnect();device.streamNode=undefined}},4502954:$0=>{miniaudio.untrack_device_by_index($0)},4502997:$0=>{var device=miniaudio.get_device_by_index($0);device.webaudio.resume();device.state=miniaudio.device_state.started},4503122:$0=>{var device=miniaudio.get_device_by_index($0);device.webaudio.suspend();device.state=miniaudio.device_state.stopped},4503248:($0,$1,$2)=>{if(typeof amy_external_midi_input_js_hook==="function"){amy_external_midi_input_js_hook((growMemViews(),HEAPU8).subarray($0,$0+$1),$1,$2)}},4503379:($0,$1)=>{if(midiOutputDevice!=null){midiOutputDevice.send((growMemViews(),HEAPU8).subarray($0,$0+$1))}}};var wasmImports;function assignWasmImports(){wasmImports={b:___assert_fail,A:__abort_js,l:__embind_register_bigint,v:__embind_register_bool,t:__embind_register_emval,k:__embind_register_float,f:__embind_register_integer,d:__embind_register_memory_view,u:__embind_register_std_string,i:__embind_register_std_wstring,w:__embind_register_void,m:_emscripten_asm_const_double,c:_emscripten_asm_const_int,h:_emscripten_cancel_main_loop,r:_emscripten_create_audio_context,B:_emscripten_create_wasm_audio_worklet_node,C:_emscripten_create_wasm_audio_worklet_processor_async,g:_emscripten_destroy_audio_context,o:_emscripten_destroy_web_audio_node,e:_emscripten_get_now,x:_emscripten_resize_heap,j:_emscripten_set_main_loop,p:_emscripten_sleep,q:_emscripten_start_wasm_audio_worklet_thread_async,s:_exit,z:_fd_close,y:_fd_seek,n:_fd_write,a:wasmMemory}}var wasmExports=await createWasm();var ___wasm_call_ctors=()=>(___wasm_call_ctors=wasmExports["D"])();var _free=Module["_free"]=a0=>(_free=Module["_free"]=wasmExports["E"])(a0);var _malloc=Module["_malloc"]=a0=>(_malloc=Module["_malloc"]=wasmExports["F"])(a0);var _amy_sysclock=Module["_amy_sysclock"]=()=>(_amy_sysclock=Module["_amy_sysclock"]=wasmExports["H"])();var _amy_reset_sysclock=Module["_amy_reset_sysclock"]=()=>(_amy_reset_sysclock=Module["_amy_reset_sysclock"]=wasmExports["I"])();var _amy_add_event=Module["_amy_add_event"]=a0=>(_amy_add_event=Module["_amy_add_event"]=wasmExports["J"])(a0);var _yield_synth_commands=Module["_yield_synth_commands"]=(a0,a1,a2,a3,a4)=>(_yield_synth_commands=Module["_yield_synth_commands"]=wasmExports["K"])(a0,a1,a2,a3,a4);var _size_of_amy_event=Module["_size_of_amy_event"]=()=>(_size_of_amy_event=Module["_size_of_amy_event"]=wasmExports["L"])();var _amy_dump_state_to_string=Module["_amy_dump_state_to_string"]=a0=>(_amy_dump_state_to_string=Module["_amy_dump_state_to_string"]=wasmExports["M"])(a0);var _sequencer_ticks=Module["_sequencer_ticks"]=()=>(_sequencer_ticks=Module["_sequencer_ticks"]=wasmExports["N"])();var _ma_device__on_notification_unlocked=Module["_ma_device__on_notification_unlocked"]=a0=>(_ma_device__on_notification_unlocked=Module["_ma_device__on_notification_unlocked"]=wasmExports["O"])(a0);var _ma_malloc_emscripten=Module["_ma_malloc_emscripten"]=(a0,a1)=>(_ma_malloc_emscripten=Module["_ma_malloc_emscripten"]=wasmExports["P"])(a0,a1);var _ma_free_emscripten=Module["_ma_free_emscripten"]=(a0,a1)=>(_ma_free_emscripten=Module["_ma_free_emscripten"]=wasmExports["Q"])(a0,a1);var _ma_device_process_pcm_frames_capture__webaudio=Module["_ma_device_process_pcm_frames_capture__webaudio"]=(a0,a1,a2)=>(_ma_device_process_pcm_frames_capture__webaudio=Module["_ma_device_process_pcm_frames_capture__webaudio"]=wasmExports["R"])(a0,a1,a2);var _ma_device_process_pcm_frames_playback__webaudio=Module["_ma_device_process_pcm_frames_playback__webaudio"]=(a0,a1,a2)=>(_ma_device_process_pcm_frames_playback__webaudio=Module["_ma_device_process_pcm_frames_playback__webaudio"]=wasmExports["S"])(a0,a1,a2);var _amy_live_start_web_audioin=Module["_amy_live_start_web_audioin"]=()=>(_amy_live_start_web_audioin=Module["_amy_live_start_web_audioin"]=wasmExports["T"])();var _amy_live_start_web=Module["_amy_live_start_web"]=()=>(_amy_live_start_web=Module["_amy_live_start_web"]=wasmExports["U"])();var _amy_live_stop=Module["_amy_live_stop"]=()=>(_amy_live_stop=Module["_amy_live_stop"]=wasmExports["V"])();var _amy_add_message=Module["_amy_add_message"]=a0=>(_amy_add_message=Module["_amy_add_message"]=wasmExports["W"])(a0);var _amy_process_single_midi_byte=Module["_amy_process_single_midi_byte"]=(a0,a1)=>(_amy_process_single_midi_byte=Module["_amy_process_single_midi_byte"]=wasmExports["X"])(a0,a1);var _amy_get_output_buffer=Module["_amy_get_output_buffer"]=a0=>(_amy_get_output_buffer=Module["_amy_get_output_buffer"]=wasmExports["Y"])(a0);var _amy_get_input_buffer=Module["_amy_get_input_buffer"]=a0=>(_amy_get_input_buffer=Module["_amy_get_input_buffer"]=wasmExports["Z"])(a0);var _amy_set_external_input_buffer=Module["_amy_set_external_input_buffer"]=a0=>(_amy_set_external_input_buffer=Module["_amy_set_external_input_buffer"]=wasmExports["_"])(a0);var _amy_start_web=Module["_amy_start_web"]=()=>(_amy_start_web=Module["_amy_start_web"]=wasmExports["$"])();var _amy_bleep=Module["_amy_bleep"]=a0=>(_amy_bleep=Module["_amy_bleep"]=wasmExports["aa"])(a0);var _amy_start_web_no_synths=Module["_amy_start_web_no_synths"]=()=>(_amy_start_web_no_synths=Module["_amy_start_web_no_synths"]=wasmExports["ba"])();var __embind_initialize_bindings=()=>(__embind_initialize_bindings=wasmExports["ca"])();var __emscripten_stack_restore=a0=>(__emscripten_stack_restore=wasmExports["da"])(a0);var __emscripten_stack_alloc=a0=>(__emscripten_stack_alloc=wasmExports["ea"])(a0);var _emscripten_stack_get_current=()=>(_emscripten_stack_get_current=wasmExports["fa"])();var __emscripten_wasm_worker_initialize=(a0,a1)=>(__emscripten_wasm_worker_initialize=wasmExports["ga"])(a0,a1);var dynCall_iiii=Module["dynCall_iiii"]=(a0,a1,a2,a3)=>(dynCall_iiii=Module["dynCall_iiii"]=wasmExports["dynCall_iiii"])(a0,a1,a2,a3);var dynCall_iii=Module["dynCall_iii"]=(a0,a1,a2)=>(dynCall_iii=Module["dynCall_iii"]=wasmExports["dynCall_iii"])(a0,a1,a2);var dynCall_vi=Module["dynCall_vi"]=(a0,a1)=>(dynCall_vi=Module["dynCall_vi"]=wasmExports["dynCall_vi"])(a0,a1);var dynCall_vii=Module["dynCall_vii"]=(a0,a1,a2)=>(dynCall_vii=Module["dynCall_vii"]=wasmExports["dynCall_vii"])(a0,a1,a2);var dynCall_ii=Module["dynCall_ii"]=(a0,a1)=>(dynCall_ii=Module["dynCall_ii"]=wasmExports["dynCall_ii"])(a0,a1);var dynCall_iiiii=Module["dynCall_iiiii"]=(a0,a1,a2,a3,a4)=>(dynCall_iiiii=Module["dynCall_iiiii"]=wasmExports["dynCall_iiiii"])(a0,a1,a2,a3,a4);var dynCall_viii=Module["dynCall_viii"]=(a0,a1,a2,a3)=>(dynCall_viii=Module["dynCall_viii"]=wasmExports["ha"])(a0,a1,a2,a3);var dynCall_viiii=Module["dynCall_viiii"]=(a0,a1,a2,a3,a4)=>(dynCall_viiii=Module["dynCall_viiii"]=wasmExports["dynCall_viiii"])(a0,a1,a2,a3,a4);var dynCall_v=Module["dynCall_v"]=a0=>(dynCall_v=Module["dynCall_v"]=wasmExports["ia"])(a0);var dynCall_iiiiiiii=Module["dynCall_iiiiiiii"]=(a0,a1,a2,a3,a4,a5,a6,a7)=>(dynCall_iiiiiiii=Module["dynCall_iiiiiiii"]=wasmExports["dynCall_iiiiiiii"])(a0,a1,a2,a3,a4,a5,a6,a7);var dynCall_iiiji=Module["dynCall_iiiji"]=(a0,a1,a2,a3,a4)=>(dynCall_iiiji=Module["dynCall_iiiji"]=wasmExports["dynCall_iiiji"])(a0,a1,a2,a3,a4);var dynCall_iiiiiii=Module["dynCall_iiiiiii"]=(a0,a1,a2,a3,a4,a5,a6)=>(dynCall_iiiiiii=Module["dynCall_iiiiiii"]=wasmExports["dynCall_iiiiiii"])(a0,a1,a2,a3,a4,a5,a6);var dynCall_jii=Module["dynCall_jii"]=(a0,a1,a2)=>(dynCall_jii=Module["dynCall_jii"]=wasmExports["dynCall_jii"])(a0,a1,a2);var dynCall_jiji=Module["dynCall_jiji"]=(a0,a1,a2,a3)=>(dynCall_jiji=Module["dynCall_jiji"]=wasmExports["dynCall_jiji"])(a0,a1,a2,a3);var dynCall_iidiiii=Module["dynCall_iidiiii"]=(a0,a1,a2,a3,a4,a5,a6)=>(dynCall_iidiiii=Module["dynCall_iidiiii"]=wasmExports["dynCall_iidiiii"])(a0,a1,a2,a3,a4,a5,a6);var dynCall_viiiiii=Module["dynCall_viiiiii"]=(a0,a1,a2,a3,a4,a5,a6)=>(dynCall_viiiiii=Module["dynCall_viiiiii"]=wasmExports["dynCall_viiiiii"])(a0,a1,a2,a3,a4,a5,a6);var dynCall_viiiii=Module["dynCall_viiiii"]=(a0,a1,a2,a3,a4,a5)=>(dynCall_viiiii=Module["dynCall_viiiii"]=wasmExports["dynCall_viiiii"])(a0,a1,a2,a3,a4,a5);var _asyncify_start_unwind=a0=>(_asyncify_start_unwind=wasmExports["ja"])(a0);var _asyncify_stop_unwind=()=>(_asyncify_stop_unwind=wasmExports["ka"])();var _asyncify_start_rewind=a0=>(_asyncify_start_rewind=wasmExports["la"])(a0);var _asyncify_stop_rewind=()=>(_asyncify_stop_rewind=wasmExports["ma"])();function run(){if(runDependencies>0){dependenciesFulfilled=run;return}if(ENVIRONMENT_IS_WASM_WORKER){readyPromiseResolve(Module);initRuntime();return}preRun();if(runDependencies>0){dependenciesFulfilled=run;return}function doRun(){Module["calledRun"]=true;if(ABORT)return;initRuntime();readyPromiseResolve(Module);Module["onRuntimeInitialized"]?.();postRun()}if(Module["setStatus"]){Module["setStatus"]("Running...");setTimeout(()=>{setTimeout(()=>Module["setStatus"](""),1);doRun()},1)}else{doRun()}}function preInit(){if(Module["preInit"]){if(typeof Module["preInit"]=="function")Module["preInit"]=[Module["preInit"]];while(Module["preInit"].length>0){Module["preInit"].shift()()}}}preInit();run();moduleRtn=readyPromise;


  return moduleRtn;
}
);
})();
if (typeof exports === 'object' && typeof module === 'object') {
  module.exports = amyModule;
  // This default export looks redundant, but it allows TS to import this
  // commonjs style module.
  module.exports.default = amyModule;
} else if (typeof define === 'function' && define['amd'])
  define([], () => amyModule);
var isWW = globalThis.self?.name == 'em-ww';
var isNode = globalThis.process?.versions?.node && globalThis.process?.type != 'renderer';
if (isNode) isWW = require('worker_threads').workerData === 'em-ww'

isWW ||= typeof AudioWorkletGlobalScope !== 'undefined';
// When running as a wasm worker, construct a new instance on startup
isWW && amyModule();
// amy_connector.js
// helpers for JS to communicate with AMY, including webMIDI

var amy_add_message = null;
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


// AUTO-GENERATED by scripts/gen_amy_js_api.py — do not edit by hand.
// Source: amy/__init__.py
(function() {
"use strict";

var AMY_KW_MAP = {
  osc: {wire: "v", type: "I"},
  wave: {wire: "w", type: "I"},
  note: {wire: "n", type: "F"},
  vel: {wire: "l", type: "F"},
  amp: {wire: "a", type: "C"},
  freq: {wire: "f", type: "C"},
  duty: {wire: "d", type: "C"},
  feedback: {wire: "b", type: "F"},
  time: {wire: "t", type: "I"},
  reset: {wire: "S", type: "I"},
  phase: {wire: "P", type: "F"},
  pan: {wire: "Q", type: "C"},
  client: {wire: "g", type: "I"},
  volume: {wire: "V", type: "L"},
  pitch_bend: {wire: "s", type: "F"},
  filter_freq: {wire: "F", type: "C"},
  resonance: {wire: "R", type: "F"},
  bp0: {wire: "A", type: "L"},
  bp1: {wire: "B", type: "L"},
  eg0: {wire: "A", type: "L"},
  eg1: {wire: "B", type: "L"},
  eg0_type: {wire: "T", type: "I"},
  eg1_type: {wire: "X", type: "I"},
  debug: {wire: "D", type: "I"},
  chained_osc: {wire: "c", type: "I"},
  mod_source: {wire: "L", type: "I"},
  eq: {wire: "x", type: "L"},
  filter_type: {wire: "G", type: "I"},
  ratio: {wire: "I", type: "F"},
  latency_ms: {wire: "N", type: "I"},
  algo_source: {wire: "O", type: "L"},
  load_sample: {wire: "z", type: "L"},
  transfer_file: {wire: "zT", type: "L"},
  disk_sample: {wire: "zF", type: "L"},
  algorithm: {wire: "o", type: "I"},
  chorus: {wire: "k", type: "L"},
  reverb: {wire: "h", type: "L"},
  echo: {wire: "M", type: "L"},
  patch: {wire: "K", type: "I"},
  voices: {wire: "r", type: "L"},
  external_channel: {wire: "W", type: "I"},
  portamento: {wire: "m", type: "I"},
  sequence: {wire: "H", type: "L"},
  tempo: {wire: "j", type: "F"},
  sequencer_run: {wire: "zY", type: "I"},
  external_midi_sync: {wire: "zC", type: "I"},
  synth: {wire: "i", type: "I"},
  pedal: {wire: "ip", type: "I"},
  synth_flags: {wire: "if", type: "I"},
  num_voices: {wire: "iv", type: "I"},
  oscs_per_voice: {wire: "in", type: "I"},
  to_synth: {wire: "it", type: "I"},
  grab_midi_notes: {wire: "im", type: "I"},
  note_source: {wire: "iM", type: "I"},
  synth_delay: {wire: "id", type: "I"},
  preset: {wire: "p", type: "I"},
  num_partials: {wire: "p", type: "I"},
  start_sample: {wire: "zS", type: "L"},
  stop_sample: {wire: "zO", type: "I"},
  bus: {wire: "y", type: "I"},
  midi_cc: {wire: "ic", type: "L"},
  midi_note_cmd: {wire: "io", type: "L"},
  cv_trigger: {wire: "ig", type: "L"},
  patch_string: {wire: "u", type: "S"}
};

var AMY_KW_PRIORITY = {
  osc: 0,
  wave: 1,
  note: 2,
  vel: 3,
  amp: 4,
  freq: 5,
  duty: 6,
  feedback: 7,
  time: 8,
  reset: 9,
  phase: 10,
  pan: 11,
  client: 12,
  volume: 13,
  pitch_bend: 14,
  filter_freq: 15,
  resonance: 16,
  bp0: 17,
  bp1: 18,
  eg0: 19,
  eg1: 20,
  eg0_type: 21,
  eg1_type: 22,
  debug: 23,
  chained_osc: 24,
  mod_source: 25,
  eq: 26,
  filter_type: 27,
  ratio: 28,
  latency_ms: 29,
  algo_source: 30,
  load_sample: 31,
  transfer_file: 32,
  disk_sample: 33,
  algorithm: 34,
  chorus: 35,
  reverb: 36,
  echo: 37,
  patch: 38,
  voices: 39,
  external_channel: 40,
  portamento: 41,
  sequence: 42,
  tempo: 43,
  sequencer_run: 44,
  external_midi_sync: 45,
  synth: 46,
  pedal: 47,
  synth_flags: 48,
  num_voices: 49,
  oscs_per_voice: 50,
  to_synth: 51,
  grab_midi_notes: 52,
  note_source: 53,
  synth_delay: 54,
  preset: 55,
  num_partials: 56,
  start_sample: 57,
  stop_sample: 58,
  bus: 59,
  midi_cc: 60,
  midi_note_cmd: 61,
  cv_trigger: 62,
  patch_string: 63
};

var AMY_COEF_FIELDS = ["const", "note", "vel", "eg0", "eg1", "mod", "bend", "ext0", "ext1"];

// --- type handlers (mirror Python's str_of_int, trunc, parse_list_or_comma_string, parse_ctrl_coefs) ---

function _str_of_int(arg) {
  return String(Math.trunc(Number(arg)));
}

function _trunc(number) {
  if (typeof number === "string") {
    if (number.trim() === "") return "";
    number = parseFloat(number);
  }
  if (typeof number === "number") {
    var s = number.toFixed(6);
    s = s.replace(/0+$/, "");
    s = s.replace(/\.$/, "");
    return s;
  }
  return String(number);
}

function _parse_list(obj) {
  if (Array.isArray(obj)) {
    return obj.map(function(v) { return v == null ? "" : String(v); }).join(",");
  }
  return String(obj);
}

function _trim_trailing(arr) {
  var end = arr.length;
  while (end > 0 && arr[end - 1] == null) end--;
  return arr.slice(0, end);
}

function _parse_ctrl_coefs(coefs) {
  if (typeof coefs === "string") {
    return coefs.split(",").map(function(x) { return _trunc(x); }).join(",");
  }
  if (typeof coefs === "number") {
    return _trunc(coefs);
  }
  if (!Array.isArray(coefs) && typeof coefs === "object" && coefs !== null) {
    var list = new Array(AMY_COEF_FIELDS.length).fill(null);
    for (var key in coefs) {
      if (!coefs.hasOwnProperty(key)) continue;
      var idx = AMY_COEF_FIELDS.indexOf(key);
      if (idx < 0) throw new Error("Unknown ctrl_coef field: " + key + ". Valid: " + AMY_COEF_FIELDS.join(", "));
      list[idx] = coefs[key];
    }
    coefs = list;
  }
  if (!Array.isArray(coefs)) {
    return String(coefs);
  }
  coefs = _trim_trailing(coefs);
  return coefs.map(function(v) { return v == null ? "" : _trunc(v); }).join(",");
}

var _ARG_HANDLERS = {
  I: _str_of_int,
  F: _trunc,
  S: String,
  L: _parse_list,
  C: _parse_ctrl_coefs
};

/**
 * Build an AMY wire code string from named parameters.
 *
 *   amy_message({osc: 0, wave: 1, freq: 440})  =>  "v0w1f440Z"
 *   amy_message({synth: 1, patch: 257, num_voices: 6})  =>  "K257i1iv6Z"
 *
 * Control coefficient params (amp, freq, duty, pan, filter_freq) accept:
 *   - A number:  freq: 440
 *   - An array:  freq: [440, 1, null, null, null, null, 1]
 *   - An object: freq: {const: 440, bend: 1}
 */
function amy_message(params) {
  if (!params || typeof params !== "object") {
    throw new Error("amy_message requires an object of parameters");
  }
  var keys = [];
  for (var key in params) {
    if (!params.hasOwnProperty(key)) continue;
    if (!(key in AMY_KW_MAP)) {
      throw new Error("Unknown AMY parameter: " + key);
    }
    var arg = params[key];
    if (arg == null) {
      if (key !== "time" && key !== "sequence") {
        throw new Error("Null value for AMY parameter: " + key);
      }
      continue;
    }
    keys.push(key);
  }
  keys.sort(function(a, b) {
    return (AMY_KW_PRIORITY[a] || 0) - (AMY_KW_PRIORITY[b] || 0);
  });
  var m = "";
  for (var i = 0; i < keys.length; i++) {
    var k = keys[i];
    var mapping = AMY_KW_MAP[k];
    var handler = _ARG_HANDLERS[mapping.type];
    m += mapping.wire + handler(params[k]);
  }
  return m + "Z";
}

/**
 * Build and send an AMY wire code message.
 * Equivalent to Python's amy.send().
 */
function amy_send(params, log) {
  var msg = amy_message(params);
  if (log && typeof amy_add_log_message === "function") {
    amy_add_log_message(msg);
  } else if (typeof amy_add_message === "function") {
    amy_add_message(msg);
  } else {
    console.warn("amy_send: no AMY message handler found (is amy.js loaded?)");
  }
  return msg;
}

// Constants from amy/constants.py (mirrors amy.SINE, amy.FILTER_LPF, etc.)
var AMY = {
  MAX_FILENAME_LEN: 127,
  AMY_BLOCK_SIZE: 256,
  BLOCK_SIZE_BITS: 8,
  AMY_SAMPLE_RATE: 44100,
  PCM_AMY_SAMPLE_RATE: 22050,
  AMY_TRANSFER_TYPE_NONE: 0,
  AMY_TRANSFER_TYPE_AUDIO: 1,
  AMY_TRANSFER_TYPE_FILE: 2,
  AMY_TRANSFER_TYPE_SAMPLE: 3,
  AMY_PCM_TYPE_ROM: 0,
  AMY_PCM_TYPE_FILE: 1,
  AMY_PCM_TYPE_MEMORY: 2,
  AMY_PCM_TYPE_GAMMA: 3,
  PCM_FILE_BUFFER_MULT: 8,
  SAMPLE_FROM_OUTPUT: 1,
  SAMPLE_FROM_AUDIO_IN: 2,
  AMY_NUM_BUSES: 4,
  AMY_DEFAULT_BUS: 0,
  AMY_MAX_CV_IN: 2,
  AMY_MAX_CORES: 2,
  AMY_MAX_CHANNELS: 2,
  AMY_NCHANS: 2,
  AMY_CORES: 1,
  AMY_MIDI_CHANNEL_DRUMS: 10,
  AMY_WIRE_COMMAND_LEN: 256,
  MALLOC_CAP_DEFAULT: 0,
  CHORUS_DEFAULT_LFO_FREQ: 0.5,
  CHORUS_DEFAULT_MOD_DEPTH: 0.5,
  CHORUS_DEFAULT_LEVEL: 0,
  CHORUS_DEFAULT_MAX_DELAY: 320,
  EQ_CENTER_LOW: 800.0,
  EQ_CENTER_MED: 2500.0,
  EQ_CENTER_HIGH: 7000.0,
  REVERB_DEFAULT_LEVEL: 0,
  REVERB_DEFAULT_LIVENESS: 0.85,
  REVERB_DEFAULT_DAMPING: 0.5,
  REVERB_DEFAULT_XOVER_HZ: 3000.0,
  ECHO_DEFAULT_LEVEL: 0,
  ECHO_DEFAULT_DELAY_MS: 500.0,
  ECHO_DEFAULT_MAX_DELAY_MS: 743.039,
  ECHO_DEFAULT_FEEDBACK: 0,
  ECHO_DEFAULT_FILTER_COEF: 0,
  AMY_SEQUENCER_PPQ: 48,
  DELAY_LINE_LEN: 512,
  CLIP_D: 0.1,
  MAX_VOLUME: 11.0,
  AMP_THRESH: 0.001,
  AMP_THRESH_PLUS: 0.0011,
  SAMPLE_MAX: 32767,
  MAX_ALGO_OPS: 6,
  DEFAULT_NUM_BREAKPOINTS: 8,
  MAX_BREAKPOINTS: 24,
  MAX_BREAKPOINT_SETS: 2,
  THREAD_USLEEP: 500,
  AMY_BYTES_PER_SAMPLE: 2,
  MAX_VOICES_PER_INSTRUMENT: 32,
  FILT_NUM_DELAYS: 4,
  ZERO_HZ_LOG_VAL: -99.0,
  ZERO_LOGFREQ_IN_HZ: 440.0,
  ZERO_MIDI_NOTE: 69,
  MIN_FILTER_LOGFREQ: -2.75,
  NUM_COMBO_COEFS: 9,
  MAX_MESSAGE_LEN: 1024,
  MAX_PARAM_LEN: 256,
  FILTER_NONE: 0,
  FILTER_LPF: 1,
  FILTER_BPF: 2,
  FILTER_HPF: 3,
  FILTER_LPF24: 4,
  SINE: 0,
  PULSE: 1,
  SAW_DOWN: 2,
  SAW_UP: 3,
  TRIANGLE: 4,
  NOISE: 5,
  KS: 6,
  PCM: 7,
  ALGO: 8,
  PARTIAL: 9,
  BYO_PARTIALS: 10,
  INTERP_PARTIALS: 11,
  AUDIO_IN0: 12,
  AUDIO_IN1: 13,
  AUDIO_EXT0: 14,
  AUDIO_EXT1: 15,
  AMY_MIDI: 16,
  PCM_LEFT: 17,
  PCM_RIGHT: 18,
  PCM_MIX: 7,
  WAVETABLE: 19,
  SILENT: 20,
  CUSTOM: 21,
  WAVE_OFF: 22,
  SYNTH_OFF: 0,
  SYNTH_AUDIBLE: 1,
  SYNTH_INAUDIBLE: 2,
  SYNTH_IS_MOD_SOURCE: 3,
  SYNTH_IS_ALGO_SOURCE: 4,
  EVENT_EMPTY: 0,
  EVENT_SCHEDULED: 1,
  EVENT_TRANSFER_DATA: 2,
  EVENT_SEQUENCE: 3,
  NOTE_SOURCE_MIDI: 1,
  ENVELOPE_NORMAL: 0,
  ENVELOPE_LINEAR: 1,
  ENVELOPE_DX7: 2,
  ENVELOPE_TRUE_EXPONENTIAL: 3,
  SEQUENCE_TICK: 0,
  SEQUENCE_PERIOD: 1,
  SEQUENCE_TAG: 2,
  RESET_SEQUENCER: 4096,
  RESET_ALL_OSCS: 8192,
  RESET_TIMEBASE: 16384,
  RESET_AMY: 32768,
  RESET_EVENTS: 65536,
  RESET_ALL_NOTES: 131072,
  RESET_SYNTHS: 262144,
  RESET_PATCH: 524288,
  RESET_QUEUE: 1048576,
  SYNTH_FLAGS_NOTES_VIA_MIDI: 1,
  SYNTH_FLAGS_IGNORE_NOTE_OFFS: 2,
  SYNTH_FLAGS_NEGATE_PEDAL: 4,
  AMY_OK: 0,
  AMY_AUDIO_IS_NONE: 0,
  AMY_AUDIO_IS_I2S: 1,
  AMY_AUDIO_IS_USB_GADGET: 2,
  AMY_AUDIO_IS_MINIAUDIO: 4,
  AMY_MIDI_IS_NONE: 0,
  AMY_MIDI_IS_UART: 1,
  AMY_MIDI_IS_USB_GADGET: 2,
  AMY_MIDI_IS_MACOS: 4,
  AMY_MIDI_IS_WEBMIDI: 8,
  AMYBOARD_LRC: 2,
  AMYBOARD_BCLK: 8,
  AMYBOARD_DOUT: 6,
  AMYBOARD_DIN: 9,
  AMYBOARD_MCLK: 3,
  AMYBOARD_MIDI_OUT_TYPE_A: 14,
  AMYBOARD_MIDI_OUT_TYPE_B: 15,
  AMYBOARD_MIDI_IN: 21
};

if (typeof globalThis !== "undefined") {
  globalThis.amy_message = amy_message;
  globalThis.amy_send = amy_send;
  globalThis.AMY = AMY;
  globalThis.AMY_KW_MAP = AMY_KW_MAP;
  globalThis.AMY_COEF_FIELDS = AMY_COEF_FIELDS;
}

})();
