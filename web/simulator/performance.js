// SPDX-License-Identifier: GPL-3.0-or-later
// Direct controls over C-owned mixer/arp settings; the audio scheduler stays in C.
const $=id=>document.getElementById(id);
export function initPerformance({send,run,action,getState,enableSound,showError,openCreation,closeSampler}) {
 let trackKey='',strips=new Map(),chord=false;
 const modes=['Off','Up','Down','Up–down','Random','Played order','Repeat last','Down–up','Up + octave','Converge','Diverge','Pinky','Thumb','Walk','Chord'];
 $('arp-mode').replaceChildren(...modes.map((n,i)=>new Option(n,i)));
 $('arp-rate').replaceChildren(...['1/4','1/8','1/8 triplet','1/16','1/16 triplet','1/32','1/32 triplet','1/64'].map((n,i)=>new Option(n,i)));
 const edit=(operation,track,field,value)=>send('/api/performance',{operation,track,field,value});
 const sync=(element,value)=>{if(document.activeElement!==element)element.value=value;};
 for(const [id,number] of [['open-mixer',10],['open-arp',11]])$(id).onclick=()=>run(async()=>{
  if(!getState().tracks){openCreation();return;}
  closeSampler();await action(number);$(number===10?'mixer-editor':'arp-editor').scrollIntoView({behavior:'smooth',block:'start'});
 });
 for(const [id,field,multiplier] of [['arp-mode',0,1],['arp-rate',1,1],['arp-octaves',2,1],['arp-gate',3,.01],['arp-swing',4,.01],['arp-probability',5,.01],['arp-latch',6,1],['arp-order',7,1]])$(id).onchange=()=>run(async()=>{
  if(!$(id).reportValidity())return;await edit(0,getState().editor.selected,field,+$(id).value*multiplier);
 });
 for(const id of ['mixer-close','arp-close'])$(id).onclick=()=>run(async()=>{await keyUp();await send('/api/command',{type:0,index:8,value:1});await send('/api/command',{type:0,index:8,value:0});});
 for(const id of ['mixer-undo','arp-undo'])$(id).onclick=()=>run(()=>action(7));
 $('arp-stop').onclick=()=>run(()=>send('/api/sequence',{operation:15,index:0,a:0,b:0,note:60,velocity:100,revision:0,pattern:0}));
 async function key(operation,note){const p=getState().editor.pattern;return send('/api/sequence',{operation,index:0,a:0,b:0,note,velocity:100,revision:p?.revision||0,pattern:p?.id||0});}
 async function keyUp(){if(!chord)return;chord=false;for(const note of [60,64,67])await key(14,note);}
 $('arp-audition').onpointerdown=async e=>{e.preventDefault();e.currentTarget.setPointerCapture(e.pointerId);chord=true;try{await enableSound();for(const note of [60,64,67])if(chord)await key(13,note);}catch(error){showError(error);}};
 for(const event of ['pointerup','pointercancel','lostpointercapture'])$('arp-audition').addEventListener(event,()=>keyUp().catch(showError));
 window.addEventListener('blur',()=>keyUp().catch(showError));window.addEventListener('pagehide',()=>keyUp().catch(showError));
 function build(channels){
  strips.clear();$('mixer-strips').replaceChildren();
  for(const channel of channels){
   const strip=document.createElement('article');strip.className='mixer-strip';strip.dataset.track=channel.id;
   const heading=document.createElement('h3'),title=document.createElement('button');title.textContent=channel.name;title.className='mixer-track-select';title.setAttribute('aria-label','Play '+channel.name);title.onclick=()=>run(async()=>{await action(2,channel.id);await action(10);});heading.append(title);strip.append(heading);
   const meter=document.createElement('meter');meter.min=0;meter.max=1;meter.setAttribute('aria-label',channel.name+' voice peak');strip.append(meter);
   const controls=[];
   for(const [name,field,min,max,step] of [['Level',0,0,2,.01],['Pan',1,-1,1,.01],['Send 1',2,0,1,.01],['Send 2',3,0,1,.01],['Send 3',4,0,1,.01],['Send 4',5,0,1,.01]]){
    const label=document.createElement('label');label.textContent=name;const value=document.createElement('output'),input=document.createElement('input');
    input.type='range';input.min=min;input.max=max;input.step=step;input.dataset.field=field;input.setAttribute('aria-label',channel.name+' '+name);
    if(!field)label.className='level-fader';
    let pending=null,sending=false,last=null;
    async function flush(){if(sending)return;sending=true;try{while(pending!==null){const next=pending;pending=null;if(next!==last){await edit(2,channel.id,field,next);last=next;}}}catch(error){showError(error);}finally{sending=false;}}
    input.oninput=input.onchange=()=>{value.value=(+input.value).toFixed(2);pending=+input.value;flush();};
    label.append(value,input);strip.append(label);controls.push([input,value]);
   }
   const buttons=[];const row=document.createElement('div');row.className='mixer-switches';
   for(const [name,field] of [['Mute',6],['Solo',7]]){const button=document.createElement('button');button.textContent=name;button.setAttribute('aria-label',channel.name+' '+name);button.dataset.field=field;button.onclick=()=>run(()=>edit(1,channel.id,field,button.getAttribute('aria-pressed')==='true'?0:1));buttons.push(button);row.append(button);}
   strip.append(row);$('mixer-strips').append(strip);strips.set(channel.id,{strip,meter,controls,buttons});
  }
 }
 return function render(s){
  $('mixer-editor').hidden=s.screen!==2;$('arp-editor').hidden=s.screen!==15;
  if(s.screen!==15&&chord)keyUp().catch(showError);
  if(s.screen===2){const channels=s.editor.mixer||[],next=JSON.stringify(channels.map(c=>[c.id,c.name]));if(next!==trackKey){trackKey=next;build(channels);}
   for(const c of channels){const ui=strips.get(c.id);ui.strip.classList.toggle('selected',c.id===s.editor.selected);ui.meter.value=c.peak;
    const values=[c.level,c.pan,...c.sends];ui.controls.forEach(([input,output],i)=>{sync(input,values[i]);output.value=(+input.value).toFixed(2);});ui.buttons[0].setAttribute('aria-pressed',String(c.mute));ui.buttons[1].setAttribute('aria-pressed',String(c.solo));}
  }
  const a=s.editor.arp;if(s.screen===15&&a){for(const [id,value] of [['arp-mode',a.mode],['arp-rate',a.division],['arp-octaves',a.octaves],['arp-gate',Math.round(a.gate*100)],['arp-swing',Math.round(a.swing*100)],['arp-probability',Math.round(a.probability*100)],['arp-latch',+a.latch],['arp-order',+a.order]])sync($(id),value);}
 };
}
