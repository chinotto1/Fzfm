// SPDX-License-Identifier: GPL-3.0-or-later
// Touch piano roll. C owns notes, timing, undo and playback; JS draws gestures.
const $=id=>document.getElementById(id),PPQN=960,ROW=24,LEFT=52;
const clamp=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
const names=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
const noteName=n=>names[n%12]+(Math.floor(n/12)-1);
export function initSequencer({request,send,run,action,getState,enableSound,showError,openCreation}) {
 let state={},pattern=null,notes=[],selected=null,gesture=null,tool='draw',page=0,lastPattern=0;
 let fetched='',fetching=false,sequence=0,pendingSelection=null,patternKey='',timingKey='',notesRevision=-1,lastOctave=null;
 const canvas=$('piano-roll'),scroll=$('roll-scroll'),keys=$('roll-keys');
 const activeKeys=new Map();
 const bar=()=>PPQN*4,windowBars=()=>+$('roll-window').value,from=()=>page*windowBars()*bar();
 const pxBeat=()=>128*+$('roll-zoom').value,snap=()=>+$('roll-snap').value;
 const to=()=>Math.min(pattern?.length||bar(),from()+windowBars()*bar());
 const xFor=t=>LEFT+(t-from())/PPQN*pxBeat();
 function seq(operation,options={}) {
  const p=getState().editor?.pattern;
  return send('/api/sequence',{operation,index:0,a:0,b:0,note:60,velocity:100,revision:p?.revision||0,pattern:p?.id||0,...options});
 }
 function sync(id,value){if(document.activeElement!==$(id))$(id).value=value;}
 function select(n){selected=n;inspector();draw();}
 function inspector() {
  const n=selected;
  for(const [id,value] of [['note-pitch',n?.note??60],['note-start',(n?.tick??from())/PPQN],['note-length',(n?.duration??(snap()||240))/PPQN],['note-velocity',n?.velocity??100]])sync(id,Math.round(value*1e6)/1e6);
  $('roll-delete').disabled=!n;
  $('note-description').textContent=n?`${noteName(n.note)} · beat ${(n.tick/PPQN+1).toFixed(3)} · velocity ${n.velocity}`:'No note selected · tap the grid or + Note';
 }
 function geometry() {
  const length=Math.max(1,to()-from());
  const width=Math.ceil(LEFT+length/PPQN*pxBeat());
  if(canvas.width!==width)canvas.width=width;
  $('roll-content').style.width=width+'px';$('roll-content').style.height=128*ROW+'px';
  $('roll-range').textContent=`Bar ${page*windowBars()+1}${windowBars()>1?'–'+Math.min(Math.ceil((pattern?.length||bar())/bar()),(page+1)*windowBars()):''}`;
  $('roll-previous').disabled=page===0;$('roll-next').disabled=to()>=(pattern?.length||bar());
 }
 function draw() {
  if(!pattern)return;geometry();
  const ctx=canvas.getContext('2d'),width=canvas.width;
  ctx.clearRect(0,0,width,canvas.height);
  for(let n=0;n<128;n++) {
   const y=(127-n)*ROW,black=[1,3,6,8,10].includes(n%12);
   ctx.fillStyle=black?'#10131f':'#1b2030';ctx.fillRect(LEFT,y,width-LEFT,ROW);
   ctx.strokeStyle=n%12===0?'#657596':'#303b52';ctx.beginPath();ctx.moveTo(LEFT,y+ROW);ctx.lineTo(width,y+ROW);ctx.stroke();
  }
  const grid=snap()||240;
  for(let tick=from();tick<=to()+.001;tick+=grid) {
   const x=xFor(tick);ctx.strokeStyle=tick%bar()===0?'#b9c2d5':tick%PPQN===0?'#657596':'#38445c';ctx.lineWidth=tick%bar()===0?2:1;
   ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,canvas.height);ctx.stroke();
  }
  for(const n of notes) {
   if(gesture&&gesture.original?.index===n.index)continue;
   drawNote(ctx,n,n.index===selected?.index,false);
  }
  if(gesture?.draft)drawNote(ctx,gesture.draft,true,true);
  const k=keys.getContext('2d');
  for(let n=0;n<128;n++) {
   const y=(127-n)*ROW,black=[1,3,6,8,10].includes(n%12);
   k.fillStyle=black?'#10131f':'#ebedf5';k.fillRect(0,y,LEFT,ROW-1);
   k.fillStyle=black?'#f5f5ef':'#10131f';k.font='700 11px system-ui';k.fillText(noteName(n),6,y+16);
  }
 }
 function drawNote(ctx,n,isSelected,draft) {
  const x=xFor(n.tick),y=(127-n.note)*ROW+3,w=Math.max(5,n.duration/PPQN*pxBeat()),h=ROW-6;
  ctx.fillStyle=draft?'#ffe4a1':isSelected?'#f4bf35':`rgba(82,203,128,${.45+n.velocity/127*.5})`;
  ctx.fillRect(x+1,y,w-2,h);ctx.strokeStyle=isSelected?'#fff':'#87e6a8';ctx.strokeRect(x+1,y,w-2,h);
  if(w>20){ctx.fillStyle='#080910';ctx.font='700 11px system-ui';ctx.fillText(noteName(n.note),x+5,y+13);}
  if(isSelected){ctx.fillStyle='#fff';ctx.fillRect(x+w-5,y+2,3,h-4);}
 }
 async function refresh() {
  if(fetching||!pattern)return;
  const key=`${pattern.id}:${pattern.revision}:${from()}:${to()}`;
  if(key===fetched)return;fetching=true;const token=++sequence;
  try {
   const response=await(await request('/api/notes',{method:'POST',body:JSON.stringify({from:from(),to:Math.max(from()+1,to())})})).json();
   if(token!==sequence||response.pattern!==getState().editor?.pattern?.id)return;
   if(response.error)throw new Error(response.error);
   notes=response.notes;notesRevision=response.revision;fetched=key;
   if(pendingSelection){const target=pendingSelection;selected=notes.find(n=>n.note===target.note&&Math.abs(n.rawTick-target.tick)<.1)||null;pendingSelection=null;}
   else if(selected)selected=notes.find(n=>n.id===selected.id&&n.note===selected.note&&Math.abs(n.rawTick-selected.rawTick)<.1)||null;
   $('roll-count').textContent=`${response.total} notes${response.truncated?' · zoom to fewer bars to show all notes':''}`;
   inspector();draw();
  } catch(error){showError(error);}finally{fetching=false;}
 }
 function windowChange(){page=clamp(page,0,Math.max(0,Math.ceil((pattern?.length||bar())/(windowBars()*bar()))-1));fetched='';draw();refresh();}
 $('open-sequencer').onclick=()=>run(async()=>{
  if(!getState().tracks){openCreation();return;}
  await action(9);$('sequencer-editor').scrollIntoView({behavior:'smooth',block:'start'});
 });
 $('seq-close').onclick=()=>run(async()=>{await send('/api/command',{type:0,index:8,value:1});await send('/api/command',{type:0,index:8,value:0});});
 $('seq-play').onclick=()=>run(()=>seq(10));$('seq-record').onclick=()=>run(()=>seq(7));
 $('seq-retro').onclick=()=>run(()=>seq(8,{a:+$('seq-lookback').value}));
 $('seq-undo').onclick=()=>run(async()=>{await seq(11);fetched='';refresh();});
 $('seq-quantize').onchange=e=>run(()=>seq(0,{a:+e.target.value}));
 $('seq-pattern').onchange=e=>run(()=>seq(6,{index:+e.target.value}));
 $('seq-timing').onchange=e=>run(()=>seq(9,{index:+e.target.value}));
 for(const [id,operation,multiplier] of [['seq-bpm',4,1],['seq-bars',5,bar()],['seq-swing',12,.01]])$(id).onchange=e=>run(async()=>{if(!e.target.reportValidity())return;await seq(operation,{a:+e.target.value*multiplier});});
 for(const next of ['draw','resize','erase','pan'])$('roll-'+next).onclick=()=>{
  tool=next;for(const name of ['draw','resize','erase','pan'])$('roll-'+name).setAttribute('aria-pressed',String(name===next));
  canvas.style.touchAction=next==='pan'?'pan-x pan-y':'none';keys.style.touchAction=next==='pan'?'pan-y':'none';
 };
 $('roll-snap').onchange=draw;$('roll-zoom').onchange=()=>{draw();};
 $('roll-window').onchange=()=>{page=0;windowChange();};
 $('roll-previous').onclick=()=>{page--;scroll.scrollLeft=0;windowChange();};
 $('roll-next').onclick=()=>{page++;scroll.scrollLeft=0;windowChange();};
 scroll.onscroll=()=>{keys.style.transform=`translateX(${scroll.scrollLeft}px)`;};
 function position(event) {
  const rect=canvas.getBoundingClientRect();
  return {x:event.clientX-rect.left,tick:from()+(event.clientX-rect.left-LEFT)/pxBeat()*PPQN,note:clamp(127-Math.floor((event.clientY-rect.top)/ROW),0,127)};
 }
 const snapped=value=>snap()?Math.round(value/snap())*snap():Math.round(value);
 canvas.onpointerdown=event=>{
  if(tool==='pan'||!pattern||fetching||notesRevision!==pattern.revision||pattern.frozen||getState().recording||getState().armed)return;
  event.preventDefault();canvas.setPointerCapture(event.pointerId);const p=position(event);
  if(p.x<LEFT)return;
  const n=[...notes].reverse().find(n=>n.note===p.note&&p.tick>=n.tick&&p.tick<n.tick+n.duration);
  if(n&&tool==='erase'){select(n);run(()=>remove());return;}
  if(n){select(n);gesture={kind:tool==='resize'||Math.abs(p.x-xFor(n.tick+n.duration))<9?'resize':'move',original:n,draft:{...n},start:p,revision:notesRevision};}
  else if(tool==='draw') {
   const tick=clamp(snap()?Math.floor(p.tick/snap())*snap():Math.round(p.tick),from(),pattern.length-1);
   gesture={kind:'add',draft:{tick,note:p.note,duration:Math.min(snap()||240,pattern.length-tick),velocity:100},start:p,revision:notesRevision};selected=null;
  }
  draw();
 };
 canvas.onpointermove=event=>{
  if(!gesture)return;const p=position(event),g=gesture,d=g.draft;
  if(g.kind==='move'){d.tick=clamp(g.original.tick+snapped(p.tick-g.start.tick),0,pattern.length-g.original.duration);d.note=clamp(g.original.note+p.note-g.start.note,0,127);}
  else d.duration=clamp(snapped(p.tick-d.tick),Math.min(snap()||1,pattern.length-d.tick),pattern.length-d.tick);
  draw();
 };
 async function commit(d,operation,index=0,revision=notesRevision) {
  pendingSelection={note:d.note,tick:d.tick};
  await seq(operation,{index,a:d.tick,b:d.duration,note:d.note,velocity:d.velocity,revision});
  fetched='';await refresh();
 }
 canvas.onpointerup=()=>{
  const g=gesture;if(!g)return;gesture=null;
  if(g.kind!=='add'&&g.draft.tick===g.original.tick&&g.draft.duration===g.original.duration&&g.draft.note===g.original.note){draw();return;}
  run(()=>commit(g.draft,g.kind==='add'?1:2,g.original?.index||0,g.revision));
 };
 for(const event of ['pointercancel','lostpointercapture'])canvas.addEventListener(event,()=>{if(gesture){gesture=null;draw();}});
 async function remove(){if(!selected)return;const old=selected;selected=null;pendingSelection=null;await seq(3,{index:old.index,a:old.rawTick,b:old.rawDuration,note:old.note,velocity:old.velocity,revision:notesRevision});fetched='';await refresh();}
 $('roll-delete').onclick=()=>run(remove);
 $('roll-add').onclick=()=>run(()=>commit({note:+$('note-pitch').value,tick:clamp(+$('note-start').value*PPQN,0,pattern.length-1),duration:Math.min(+$('note-length').value*PPQN,pattern.length-(+$('note-start').value*PPQN)),velocity:+$('note-velocity').value},1));
 for(const id of ['note-pitch','note-start','note-length','note-velocity'])$(id).onchange=()=>run(async()=>{
  if(!selected)return;
  if(!['note-pitch','note-start','note-length','note-velocity'].every(id=>$(id).reportValidity()))return;
  await commit({...selected,note:+$('note-pitch').value,tick:id==='note-start'?+$('note-start').value*PPQN:selected.rawTick,duration:id==='note-length'?+$('note-length').value*PPQN:selected.rawDuration,velocity:+$('note-velocity').value},2,selected.index);
 });
 for(const [id,direction] of [['roll-note-prev',-1],['roll-note-next',1]])$(id).onclick=()=>{
  if(!notes.length)return;const index=selected?notes.findIndex(n=>n.index===selected.index):0;
  select(notes[clamp(index+direction,0,notes.length-1)]);
 };
 canvas.onkeydown=event=>{
  if(!selected)return;
  if(['Delete','Backspace'].includes(event.key)){event.preventDefault();event.stopPropagation();run(remove);return;}
  if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key))return;
  event.preventDefault();event.stopPropagation();const d={...selected,tick:selected.rawTick,duration:selected.rawDuration},amount=snap()||1;
  if(event.key==='ArrowUp')d.note=clamp(d.note+1,0,127);
  if(event.key==='ArrowDown')d.note=clamp(d.note-1,0,127);
  if(event.key==='ArrowLeft')event.shiftKey?d.duration=Math.max(1,d.duration-amount):d.tick=Math.max(0,d.tick-amount);
  if(event.key==='ArrowRight')event.shiftKey?d.duration=Math.min(pattern.length-d.tick,d.duration+amount):d.tick=Math.min(pattern.length-d.duration,d.tick+amount);
  run(()=>commit(d,2,selected.index));
 };
 keys.onpointerdown=async event=>{
  if(tool==='pan')return;event.preventDefault();keys.setPointerCapture(event.pointerId);
  const rect=keys.getBoundingClientRect(),note=clamp(127-Math.floor((event.clientY-rect.top)/ROW),0,127);
  activeKeys.set(event.pointerId,note);
  try{await enableSound();if(activeKeys.has(event.pointerId))await seq(13,{note});}catch(error){showError(error);}
 };
 function keyUp(event){const note=activeKeys.get(event.pointerId);if(note===undefined)return;activeKeys.delete(event.pointerId);if([...activeKeys.values()].includes(note))return;seq(14,{note,pattern:0}).catch(showError);}
 for(const name of ['pointerup','pointercancel','lostpointercapture'])keys.addEventListener(name,keyUp);
 function release(){for(const [pointerId] of activeKeys)keyUp({pointerId});}
 window.addEventListener('blur',release);window.addEventListener('pagehide',release);
 return function render(s) {
  state=s;pattern=s.editor?.pattern;
  const visible=!!pattern&&[4,5,16].includes(s.screen);$('sequencer-editor').hidden=!visible;
  const octaveShift=lastOctave===null?0:s.octave-lastOctave;lastOctave=s.octave;
  if(!visible){if(activeKeys.size)release();return;}
  if(pattern.id!==lastPattern){lastPattern=pattern.id;page=0;fetched='';selected=null;notes=[];notesRevision=-1;scroll.scrollLeft=0;scroll.scrollTop=(127-((s.instrumentType===0?s.baseNote:60)+s.octave*12)-5)*ROW;}
  else if(octaveShift)scroll.scrollTop-=octaveShift*12*ROW;
  if(page*windowBars()*bar()>=pattern.length){page=0;fetched='';}
  $('seq-play').textContent=s.playing?'Stop':'Play';$('seq-record').textContent=s.armed||s.recording?'Finish recording':'Record';
  $('seq-record-status').textContent=s.recording?'Recording your notes…':s.armed?'Armed · play a key to begin':s.playing?'Playing':'Stopped';
  $('seq-record').classList.toggle('recording',!!s.armed||!!s.recording);
  const nextPatternKey=JSON.stringify([s.editor.selected,pattern.slot,pattern.queued]);
  if(patternKey!==nextPatternKey){patternKey=nextPatternKey;$('seq-pattern').replaceChildren(...Array.from({length:32},(_,i)=>new Option(`Pattern ${i+1}${i===pattern.queued?' · queued':''}`,i)));$('seq-pattern').value=pattern.slot;}
  if(![...$('seq-quantize').options].some(o=>+o.value===pattern.quantize))$('seq-quantize').add(new Option(`Custom · ${pattern.quantize} ticks`,pattern.quantize));
  sync('seq-quantize',pattern.quantize);sync('seq-bars',pattern.length/bar());sync('seq-bpm',s.bpm.toFixed(1));sync('seq-swing',Math.round(pattern.swing*100));
  $('timing-label').hidden=!s.editor.timing?.length;
  const timing=JSON.stringify(s.editor.timing);
  if(timingKey!==timing){timingKey=timing;$('seq-timing').replaceChildren(...(s.editor.timing||[]).map((c,i)=>new Option(`${c.bpm.toFixed(1)} BPM · ${c.beats} beats · ${Math.round(c.confidence*100)}%`,i)));}
  $('seq-help').textContent=pattern.frozen?'This pattern is frozen. Unfreeze it to edit notes.':s.armed||s.recording?'Play the keys on the left or the keyboard below. Finish recording to edit the take.':'Tap to add; drag to move; drag the right edge to resize. Pan scrolls the roll. Quantize Off restores original timing.';
  const x=xFor(pattern.playhead);$('roll-playhead').hidden=pattern.playhead<from()||pattern.playhead>=to();$('roll-playhead').style.left=x+'px';
  refresh();
 };
}
