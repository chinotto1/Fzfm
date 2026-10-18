// SPDX-License-Identifier: GPL-3.0-or-later
// Transport only: every instrument state transition and DSP operation stays in C.
export const isBrowser = document.documentElement.dataset.runtime === 'wasm';
let worker, serial=0;
const pending = new Map();
function rpc(path,body) {
  if(!worker) {
    worker=new Worker(new URL('./emulator-worker.js',import.meta.url),{type:'module'});
    worker.onmessage=({data:{id,result,error}})=>{
      const request=pending.get(id);if(!request)return;pending.delete(id);
      if(error)request.reject(new Error(error));else request.resolve(result);
    };
    worker.onerror=()=>{
      const error=new Error('The audio engine could not load. Reload the page with an internet connection.');
      for(const request of pending.values())request.reject(error);
      pending.clear();
    };
  }
  return new Promise((resolve,reject)=>{
    const id=++serial;pending.set(id,{resolve,reject});
    worker.postMessage({id,path,body},body instanceof ArrayBuffer?[body]:[]);
  });
}
export async function request(path,options={}) {
  if(!isBrowser) {
    const response=await fetch(path,options);
    if(!response.ok)throw new Error(`Request failed (${response.status})`);
    return response;
  }
  const result=await rpc(path,options.body);
  if(path==='/api/screen.png') {
    const canvas=document.createElement('canvas');canvas.width=canvas.height=240;
    const ctx=canvas.getContext('2d'),image=ctx.createImageData(240,240),pixels=new Uint8Array(result);
    for(let i=0;i<240*240;i++) {
      image.data[4*i]=pixels[3*i];image.data[4*i+1]=pixels[3*i+1];image.data[4*i+2]=pixels[3*i+2];image.data[4*i+3]=255;
    }
    ctx.putImageData(image,0,0);
    const blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/png'));
    return new Response(blob,{headers:{'Content-Type':'image/png'}});
  }
  if(result instanceof ArrayBuffer)return new Response(result);
  return new Response(JSON.stringify(result),{headers:{'Content-Type':'application/json'}});
}
