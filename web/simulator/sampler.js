// SPDX-License-Identifier: GPL-3.0-or-later
const $=id=>document.getElementById(id);
const noteName=n=>['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'][n%12]+(Math.floor(n/12)-1);
export function initSampler({send,run,action,getState,enableSound,showError,liveMic,isBusy}) {
 let open=false,kind=0,instrument=0,slot=0,replace=false,lastInstrument=0,selectorKey='',model={},mic=false;
 let audition=false,rootDraft=null;
 $('sample-root').onchange=()=>{if($('sample-root').reportValidity())rootDraft=+$('sample-root').value;};
 const slotButtons=[];
 function sample(operation,value=+$('sample-root').value){return send('/api/sampling',{operation,slot,value});}
 function emptyAfter(after){const slots=model.sampleSlots||[];for(let n=1;n<=16;n++){const at=(after+n)%16;if(!slots[at]?.id)return at;}return after;}
 function nextRoot(){const used=new Set((model.sampleSlots||[]).filter(s=>s.id).map(s=>s.note));for(const n of [60,72,84,96,108,120,0,12,24,36,48])if(!used.has(n))return n;for(let n=0;n<128;n++)if(!used.has(n))return n;return 60;}
 function waveform(samples,live=false) {
  const canvas=$('sample-waveform'),ctx=canvas.getContext('2d'),w=canvas.width,h=canvas.height;
  ctx.fillStyle='#111910';ctx.fillRect(0,0,w,h);ctx.strokeStyle='#3b5032';ctx.lineWidth=1;
  for(let x=0;x<w;x+=w/8){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,h);ctx.stroke();}
  ctx.beginPath();ctx.moveTo(0,h/2);ctx.lineTo(w,h/2);ctx.stroke();
  if(!samples?.length)return;
  ctx.strokeStyle=live?'#efc681':'#cde29b';ctx.lineWidth=2;
  if(live){ctx.beginPath();samples.forEach((v,i)=>{const x=i/(samples.length-1)*w,y=h/2-v*(h/2-8);i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();}
  else for(let i=0;i<samples.length;i++){const x=(i+.5)/samples.length*w,y=samples[i]/32768*(h/2-10);ctx.beginPath();ctx.moveTo(x,h/2-y);ctx.lineTo(x,h/2+y);ctx.stroke();}
 }
 function detail() {
  const s=instrument===model.selected?model.sampleSlots?.[slot]:null;
  const filled=!!s?.id;
  $('sample-slot-title').textContent=`Slot ${slot+1} · ${filled?noteName(s.note):'Empty'}`;
  $('sample-slot-description').textContent=replace?'The next take replaces this slot. Its old sound stays until the new capture succeeds.':filled?`${s.seconds.toFixed(3)} seconds · hold to audition, or choose Replace this slot.`:'Ready for a test sound, microphone take or audio file.';
  $('sample-replace').disabled=!filled||liveMic();$('sample-delete').disabled=!filled||liveMic();$('sample-audition').disabled=!filled;
  $('sample-replace').textContent=replace?'Cancel replace':'Replace this slot';
  $('sample-replace').classList.toggle('armed',replace);
  const canCapture=!filled||replace;
  for(const id of ['capture-test','record-mic','sample-file']){
   if(id==='record-mic'&&liveMic())continue;
   $(id).disabled=!canCapture||liveMic()||isBusy();
  }
  $('sample-root-label').hidden=kind===0;$('sample-root').disabled=filled||liveMic();
  if(document.activeElement!==$('sample-root'))$('sample-root').value=filled?s.note:(rootDraft??nextRoot());
  if(!mic)waveform(filled?s.peaks:[]);
 }
 function drawSlots() {
  const selected=instrument===model.selected?model.sampleSlots||[]:[];
  for(let i=0;i<16;i++) {
   const s=selected[i],button=slotButtons[i];
   button.setAttribute('aria-pressed',String(i===slot));button.classList.toggle('filled',!!s?.id);button.disabled=liveMic();
   button.setAttribute('aria-label',`Slot ${i+1}, ${s?.id?noteName(s.note)+', '+s.seconds.toFixed(2)+' seconds':'empty'}`);
   button.querySelector('small').textContent=s?.id?`${noteName(s.note)} · ${s.seconds.toFixed(2)}s`:'Empty';
   const key=s?.id?`${s.id}:${s.seconds}:${s.peaks.join(',')}`:'empty';
   if(button.dataset.wave!==key){button.dataset.wave=key;const points=(s?.peaks||[]).map((v,j)=>`${j*2},${16-v/32768*14}`).join(' ');button.querySelector('polyline').setAttribute('points',points);}
  }
  detail();
 }
 for(let i=0;i<16;i++) {
  const b=document.createElement('button');b.className='sample-slot';b.dataset.slot=i;b.append(Object.assign(document.createElement('strong'),{textContent:String(i+1)}));
  const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');svg.setAttribute('viewBox','0 0 94 32');svg.setAttribute('aria-hidden','true');svg.append(document.createElementNS('http://www.w3.org/2000/svg','polyline'));
  b.append(svg,document.createElement('small'));$('sample-slots').append(b);slotButtons.push(b);
  b.onclick=()=>run(async()=>{slot=i;replace=false;rootDraft=null;if(instrument){if(getState().editor.selected!==instrument)await action(2,instrument);await sample(0);}drawSlots();});
 }
 async function chooseInstrument(id) {
  instrument=id;lastInstrument=id;slot=0;replace=false;rootDraft=null;
  if(id){await action(2,id);model=getState().editor;slot=emptyAfter(15);await sample(0);}
  else if(getState().screen===7)await action(4);
  drawSlots();
 }
 $('sampler-type').onchange=e=>run(async()=>{kind=+e.target.value;selectorKey='';const candidate=model.tracks.find(t=>t.type===kind);await chooseInstrument(candidate?.id||0);});
 $('sampler-instrument').onchange=e=>run(()=>chooseInstrument(+e.target.value));
 $('sampler-new').onclick=()=>run(()=>chooseInstrument(0));
 $('sample-replace').onclick=()=>{replace=!replace;detail();};
 $('sample-delete').onclick=()=>run(async()=>{await sample(3);replace=false;drawSlots();});
 $('sample-undo').onclick=()=>run(async()=>{await action(7);drawSlots();});
 async function auditionDown(event) {
  event.preventDefault();event.currentTarget.setPointerCapture(event.pointerId);audition=true;
  try{await enableSound();if(audition)await sample(4);}catch(error){showError(error);}
 }
 function auditionUp(){if(!audition)return;audition=false;sample(5).catch(showError);}
 $('sample-audition').onpointerdown=auditionDown;
 for(const event of ['pointerup','pointercancel','lostpointercapture','blur'])$('sample-audition').addEventListener(event,auditionUp);
 window.addEventListener('pagehide',auditionUp);window.addEventListener('blur',auditionUp);
 return {
  open(){open=true;model=getState().editor||{tracks:[]};instrument=model.selected||0;kind=model.tracks.find(t=>t.id===instrument)?.type??0;lastInstrument=instrument;slot=instrument?emptyAfter(15):0;replace=false;rootDraft=null;selectorKey='';},
  close(){open=false;auditionUp();},
  isOpen(){return open;},
  async ensure(){
   const target=slot,replaceTarget=replace;
   if(!instrument){await action(0,kind===0?0:1);instrument=getState().editor.selected;lastInstrument=instrument;}
   else if(getState().editor.selected!==instrument)await action(2,instrument);
   slot=target;replace=replaceTarget;
   await sample(replace?2:1,+$('sample-root').value);open=true;
  },
  completed(){model=getState().editor;replace=false;rootDraft=null;slot=emptyAfter(slot);mic=false;$('sample-meter').value=0;drawSlots();},
  microphone(samples){mic=true;waveform(samples,true);$('sample-meter').value=Math.max(...samples.map(Math.abs));},
  stopMicrophone(){mic=false;$('sample-meter').value=0;},
  render(s){
   model=s.editor;
   if(s.screen===7&&(!open||lastInstrument!==model.selected)) {open=true;instrument=model.selected;lastInstrument=instrument;kind=s.instrumentType;slot=Math.min(15,s.captureSlot);replace=false;selectorKey='';}
   if(open&&[1,4,5,6].includes(s.screen)){open=false;auditionUp();}
   if(!open&&s.screen!==7)return false;
   if(!model.tracks.some(t=>t.id===instrument))instrument=0;
   const key=JSON.stringify([kind,instrument,model.tracks]);
   if(selectorKey!==key){selectorKey=key;$('sampler-type').value=kind;$('sampler-instrument').replaceChildren(new Option(`New ${kind===0?'multi':'simple'} instrument`,0),...model.tracks.filter(t=>t.type===kind).map(t=>new Option(t.name,t.id)));$('sampler-instrument').value=instrument;}
   $('sampler-type').disabled=liveMic();$('sampler-instrument').disabled=liveMic();$('sampler-new').disabled=liveMic();
   drawSlots();
   $('capture-progress').textContent=`${instrument===model.selected?s.regions:0} of 16 slots filled · selected slot ${slot+1} · ${instrument?'existing instrument':'creates an instrument when you capture'}`;
   return true;
  }
 };
}
