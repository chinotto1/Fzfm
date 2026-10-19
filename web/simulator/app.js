import {request, isBrowser} from './backend.js';
import {initWorkspace} from './workspace.js';
let renderWorkspace=()=>{};
'use strict';
const $=id=>document.getElementById(id);
const whites=[0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26];
const buttonNames=['OCT−','OCT+','FX','SCL / SEL','ENV','LFO','EDIT','GLO','HOME','SAVE','ARP','SEQ','PLAY','REC'];
let state={},audioContext=null,sound=false,soundWanted=true,audioEpoch=0,audioBusy=false,nextAudio=0,tickBusy=false,sequence=Promise.resolve(),activeKeys=new Set(),activeButtons=new Set(),screenLoading=false,initialized=false;
const audioSources=new Set(),audioPauses=new Set(),encoders=new Map();
document.body.classList.add('loading');
const contexts={'PIANO ROLL':['See your MIDI notes.','Knob 1 moves the time cursor; knobs 2–4 edit pitch, length and velocity. SCL adds/deletes. EDIT toggles time-move mode. PRESETS selects performed notes, OCT scrolls pitch, and SEQ cycles to steps/patterns/song.'],ARPEGGIATOR:['Turn a chord into a phrase.','ARP switches between two pages. Knobs 1–4 edit the four visible fields directly. It follows BPM even with PLAY stopped. Hold HOME opens commands.'],HOME:['A sound under every key.','ALGORITHM selects a track. The four knobs control cutoff, resonance, volume and pan. SCL opens actions; hold HOME opens commands.'],EDIT:['One editor, every detail.','EDIT cycles the pages for this instrument. SELECT chooses ALL or one sample. Knob 1 moves through rows; knobs 2–4 edit. SCL enters a value or applies an action.'],CAPTURE:['Catch a sound. Keep playing.','Send a test hit or use the host input. Adjust the visible settings; SCL retries or skips a slot. Press flashing OCT+ to go straight to playable HOME.'],STEP:['Put it on the steps.','Sixteen white keys toggle steps. SCL picks a note. The first two black keys are rest and tie. Hold HOME for commands; this opens step detail and pattern settings.'],PATTERNS:['A pattern for every part.','Select an empty slot to create it. Hold two white keys for a range chain. Hold HOME for commands; this includes ordered-chain append/play and copy/paste.'],SONG:['Turn patterns into a song.','SCL creates or launches a section. Knobs edit its bars, repeats and selected-track pattern. Hold HOME for commands; this inserts, duplicates, moves and mutes sections.'],MIXER:['Give every sound its place.','Knob 1 selects a track; 2 sets level, 3 pan, 4 the selected send. GLO cycles sends 1–4. SCL toggles track mute; hold GLO toggles solo.'],FX:['Four shared colours.','FX cycles the four send processors. Knob 1 chooses a parameter; the display shows which knobs edit it. Existing tails keep running.'],SOURCE:['Start from a source.','Synth oscillators offer additive, preset waves or drawing. Sampled instruments open USB capture directly. Use knobs 2 and 3 for a preset’s shape and point count.']};

function update(s){state=s;renderWorkspace(s);$('transport').textContent=s.recording?'RECORDING':s.arranging?'ARRANGING':s.playing?'PLAYING':'STOPPED';$('tempo').textContent=s.bpm.toFixed(1);$('screen-title').textContent=s.title;$('storage').textContent=s.dirty?'UNSAVED':'SAVED';$('instrument').textContent=s.instrument;$('key-help').textContent=innerWidth<=430?'Scroll keys · Z X C V B N M':'Z X C V B N M · mouse / touch';$('status').textContent=s.status==='OK'?'':(s.status||'');$('status').hidden=!$('status').textContent;for(const k of ['tracks','regions','voices','sections'])$(k).textContent=s[k];$('test-hit').disabled=s.screen!==7;$('demo').hidden=s.tracks>0;const c=contexts[s.title]||[s.title.charAt(0)+s.title.slice(1).toLowerCase(),'Use knob 1 to choose a row and SCL to apply. HOME cancels a picker first, then returns to the preceding screen.'];$('context-title').textContent=c[0];$('context').textContent=c[1];document.querySelector('[data-index="1"]').classList.toggle('flash',s.screen===7);const values=s.screen===0?[s.cutoff.toFixed(2),s.resonance.toFixed(2),s.volume.toFixed(2),s.pan.toFixed(2)]:['SEE DISPLAY','SEE DISPLAY','SEE DISPLAY','SEE DISPLAY'];document.querySelectorAll('.knobs .knob-value').forEach((e,i)=>e.textContent=values[i]);
 const roles=['0','1','2','3','4','5','6','7','8','9','⌫','−','.','ESC','OK',''];const cats=['DRUMS','SYNTH','PAD','LEAD','FX','BASS','KEYS','PERC','LOOP'];document.querySelectorAll('.keyboard button').forEach(e=>{const i=+e.dataset.key,w=whites.indexOf(i);const midi=Math.max(0,Math.min(127,(s.instrumentType===0?s.baseNote:53)+i+s.octave*12));const note=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'][midi%12]+(Math.floor(midi/12)-1);e.firstChild.textContent=s.picker===1&&w>=0?roles[w]:s.picker===6&&w>=0?(cats[w]||''):note;});
 syncEncoders(s);refreshScreen();}
async function refreshScreen(){if(screenLoading)return;screenLoading=true;try{const b=await(await request('/api/screen.png')).blob();const url=URL.createObjectURL(b);const im=$('screen');const old=im.dataset.url;im.src=url;im.dataset.url=url;if(old)URL.revokeObjectURL(old);}finally{screenLoading=false;}}
function command(type,index,value){sequence=sequence.then(async()=>{const r=await(await request('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type,index,value})})).json();update(r.state);}).catch(showError);return sequence;}
function showError(e){$('status').textContent=e.message;$('status').hidden=false;$('runtime-message').textContent=e.message;}
function bindPress(el,type,index){el.addEventListener('pointerdown',e=>{e.preventDefault();el.setPointerCapture(e.pointerId);el.classList.add('down');(type===0?activeButtons:activeKeys).add(index);command(type,index,1);});for(const name of ['pointerup','pointercancel','lostpointercapture'])el.addEventListener(name,()=>{const set=type===0?activeButtons:activeKeys;if(!set.has(index))return;set.delete(index);el.classList.remove('down');command(type,index,0);});}
buttonNames.forEach((name,i)=>{const b=document.createElement('button');b.textContent=name;b.dataset.index=i;b.title=name+' · press / release; HOME, GLO, EDIT, REC and SCL also support a hold';bindPress(b,0,i);$('buttons').append(b);});
for(let i=0;i<27;i++){const b=document.createElement('button');b.dataset.key=i;b.append(document.createTextNode(''),Object.assign(document.createElement('small'),{textContent:String(i)}));if(!whites.includes(i)){b.className='black';b.style.left=`${whites.filter(w=>w<i).length*100/16-1.9}%`;}bindPress(b,1,i);$('keys').append(b);}
function encoder(parent,index,name){
 const wrap=document.createElement('div');wrap.className='encoder';
 const wheel=document.createElement('div');wheel.className='wheel';
 const dial=document.createElement('span');dial.className='dial';dial.dataset.encoder=index;
 dial.setAttribute('role','slider');dial.setAttribute('aria-label',name);
 dial.setAttribute('aria-orientation','vertical');dial.tabIndex=0;
 dial.title='Drag up or down, scroll, or use arrow keys. Shift + arrow or Page Up/Down: 10 steps.';
 const view={dial,name,position:0};encoders.set(index,view);
 let drag=null,wheelRemainder=0;
 const turn=delta=>{
  if(!delta)return;
  view.position=Math.max(-1000000,Math.min(1000000,view.position+delta));
  const current=parseFloat(dial.style.getPropertyValue('--dial-angle'))||0;
  const home=state.screen===0&&index>=2&&index<=5;
  const span=Number(dial.getAttribute('aria-valuemax'))-Number(dial.getAttribute('aria-valuemin'));
  const angle=current+delta*(home?270*.01/span:6);
  dial.style.setProperty('--dial-angle',`${home?Math.max(-135,Math.min(135,angle)):angle}deg`);
  command(2,index,delta);
 };
 const end=event=>{
  if(event&&drag&&event.pointerId!==undefined&&event.pointerId!==drag.id)return;
  drag=null;dial.classList.remove('dragging');
 };
 dial.addEventListener('pointerdown',event=>{
  if(drag||event.button!==0)return;
  event.preventDefault();dial.focus({preventScroll:true});dial.setPointerCapture(event.pointerId);
  drag={id:event.pointerId,y:event.clientY};dial.classList.add('dragging');
 });
 dial.addEventListener('pointermove',event=>{
  if(!drag||event.pointerId!==drag.id)return;
  event.preventDefault();
  const delta=Math.trunc((drag.y-event.clientY)/4);
  if(delta){drag.y-=delta*4;turn(delta);}
 });
 for(const event of ['pointerup','pointercancel','lostpointercapture'])dial.addEventListener(event,end);
 dial.addEventListener('blur',end);window.addEventListener('blur',()=>end());
 dial.addEventListener('wheel',event=>{
  if(event.ctrlKey)return;
  event.preventDefault();
  const pixels=event.deltaY*(event.deltaMode===1?16:event.deltaMode===2?innerHeight:1);
  wheelRemainder-=pixels/40;
  const delta=Math.trunc(wheelRemainder);
  if(delta){wheelRemainder-=delta;turn(delta);}
 },{passive:false});
 dial.addEventListener('keydown',event=>{
  const directions={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1,PageUp:10,PageDown:-10};
  if(!(event.key in directions))return;
  event.preventDefault();event.stopPropagation();turn(directions[event.key]*(event.shiftKey?10:1));
 });
 wheel.append(dial);
 wrap.append(wheel,Object.assign(document.createElement('span'),{className:'encoder-name',textContent:name}),Object.assign(document.createElement('div'),{className:'knob-value'}));parent.append(wrap);
}
function syncEncoders(s){
 const home=[['cutoff',0,1],['resonance',0,.98],['volume',0,2],['pan',-1,1]];
 for(const [index,view] of encoders){
  const parameter=s.screen===0&&index>=2&&index<=5?home[index-2]:null;
  const [field,min,max]=parameter||['',-1000000,1000000];
  const value=parameter?s[field]:view.position;
  view.dial.setAttribute('aria-valuemin',min);view.dial.setAttribute('aria-valuemax',max);
  view.dial.setAttribute('aria-valuenow',value);
  view.dial.setAttribute('aria-valuetext',parameter?`${field} ${value.toFixed(2)}`:index===1?s.instrument:`${view.position} steps; see display`);
  view.dial.style.setProperty('--dial-angle',`${parameter?-135+270*Math.max(0,Math.min(1,(value-min)/(max-min))):(view.position%60)*6}deg`);
 }
}
encoder($('track-encoder'),1,'ALGORITHM');encoder($('select-encoder'),0,'SELECT');encoder($('preset-encoder'),6,'PRESETS');for(let i=0;i<4;i++)encoder($('knobs'),i+2,`KNOB ${i+1}`);
if(isBrowser)contexts.CAPTURE=['Catch a sound. Keep playing.','Send a test hit to try the detector. For your own sounds, import a PCM16 WAV from Files. Use the display settings and press flashing OCT+ for playable HOME.'];
async function poll(){if(!initialized||document.hidden)return;try{if(!sound&&!tickBusy){tickBusy=true;await command(3,0,1188);tickBusy=false;}else update(await(await request('/api/state')).json());}catch(e){tickBusy=false;showError(e);}}
function audioButton(){
 const button=$('audio');
 button.textContent=sound?'Sound on':!soundWanted?'Sound off':audioPauses.size?'Sound paused':'Sound ready';
 button.classList.toggle('enabled',sound);button.setAttribute('aria-pressed',String(soundWanted));
}
function haltSound(){
 sound=false;audioEpoch++;
 for(const source of audioSources){try{source.stop();source.disconnect();}catch(error){}}
 audioSources.clear();nextAudio=0;audioButton();
}
function stopSound(){
 soundWanted=false;haltSound();
 if(audioContext?.state==='running')audioContext.suspend().catch(()=>{});
}
function pauseSound(reason){
 audioPauses.add(reason);haltSound();
 if(audioContext?.state==='running')audioContext.suspend().catch(()=>{});
}
function resumeSound(reason){
 audioPauses.delete(reason);
 if(soundWanted&&!audioPauses.size&&!document.hidden)enableSound().catch(()=>{});
}
function syncAudioState(){
 const running=soundWanted&&!audioPauses.size&&!document.hidden&&audioContext?.state==='running';
 if(!running){haltSound();return;}
 if(!sound){sound=true;nextAudio=audioContext.currentTime;audioButton();audioLoop();}
}
async function audioLoop(){
 if(!initialized||!sound||audioBusy||document.hidden)return;
 audioBusy=true;const epoch=audioEpoch;
 try{
  while(sound&&audioContext.state==='running'&&nextAudio<audioContext.currentTime+.055){
   const data=new Float32Array(await(await request('/api/audio')).arrayBuffer());
   if(!sound||epoch!==audioEpoch)return;
   const frames=data.length/2,buffer=audioContext.createBuffer(2,frames,36000);
   for(let c=0;c<2;c++){const out=buffer.getChannelData(c);for(let i=0;i<frames;i++)out[i]=data[2*i+c];}
   const source=audioContext.createBufferSource();source.buffer=buffer;source.connect(audioContext.destination);
   audioSources.add(source);source.onended=()=>{audioSources.delete(source);source.disconnect();};
   nextAudio=Math.max(nextAudio,audioContext.currentTime+.008);source.start(nextAudio);nextAudio+=frames/36000;
  }
 }catch(error){stopSound();showError(error);}finally{audioBusy=false;}
}
async function enableSound(){
 soundWanted=true;audioButton();
 if(audioPauses.size||document.hidden)return;
 const Audio=window.AudioContext||window.webkitAudioContext;
 if(!Audio)throw new Error('Audio is unavailable in this browser. Open this page in Safari.');
 if(!audioContext||audioContext.state==='closed'){
  audioContext=new Audio({latencyHint:'interactive'});audioContext.onstatechange=syncAudioState;
 }
 if(audioContext.state!=='running')await audioContext.resume();
 syncAudioState();
}
$('audio').onclick=()=>{if(sound||(soundWanted&&audioPauses.size))stopSound();else enableSound().catch(showError);};
function autoSound(event){
 if(!soundWanted||sound||audioPauses.size||event.target.closest?.('#audio,#record-mic'))return;
 enableSound().catch(()=>{});
}
for(const type of ['pointerdown','pointerup','touchend','keydown'])
 document.addEventListener(type,autoSound,{capture:true,passive:type==='touchend'});
audioButton();enableSound().catch(()=>{});
let previewHeld=false,previewKey=7;
async function previewDown(){
 if(previewHeld||state.picker)return;
 previewHeld=true;previewKey=state.instrumentType===0?Math.max(0,(state.scope||1)-1):7;
 try{await enableSound();if(previewHeld){activeKeys.add(previewKey);command(1,previewKey,1);}}catch(e){previewHeld=false;showError(e);}
}
function previewUp(){if(!previewHeld)return;previewHeld=false;activeKeys.delete(previewKey);command(1,previewKey,0);}
$('preview-note').onpointerdown=e=>{e.preventDefault();e.currentTarget.setPointerCapture(e.pointerId);previewDown();};
for(const event of ['pointerup','pointercancel','lostpointercapture'])$('preview-note').addEventListener(event,previewUp);
$('preview-note').onkeydown=e=>{if([' ','Enter'].includes(e.key)){e.preventDefault();e.stopPropagation();previewDown();}};
$('preview-note').onkeyup=e=>{if([' ','Enter'].includes(e.key)){e.preventDefault();e.stopPropagation();previewUp();}};
$('preview-note').onblur=previewUp;

$('demo').onclick=async()=>{try{$('runtime-message').textContent='Preparing your demo kit and synth…';const r=await(await request('/api/demo',{method:'POST',body:''})).json();update(r.state);$('runtime-message').textContent=isBrowser?'Your projects stay on this device. Export important projects to Files.':'';}catch(e){showError(e);}};
$('quick-save').onclick=()=>command(0,9,1).then(()=>command(0,9,0));
$('test-hit').onclick=()=>$('capture-test').click();
for(const type of ['wave','cycle','restore'])$(type).onchange=async e=>{const file=e.target.files[0];if(!file)return;if(file.size>32*1024*1024){showError(new Error('Maximum file size is 32 MB'));return;}if(type==='restore'&&!confirm('Replace the current project with this backup?'))return;try{const r=await(await request('/api/'+type,{method:'POST',body:await file.arrayBuffer()})).json();update(r.state);if(r.result)showError(new Error('Import rejected; previous project retained (code '+r.result+')'));}catch(error){showError(error);}e.target.value='';};
$('backup').onclick=async()=>{try{const b=await(await request('/api/backup')).blob();const u=URL.createObjectURL(b);const a=document.createElement('a');a.href=u;a.download='fm1-project.fm1p';a.click();setTimeout(()=>URL.revokeObjectURL(u),1000);}catch(e){showError(e);}};
const controlKeys={Escape:8,Enter:3,' ':12,e:6,r:13,g:7,f:2,a:10,s:9};const noteKeys={z:0,x:2,c:4,v:6,b:7,n:9,m:11,',':12,'.':14,'/':16};
window.onkeydown=e=>{if(e.repeat||e.ctrlKey||e.metaKey||e.altKey)return;if(e.target.matches('input,select,textarea,button,a')||e.target.closest('[role=application]'))return;if(e.key in controlKeys){e.preventDefault();const i=controlKeys[e.key];activeButtons.add(i);command(0,i,1);}else if(e.key in noteKeys){e.preventDefault();const i=noteKeys[e.key];activeKeys.add(i);command(1,i,1);}};
window.onkeyup=e=>{if(e.key in controlKeys){const i=controlKeys[e.key];if(activeButtons.delete(i))command(0,i,0);}if(e.key in noteKeys){const i=noteKeys[e.key];if(activeKeys.delete(i))command(1,i,0);}};
function releaseInputs(){previewUp();for(const i of activeKeys)command(1,i,0);for(const i of activeButtons)command(0,i,0);activeKeys.clear();activeButtons.clear();}
window.addEventListener('blur',releaseInputs);
window.addEventListener('pagehide',()=>{releaseInputs();pauseSound('hidden');});
window.addEventListener('pageshow',()=>resumeSound('hidden'));
document.addEventListener('visibilitychange',()=>{if(document.hidden){releaseInputs();pauseSound('hidden');}else resumeSound('hidden');});
window.fm1={request,state:()=>state,backend:isBrowser?'wasm':'native',stopSound};
renderWorkspace=initWorkspace({request,update,getState:()=>state,pauseSound,resumeSound,enableSound,showError});
setInterval(poll,33);setInterval(audioLoop,12);
request('/api/state').then(r=>r.json()).then(s=>{update(s);initialized=true;if(soundWanted)enableSound().catch(()=>{});document.body.classList.remove('loading');$('runtime-message').textContent=isBrowser?(s.persistence?'Your projects stay on this device. Export important projects to Files.':'Local saving is unavailable. Export project to Files before leaving.') : '';}).catch(e=>{document.body.classList.remove('loading');showError(e);});
