// SPDX-License-Identifier: GPL-3.0-or-later
import createFM1Module from './fm1-core.js';
const fields = ['frames','tracks','assets','bpm','playing','dirty','picker','screen','tab','scope','cutoff','resonance','volume','pan','regions','captureState','peak','captureSlot','error','sections','pattern','voices','recording','armed','candidates','historySeconds','arranging','audioBytes','fxBytes','rejectedEvents','busyFrames','instrumentType','baseNote','octave','generation'];
let core, handle, audio, persistent = true, queue = Promise.resolve();
const sync = populate => new Promise((resolve,reject) => core.FS.syncfs(populate,e => e ? reject(e) : resolve()));
function state() {
  const s = Object.fromEntries(fields.map((name,i) => [name,core._fm_sim_metric(handle,i)]));
  s.title = core.UTF8ToString(core._fm_sim_text(handle,0));
  s.status = core.UTF8ToString(core._fm_sim_text(handle,1));
  s.instrument = core.UTF8ToString(core._fm_sim_text(handle,2));
  s.editor = JSON.parse(core.UTF8ToString(core._fm_sim_editor(handle)));
  s.browser = true; s.persistence = persistent;
  return s;
}
function bytesIn(bytes, callback) {
  const b = new Uint8Array(bytes);
  if (b.length > 32*1024*1024) throw new Error('Maximum file size is 32 MB');
  const p = core._malloc(b.length || 1);
  if (!p) throw new Error('Working memory is full');
  try {core.HEAPU8.set(b,p); return callback(p,b.length);} finally {core._free(p);}
}
function backup() {
  const pointers = core._malloc(8);
  try {
    const result = core._fm_sim_backup(handle,pointers,pointers+4);
    if(result) throw new Error('Project export failed (code '+result+')');
    const view = new DataView(core.HEAPU8.buffer);
    const p = view.getUint32(pointers,true), n = view.getUint32(pointers+4,true);
    try {return core.HEAPU8.slice(p,p+n).buffer;} finally {core._fm_sim_free(p);}
  } finally {core._free(pointers);}
}
async function durability(before) {
  if(core._fm_sim_metric(handle,34) === before) return;
  try {await sync(false); persistent = true;}
  catch(e) {
    persistent = false; core._fm_sim_persistence_error(handle);
    throw new Error('This browser could not save locally. Export a project backup to Files.');
  }
}
const ready = (async () => {
  core = await createFM1Module({print:()=>{},printErr:message=>console.warn(message)});
  core.FS.mkdir('/fm1-fz-emulator');
  core.FS.mount(core.IDBFS,{},'/fm1-fz-emulator');
  try {await sync(true);} catch(e) {persistent = false;}
  handle = core.ccall('fm_sim_create','number',['string'],['/fm1-fz-emulator']);
  if(!handle) throw new Error('The audio engine could not allocate its working memory');
  audio = core._malloc(8192*2*4);
  if(!audio) throw new Error('The browser audio buffer could not be allocated');
  const saved = ['0','1'].some(n=>core.FS.analyzePath('/fm1-fz-emulator/project.'+n).exists);
  if(saved && core._fm_sim_command(handle,6,0,0)) {
    core._fm_sim_persistence_error(handle);
  }
  return state();
})();
self.onmessage = ({data:{id,path,body}}) => {
  queue = queue.then(async () => {
    await ready;
    const before = core._fm_sim_metric(handle,34);
    let result;
    if(path==='/api/state') result = state();
    else if(path==='/api/screen.png') {
      const p = core._fm_sim_pixels(handle);
      const pixels = core.HEAPU8.slice(p,p+240*240).buffer;
      self.postMessage({id,result:pixels},[pixels]); return;
    } else if(path==='/api/audio') {
      const code = core._fm_sim_render(handle,audio,512);
      if(code) throw new Error('Audio render failed (code '+code+')');
      await durability(before);
      const samples = core.HEAPF32.slice(audio/4,audio/4+1024).buffer;
      self.postMessage({id,result:samples},[samples]); return;
    } else if(path==='/api/backup') {
      const bytes = backup(); self.postMessage({id,result:bytes},[bytes]); return;
    } else {
      let code;
      if(path==='/api/command') {
        const c=JSON.parse(body);
        if(!['type','index','value'].every(k=>Number.isInteger(c[k]))) throw new Error('Invalid control command');
        code=core._fm_sim_command(handle,c.type,c.index,c.value);
      } else if(path==='/api/action') {
        const c=JSON.parse(body);
        if(!Number.isInteger(c.action)||!Number.isInteger(c.value)||c.action<0||c.value<0) throw new Error('Invalid action');
        code=core._fm_sim_action(handle,c.action,c.value);
      } else if(path==='/api/parameter') {
        const c=JSON.parse(body);
        if(!['tab','row','field'].every(k=>Number.isInteger(c[k])&&c[k]>=0)||!Number.isFinite(c.value)) throw new Error('Invalid parameter');
        code=core._fm_sim_parameter(handle,c.tab,c.row,c.field,c.value);
      } else if(path==='/api/draw'||path==='/api/input'||path==='/api/harmonics') {
        if(!(body instanceof ArrayBuffer)||body.byteLength%2) throw new Error('Invalid PCM16 data');
        code=bytesIn(body,(p,n)=>path==='/api/draw'?core._fm_sim_draw(handle,p,n/2):path==='/api/harmonics'?core._fm_sim_harmonics(handle,p,n/2):core._fm_sim_input(handle,p,n/2));
      } else if(path==='/api/demo') code=core._fm_sim_demo(handle);
      else if(path==='/api/restore') {
        code=bytesIn(body,(p,n)=>core._fm_sim_restore(handle,p,n));
        if(!code) {
          const saved=core._fm_sim_command(handle,5,0,0);
          if(saved) {
            core._fm_sim_persistence_error(handle);
            throw new Error('Project restored in memory, but local saving failed. Export a backup to Files.');
          }
        }
      }
      else if(path==='/api/wave' || path==='/api/cycle') code=bytesIn(body,(p,n)=>core._fm_sim_import_wave(handle,p,n,path.endsWith('cycle')?1:0));
      else throw new Error('Unknown emulator request');
      await durability(before);
      result={result:code,state:state()};
    }
    self.postMessage({id,result});
  }).catch(error=>self.postMessage({id,error:error.message || String(error)}));
};
