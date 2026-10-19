import {initSequencer} from './sequencer.js';
import {initPerformance} from './performance.js';
import {initSampler} from './sampler.js';
// SPDX-License-Identifier: GPL-3.0-or-later
// Touch editors display the C model. Drawing is a draft until explicitly applied.
const $ = id => document.getElementById(id);
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
export function initWorkspace({request, update, getState, pauseSound, resumeSound, enableSound, showError}) {
  let model = {}, busy = false, synthMode = 'additive', drawing = null, drawingTrack = 0;
  let drawingDirty = false, gesture = null, partials = null;
  let recorder = null, micStream = null, micTimer = null, micPending = false, discardRecording = false, recordingTrack = 0;
  let controlKey = '', trackKey = '', scopeKey = '';
  let sampler,renderSequencer=()=>{},renderPerformance=()=>{},micVisual=null;
  const creation = $('new-track-dialog');
  const errors = {1:'Choose a compatible instrument or check the entered value.',2:'There is not enough working memory for this sound.',3:'Sample storage is full. Export a backup before removing unused sounds.',4:'This file or value is invalid.',5:'This value is outside the supported range.',6:'Finish the current take, stop playback or close the open picker, then try again.',7:'Choose a track with audio first.',8:'This operation is unavailable for the selected instrument.'};
  const liveMic = () => micPending || (recorder && recorder.state !== 'inactive');
  function fail(error) { showError(error instanceof Error ? error : new Error(String(error))); }
  async function send(path, body) {
    const response = await request(path, {method:'POST', body:body instanceof ArrayBuffer ? body : JSON.stringify(body)});
    const result = await response.json();
    update(result.state);
    if (result.result) throw new Error(errors[result.result] || `The instrument could not apply that change (code ${result.result}).`);
    return result.state;
  }
  async function run(task) {
    if (busy) return;
    busy = true;
    const controls=[...document.querySelectorAll('.workspace-actions,.workspace-top,#mod-controls,#synth-editor,#sequencer-editor,#arp-controls,#mixer-strips,.sampling-selectors,#sample-slots')];
    controls.forEach(element=>element.inert=true);
    $('visual-editor').setAttribute('aria-busy','true');
    try { return await task(); } catch (error) { fail(error); }
    finally {
      busy = false; controls.forEach(element=>element.inert=false);
      $('visual-editor').setAttribute('aria-busy','false');
    }
  }
  const action = (number, value = 0) => send('/api/action', {action:number, value});
  const parameter = (tab, row, value, field = 1) => send('/api/parameter', {tab,row,field,value});
  function discardDraft() {
    if (drawingDirty && !confirm('Discard the drawing that has not been applied?')) return false;
    drawingDirty = false; drawing = null; return true;
  }
  function reveal() {
    $('visual-editor').hidden = false;
    $('visual-editor').scrollIntoView({behavior:'smooth', block:'start'});
  }
  function openCreation(sampleOnly = false) {
    if (liveMic() || !discardDraft()) return;
    $('creation-error').textContent = '';
    creation.querySelectorAll('[data-create]').forEach(button => button.hidden = sampleOnly && +button.dataset.create > 1);
    creation.showModal();
  }
  $('new-track').onclick = () => openCreation();
  $('cancel-new-track').onclick = () => creation.close();
  creation.querySelectorAll('[data-create]').forEach(button => button.onclick = () => run(async () => {
    button.disabled = true;
    try {
      sampler.close();synthMode = +button.dataset.create >= 3 ? 'draw' : 'additive';
      await action(0, +button.dataset.create);
      creation.close(); reveal();
    } catch (error) { $('creation-error').textContent = error.message; }
    finally { button.disabled = false; }
  }));
  $('track-choice').onchange = event => run(async () => {
    if (liveMic() || !discardDraft()) { event.target.value = model.selected; return; }
    const page=getState().screen;
    sampler.close();await action(2, +event.target.value);
    if(page===2)await action(10);else if(page===15)await action(11);else if(page===16)await action(9);
  });
  $('sample-audio').onclick = () => run(async () => {
    if (liveMic() || !discardDraft()) return;
    if(![0,7].includes(getState().screen)){await send('/api/command',{type:0,index:8,value:1});await send('/api/command',{type:0,index:8,value:0});}
    sampler.open();update(getState());reveal();
  });
  for (const [id,tab] of [['open-envelope',3],['open-filter-envelope',4],['open-lfo',5],['open-synthesis',6]]) {
    $(id).onclick = () => run(async () => {
      if (!getState().tracks) { openCreation(); return; }
      if (liveMic() || !discardDraft()) return;
      if (tab === 6 && getState().instrumentType !== 2) { openCreation(); return; }
      sampler.close();await action(1,tab); reveal();
    });
  }
  $('close-editor').onclick = () => run(async () => {
    if (liveMic() || !discardDraft()) return;
    sampler.close();if(getState().screen===7) await action(4);
    else {
      await send('/api/command',{type:0,index:8,value:1});
      await send('/api/command',{type:0,index:8,value:0});
    }
  });
  $('edit-scope').onchange = event => run(() => action(8,+event.target.value));
  function field(label, name, config, value, change) {
    const wrapper = document.createElement('label'); wrapper.textContent = label;
    const input = document.createElement(config.options ? 'select' : 'input');
    input.id = name; input.setAttribute('aria-label',label);
    if (config.options) config.options.forEach((text,value) => input.add(new Option(text,value)));
    else { input.type='number'; input.required=true; input.min=config.min; input.max=config.max; input.step=config.step ?? 1; }
    input.value = value;
    input.onchange = () => run(async () => {
      const value = Number(input.value);
      if (!input.checkValidity() || !Number.isFinite(value)) { input.reportValidity(); return; }
      await change(value);
    });
    wrapper.append(input); $('mod-controls').append(wrapper);
  }
  function syncInput(id,value) {
    const input = $(id);
    if (input && document.activeElement !== input) input.value = value;
  }
  function graphBase(canvas, signed = false) {
    const ctx = canvas.getContext('2d'), w = canvas.width, h = canvas.height;
    ctx.fillStyle='#080910'; ctx.fillRect(0,0,w,h);
    ctx.strokeStyle='#38445c'; ctx.lineWidth=1;
    for(let x=12;x<w;x+=(w-24)/8) { ctx.beginPath();ctx.moveTo(x,12);ctx.lineTo(x,h-12);ctx.stroke(); }
    for(let y=12;y<h;y+=(h-24)/4) { ctx.beginPath();ctx.moveTo(12,y);ctx.lineTo(w-12,y);ctx.stroke(); }
    if(signed) {ctx.strokeStyle='#657596';ctx.beginPath();ctx.moveTo(12,h/2);ctx.lineTo(w-12,h/2);ctx.stroke();}
    ctx.strokeStyle='#52cb80'; ctx.lineWidth=2; return ctx;
  }
  function plotWave(canvas, samples, stepped=false) {
    const ctx=graphBase(canvas,true),w=canvas.width,h=canvas.height;
    if(!samples?.length) return;
    ctx.beginPath();
    samples.forEach((v,i)=>{ const x=12+i*(w-24)/Math.max(1,samples.length-1),y=h/2-v/32768*(h/2-14); if(i){if(stepped)ctx.lineTo(x,h/2-samples[i-1]/32768*(h/2-14));ctx.lineTo(x,y);}else ctx.moveTo(x,y); });
    ctx.stroke();
  }
  function plotEnvelope(envelope, stage) {
    const canvas=$('mod-graph'),ctx=graphBase(canvas),w=canvas.width,h=canvas.height;
    if(!envelope) return;
    const total=envelope.stages.reduce((sum,entry)=>sum+entry[0],0);
    const minimum=Math.max(1,total*.02),width=envelope.stages.reduce((sum,entry)=>sum+Math.max(minimum,entry[0]),0);
    let x=12,y=h-16;
    envelope.stages.forEach(([ms,level],i)=>{
      const nx=x+Math.max(minimum,ms)/width*(w-24),ny=h-16-level*(h-38);
      ctx.strokeStyle=i===stage?'#f4bf35':'#52cb80';ctx.lineWidth=i===stage?3:2;
      ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(nx,ny);ctx.stroke();
      ctx.fillStyle=i===stage?'#f4bf35':'#52cb80';ctx.beginPath();ctx.arc(nx,ny,i===stage?5:3,0,Math.PI*2);ctx.fill();
      ctx.font='700 12px system-ui';ctx.fillText(`${i+1}${i===envelope.sustain?' S':''}${i===envelope.release?' R':''}`,clamp(nx-8,12,w-36),16);
      x=nx;y=ny;
    });
  }
  function modEditor(s) {
    const tab=s.tab,env=tab===3?model.amp:model.filter,lfo=model.lfo;
    const key=`${model.selected}:${tab}:${!!env}:${!!lfo}:${env?.count}`;
    if(controlKey!==key) {
      controlKey=key; $('mod-controls').replaceChildren();
      if(tab===5&&lfo) {
        field('Rate (Hz)','lfo-rate',{min:0,max:50,step:.1},lfo.rate,v=>parameter(5,0,v));
        field('Depth (0–1)','lfo-depth',{min:0,max:1,step:.01},lfo.depth,v=>parameter(5,1,v));
        field('Shape','lfo-shape',{options:['Sine','Triangle','Square','Saw','Sample & Hold']},lfo.shape,v=>parameter(5,2,v));
        field('Destination','lfo-destination',{options:['Filter cutoff','Amplitude','Pitch','Pan']},lfo.destination,v=>parameter(5,3,v));
        field('Phase mode','lfo-phase',{options:['Retrigger on each note','Free running']},+lfo.free,v=>parameter(5,4,v));
      } else if(env) {
        const offset=tab===3?0:5;
        field('Stage','env-stage',{min:1,max:8},model.stage+1,v=>parameter(tab,offset,v-1,0));
        field('Time (ms)','env-time',{min:0,max:60000,step:1},env.stages[model.stage]?.[0]??0,v=>parameter(tab,offset+1,v));
        field('Level (0–1)','env-level',{min:0,max:1,step:.01},env.stages[model.stage]?.[1]??0,v=>parameter(tab,offset+2,v));
        field('Sustain stage','env-sustain',{min:1,max:env.count},env.sustain+1,v=>parameter(tab,offset+3,v-1));
        field('Release stage','env-release',{min:1,max:env.count},env.release+1,v=>parameter(tab,offset+4,v-1));
        field('Stage count','env-count',{min:1,max:8},env.count,v=>parameter(tab,offset+5,v));
      }
    }
    if(tab===5) {
      $('mod-graph').setAttribute('aria-label','LFO waveform graph');
      plotWave($('mod-graph'),lfo?.samples,lfo?.shape===4);
      $('graph-caption').textContent=lfo?`One second · ${lfo.rate.toFixed(2)} Hz · depth ${lfo.depth.toFixed(2)} · ${lfo.rate?`period ${(1000/lfo.rate).toFixed(1)} ms`:'stopped'}`:'Capture a sound first to edit its LFO.';
      if(lfo) for(const [id,v] of [['rate',lfo.rate],['depth',lfo.depth],['shape',lfo.shape],['destination',lfo.destination],['phase',+lfo.free]]) syncInput('lfo-'+id,v);
    } else {
      $('mod-graph').setAttribute('aria-label',tab===3?'Amplitude envelope graph':'Filter envelope graph');
      plotEnvelope(env,model.stage);
      $('graph-caption').textContent=env?'Stage times set the curve. S = sustain until note release; R = release entry. Zero-time stages remain visible.':'Capture a sound first to edit its envelope.';
      if(env) for(const [id,v] of [['stage',model.stage+1],['time',env.stages[model.stage]?.[0]??0],['level',env.stages[model.stage]?.[1]??0],['sustain',env.sustain+1],['release',env.release+1],['count',env.count]]) syncInput('env-'+id,v);
    }
  }
  function drawBars(values) {
    const canvas=$('harmonic-graph'),ctx=graphBase(canvas),w=canvas.width,h=canvas.height;
    values.forEach((level,i)=>{const bw=(w-24)/48;ctx.fillStyle=i===model.harmonic?'#f4bf35':'#52cb80';ctx.fillRect(12+i*bw,h-16-level/256*(h-32),Math.max(1,bw-2),Math.max(2,level/256*(h-32)));});
  }
  function drawingState() {
    $('apply-drawing').disabled=!drawingDirty;
    $('draw-status').textContent=drawingDirty?'Drawing changed. Apply drawing to hear and save it.':'Draw with your finger. Apply the drawing to hear it.';
  }
  function syncSynth() {
    const drawingMode=synthMode==='draw';
    $('synth-additive').setAttribute('aria-pressed',String(!drawingMode));
    $('synth-draw').setAttribute('aria-pressed',String(drawingMode));
    $('additive-controls').hidden=drawingMode; $('drawing-controls').hidden=!drawingMode;
    $('wave-graph').classList.toggle('drawable',drawingMode);
    $('wave-graph').setAttribute('aria-label',drawingMode?'Draw a single-cycle waveform':'Current single-cycle waveform');
    if(!drawingDirty||drawingTrack!==model.selected) {
      drawingTrack=model.selected;drawing=new Int16Array(1024);
      if(model.wave?.length) for(let i=0;i<1024;i++) drawing[i]=model.wave[Math.round(i/1023*(model.wave.length-1))];
      drawingDirty=false;
    }
    if(gesture?.kind!=='wave') plotWave($('wave-graph'),drawingMode?drawing:model.wave);
    if(gesture?.kind!=='partials') drawBars(model.harmonics||[]);
    syncInput('partial-number',(model.harmonic||0)+1);
    syncInput('partial-level',model.harmonics?.[model.harmonic]||0);
    drawingState();
  }
  $('synth-additive').onclick=()=>{if(!discardDraft())return;synthMode='additive';syncSynth();};
  $('synth-draw').onclick=()=>{synthMode='draw';syncSynth();};
  $('partial-number').onchange=event=>run(async()=>{
    if(!event.target.reportValidity()) return;
    await parameter(6,1,+event.target.value-1,0);
  });
  $('partial-level').onchange=event=>run(async()=>{
    if(!event.target.reportValidity()) return;
    await parameter(6,1,+event.target.value,1);
  });
  for(const [attribute,number] of [['wave',5],['recipe',6]]) document.querySelectorAll(`[data-${attribute}]`).forEach(button=>button.onclick=()=>run(async()=>{
    if(!discardDraft()) return;
    await action(number,+button.dataset[attribute]); syncSynth();
  }));
  $('apply-drawing').onclick=()=>run(async()=>{
    if(!drawingDirty) return;
    const bytes=drawing.slice().buffer;
    await send('/api/draw',bytes);drawingDirty=false;syncSynth();
  });
  $('reset-drawing').onclick=()=>{drawingDirty=false;syncSynth();};
  $('clear-drawing').onclick=()=>{drawing.fill(0);drawingDirty=true;plotWave($('wave-graph'),drawing);drawingState();};
  $('smooth-drawing').onclick=()=>{const old=drawing.slice();for(let i=0;i<drawing.length;i++)drawing[i]=Math.round((old[(i+1023)%1024]+old[i]+old[(i+1)%1024])/3);drawingDirty=true;plotWave($('wave-graph'),drawing);drawingState();};
  $('undo-synthesis').onclick=()=>run(async()=>{if(!discardDraft())return;await action(7);syncSynth();});
  function coordinate(canvas,event,n) {
    const box=canvas.getBoundingClientRect();
    return [clamp(Math.round(((event.clientX-box.left)/box.width*canvas.width-12)/(canvas.width-24)*(n-1)),0,n-1),clamp((1-(event.clientY-box.top)/box.height),0,1)];
  }
  function paint(event) {
    if(!gesture) return;
    const canvas=event.currentTarget,kind=gesture.kind,n=kind==='wave'?1024:48;
    const [at,height]=coordinate(canvas,event,n),value=kind==='wave'?Math.round((height*2-1)*30000):Math.round(height*256);
    const values=kind==='wave'?drawing:partials;
    if(gesture.at===null) values[at]=value;
    else {
      const from=Math.min(at,gesture.at),to=Math.max(at,gesture.at);
      for(let i=from;i<=to;i++) values[i]=Math.round(gesture.value+(value-gesture.value)*(at===gesture.at?1:(i-gesture.at)/(at-gesture.at)));
    }
    gesture.at=at;gesture.value=value;
    if(kind==='wave'){drawingDirty=true;plotWave(canvas,drawing);drawingState();}else drawBars(partials);
  }
  for(const [id,kind] of [['wave-graph','wave'],['harmonic-graph','partials']]) {
    const canvas=$(id);
    canvas.onpointerdown=event=>{
      if(busy||liveMic()||(kind==='wave'&&synthMode!=='draw')||getState().picker) return;
      event.preventDefault();canvas.setPointerCapture(event.pointerId);
      if(kind==='partials') partials=Uint16Array.from(model.harmonics);
      gesture={kind,at:null,value:0};paint(event);
    };
    canvas.onpointermove=event=>{if(gesture?.kind===kind)paint(event);};
    canvas.onpointerup=()=>{if(gesture?.kind!==kind)return;gesture=null;if(kind==='partials')run(()=>send('/api/harmonics',partials.buffer));};
    canvas.onpointercancel=()=>{gesture=null;syncSynth();};
    canvas.onlostpointercapture=()=>{if(gesture?.kind===kind){gesture=null;syncSynth();}};
  }
  $('capture-test').onclick=()=>run(async()=>{
    if(liveMic())return;
    await sampler.ensure();
    await send('/api/command',{type:4,index:getState().captureSlot%16,value:0});
    sampler.completed();
  });
  $('finish-sampling').onclick=()=>run(async()=>{
    if(liveMic())return;
    sampler.close();await action(4);
    document.querySelector('.key-scroll').scrollIntoView({behavior:'smooth',block:'center'});
  });
  async function sampleAudio(bytes, recorded=false) {
    const track=getState().editor.selected;
    const Audio=window.AudioContext||window.webkitAudioContext;
    const Offline=window.OfflineAudioContext||window.webkitOfflineAudioContext;
    if(!Audio||!Offline)throw new Error('Open this page in Safari to decode audio.');
    const decoder=new Audio();
    let decoded;
    try {decoded=await decoder.decodeAudioData(bytes);} finally {await decoder.close();}
    if((decoded.duration>10&&!recorded)||decoded.duration<.001)throw new Error('Choose a take between 1 ms and 10 seconds.');
    const offline=new Offline(1,Math.ceil(Math.min(10,decoded.duration)*36000),36000),source=offline.createBufferSource();
    source.buffer=decoded;source.connect(offline.destination);source.start();
    const rendered=await offline.startRendering(),input=rendered.getChannelData(0),pcm=new Int16Array(input.length);
    for(let i=0;i<input.length;i++)pcm[i]=Math.round(clamp(input[i],-1,1)*(input[i]<0?32768:32767));
    if(document.hidden||getState().editor.selected!==track||getState().screen!==7)throw new Error('Sampling was cancelled because the selected track or page changed.');
    await send('/api/input',pcm.buffer);sampler.completed();
  }
  $('sample-file').onchange=event=>run(async()=>{
    const file=event.target.files[0]; if(!file)return;
    try {
      if(file.size>32*1024*1024)throw new Error('Maximum file size is 32 MB.');
      await sampler.ensure();$('mic-status').textContent='Decoding the take on this device…';
      await sampleAudio(await file.arrayBuffer());
      $('mic-status').textContent='Take captured. Finish sampling to play it.';
    } finally {event.target.value='';}
  });
  function stopMic(discard=false) {
    discardRecording=discard;clearTimeout(micTimer);
    if(recorder&&recorder.state!=='inactive')recorder.stop();else resumeSound('microphone');
    micStream?.getTracks().forEach(track=>track.stop());micStream=null;
    stopMicVisual();
  }
  $('record-mic').onclick=async()=>{
    if(micPending||busy)return;
    if(recorder?.state==='recording'){stopMic();return;}
    if(!navigator.mediaDevices?.getUserMedia||!window.MediaRecorder){fail(new Error('Microphone recording is unavailable here. Open in Safari, or use Sample from Files.'));return;}
    micPending=true;discardRecording=false;recordingTrack=0;pauseSound('microphone');
    $('record-mic').disabled=true;$('mic-status').textContent='Allow microphone access to record a take.';
    try {
      await sampler.ensure();recordingTrack=getState().editor.selected;
      micStream=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:false,noiseSuppression:false,autoGainControl:false}});
      if(discardRecording||document.hidden||getState().screen!==7){stopMic(true);return;}
      startMicVisual(micStream);const chunks=[];recorder=new MediaRecorder(micStream);
      recorder.ondataavailable=event=>{if(event.data.size)chunks.push(event.data);};
      recorder.onerror=()=>{stopMic(true);fail(new Error('Microphone recording failed. Try Sample from Files.'));};
      recorder.onstop=()=>{
        stopMicVisual();
        micStream?.getTracks().forEach(track=>track.stop());micStream=null;
        $('record-mic').textContent='Record microphone';resumeSound('microphone');
        if(discardRecording){$('mic-status').textContent='Recording cancelled. Tap Record microphone to try again.';return;}
        run(async()=>{
          $('mic-status').textContent='Preparing your take…';
          await sampleAudio(await new Blob(chunks,{type:recorder.mimeType}).arrayBuffer(),true);
          $('mic-status').textContent='Take captured. Finish sampling to play it.';
        });
      };
      recorder.start();micTimer=setTimeout(()=>stopMic(),10000);
      $('record-mic').textContent='Stop recording';$('mic-status').textContent='Recording… tap Stop recording when ready. Stops automatically after 10 seconds.';
    } catch(error) {stopMic(true);resumeSound('microphone');fail(new Error(error.name==='NotAllowedError'?'Microphone access was declined. You can use Sample from Files instead.':error.message));}
    finally{micPending=false;$('record-mic').disabled=false;}
  };
  document.addEventListener('visibilitychange',()=>{if(document.hidden)stopMic(true);});
  window.addEventListener('pagehide',()=>stopMic(true));
  function startMicVisual(stream) {
    try {
      const Audio=window.AudioContext||window.webkitAudioContext,context=new Audio(),source=context.createMediaStreamSource(stream),analyser=context.createAnalyser(),silent=context.createGain();
      analyser.fftSize=256;silent.gain.value=0;source.connect(analyser);analyser.connect(silent);silent.connect(context.destination);
      const data=new Float32Array(256);micVisual={context,frame:0};context.resume().catch(()=>{});
      const paint=()=>{if(!micVisual)return;analyser.getFloatTimeDomainData(data);sampler.microphone(Array.from(data));micVisual.frame=requestAnimationFrame(paint);};paint();
    } catch(error) { /* Recording/files still work if live metering is unavailable. */ }
  }
  function stopMicVisual(){if(micVisual){cancelAnimationFrame(micVisual.frame);micVisual.context.close().catch(()=>{});micVisual=null;}sampler.stopMicrophone();}
  sampler=initSampler({send,run,action,getState,enableSound,showError,liveMic,isBusy:()=>busy});
  renderSequencer=initSequencer({request,send,run,action,getState,enableSound,showError,openCreation});
  renderPerformance=initPerformance({send,run,action,getState,enableSound,showError,openCreation:()=>$('new-track').click(),closeSampler:()=>sampler.close()});
  return function render(s) {
    if(!s.editor)return;
    model=s.editor;
    if(liveMic()&&recordingTrack&&(s.screen!==7||model.selected!==recordingTrack))stopMic(true);
    const key=JSON.stringify([model.selected,model.tracks]);
    if(trackKey!==key){trackKey=key;$('track-choice').replaceChildren(...model.tracks.map(track=>new Option(`${track.name} · ${track.type===0?'kit':track.type===2?'synth':'sample'}`,track.id)));$('track-choice').value=model.selected;}
    $('track-choice').disabled=!s.tracks||liveMic();$('new-track').disabled=liveMic()||!!s.picker||!!s.playing;
    $('sample-file').disabled=liveMic()||busy;
    $('record-mic').disabled=micPending||busy;
    $('record-mic').classList.toggle('recording',!!liveMic());
    $('getting-started').textContent=s.tracks?'Choose a track to play or edit. Add another sound with New track.':'Create your first track, or try Explore demo below.';
    const isCapture=sampler.render(s);renderSequencer(s);renderPerformance(s);
    const isMod=s.screen===1&&[3,4,5].includes(s.tab),isSynth=s.screen===1&&s.tab===6;
    for(const [id,active] of [['sample-audio',isCapture],['open-sequencer',[4,5,16].includes(s.screen)],['open-mixer',s.screen===2],['open-arp',s.screen===15],['open-synthesis',isSynth],['open-envelope',isMod&&s.tab===3],['open-filter-envelope',isMod&&s.tab===4],['open-lfo',isMod&&s.tab===5]])$(id).setAttribute('aria-pressed',String(active));
    const visible=isMod||isSynth||isCapture;
    $('visual-editor').hidden=!visible;
    $('mod-editor').hidden=!isMod;$('synth-editor').hidden=!isSynth;$('sampling-editor').hidden=!isCapture;
    $('scope-label').hidden=isCapture||isSynth||s.instrumentType===2;
    $('preview-note').hidden=isCapture;
    $('preview-note').disabled=!!s.picker;
    const scope=JSON.stringify([model.selected,model.slots,s.scope]);
    if(scope!==scopeKey){scopeKey=scope;$('edit-scope').replaceChildren(new Option('All samples',0),...model.slots.map(slot=>new Option(`Sample ${slot}`,slot)));$('edit-scope').value=s.scope;}
    if(isMod){$('editor-title').textContent=s.tab===3?'Amplitude envelope':s.tab===4?'Filter envelope':'Low frequency oscillator';$('editor-help').textContent=s.instrumentType===2?`${s.instrument} · Shape this synth’s sound. Use Hold to hear to audition.`:`${s.instrument} · Changes apply to the selected scope. ${s.scope?'The graph shows this sample.':'The graph shows the first sample.'} Use Hold to hear to audition.`;modEditor(s);}
    if(isSynth){$('editor-title').textContent='FZ-inspired wave synthesis';$('editor-help').textContent='48 sine harmonics, each with a level from 0 to 256. Presets and applied drawings replace this instrument’s C4 cycle; Undo restores the previous sound.';syncSynth();}
    if(isCapture){$('editor-title').textContent='Sampler';$('editor-help').textContent='Choose Simple or Multi, then select a slot and capture. Switching types selects another instrument; your existing sounds stay in the project.';}
    for(const id of ['sample-audio','open-sequencer','open-synthesis','open-envelope','open-filter-envelope','open-lfo','open-mixer','open-arp','finish-sampling'])$(id).disabled=liveMic()||!!s.picker;
  };
}
