// The file manager page served at / on the ONVIF port.
#pragma once

static const char CAM_WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SensorCam files</title>
<style>
body{font:15px/1.4 system-ui,sans-serif;margin:0 auto;max-width:860px;padding:16px;color:#1b1f24;background:#f6f7f9}
h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:0;font-family:ui-monospace,monospace}
.card{background:#fff;border:1px solid #d8dde3;border-radius:8px;padding:12px;margin:12px 0}
.head{display:flex;gap:8px;align-items:center;justify-content:space-between;flex-wrap:wrap}
.sensor{border-top:1px solid #e6e9ed;margin-top:10px;padding-top:10px}
table{border-collapse:collapse;width:100%;margin-top:6px}td{padding:3px 6px 3px 0;border-top:1px solid #f0f2f4;font-size:14px}
td:first-child{font-family:ui-monospace,monospace;word-break:break-all}.missing{color:#8a929b}
td.acts{text-align:right;white-space:nowrap}
button,.btn{font:inherit;font-size:13px;padding:3px 9px;border:1px solid #b9c0c8;border-radius:5px;background:#fff;color:inherit;cursor:pointer;text-decoration:none;display:inline-block}
button.del{color:#b3261e;border-color:#e0b4b0}input[type=text]{font:inherit;padding:4px 8px;border:1px solid #b9c0c8;border-radius:5px}
details{margin-top:8px}summary{cursor:pointer;font-size:14px}
#msg{min-height:20px;font-size:14px;margin:8px 0;white-space:pre-wrap}#msg.err{color:#b3261e}
.bar{height:8px;background:#e6e9ed;border-radius:4px;overflow:hidden;margin-top:6px}.bar div{height:100%;background:#3b6fd4}
</style></head><body>
<h1>SensorCam files</h1>
<div id="usage"></div><div class="bar"><div id="fill" style="width:0"></div></div>
<div id="msg"></div>
<div class="card head"><span><input id="mac" type="text" placeholder="AA:BB:CC:DD:EE:01" size="20"> <button id="addSlave">Add slave</button></span>
<span id="clear"></span></div>
<div id="tree"></div>
<script>
const STATES=["active","approaching","away","ltr","rtl"];
const FILES=STATES.flatMap(s=>[s+".h264",s+"-delayed.h264"]);
const MAX=8192, $=id=>document.getElementById(id);
function el(tag,props,...kids){const e=Object.assign(document.createElement(tag),props||{});e.append(...kids);return e}
function say(t,err){$("msg").textContent=t;$("msg").className=err?"err":""}
async function call(method,url,body){
  const r=await fetch(url,{method,body});
  let j={};try{j=await r.json()}catch(e){}
  if(!r.ok)throw new Error(j.error||("HTTP "+r.status));
  return j}
async function act(fn,done){try{await fn();if(done)say(done);await load()}catch(e){say(e.message,true)}}
function pick(path){
  const i=el("input",{type:"file",accept:".h264"});
  i.onchange=()=>{const f=i.files[0];if(!f)return;
    if(f.size>MAX){say(f.name+" is larger than 8 KB",true);return}
    act(()=>call("PUT","/api/file?path="+path,f),"Saved "+path)};
  i.click()}
function del(path,what){if(confirm("Delete "+what+"?"))act(()=>call("DELETE","/api/file?path="+path),"Deleted "+what)}
function fileRow(path,name,size){
  const acts=el("td",{className:"acts"});
  acts.append(el("button",{textContent:size==null?"Upload":"Replace",onclick:()=>pick(path)}));
  if(size!=null){acts.append(" ",el("a",{className:"btn",href:"/api/file?path="+path,download:name,textContent:"Download"}),
    " ",el("button",{className:"del",textContent:"Delete",onclick:()=>del(path,path)}))}
  return el("tr",null,el("td",{textContent:name}),
    el("td",{className:size==null?"missing":"",textContent:size==null?"uses a fallback":(size/1024).toFixed(1)+" KB"}),acts)}
// ---- making pictures from typed text ----
// The hub has no encoder, so this browser makes each picture with ffmpeg.wasm and uploads the result.
// These settings must stay in step with tools/make_frames.py. The hub refuses anything else.
const CDN="https://cdn.jsdelivr.net/npm/@ffmpeg/core@0.12.6/dist/umd/";
const WORKER=`let core;
self.onmessage=async e=>{const m=e.data;try{
if(m.load){importScripts(m.load+"ffmpeg-core.js");
core=await self.createFFmpegCore({mainScriptUrlOrBlob:m.load+"ffmpeg-core.js#"+btoa(JSON.stringify({wasmURL:m.load+"ffmpeg-core.wasm"}))});
postMessage({id:m.id});return}
core.FS.writeFile("in.png",m.png);core.setTimeout(-1);
core.exec("-framerate","1","-i","in.png","-frames:v","1","-c:v","libx264","-profile:v","baseline","-level","3.0","-crf","38",
"-pix_fmt","yuv420p","-x264-params","scenecut=0:aud=0","-f","h264","out.h264");
const ret=core.ret;core.reset();
const data=core.FS.readFile("out.h264");core.FS.unlink("in.png");core.FS.unlink("out.h264");
postMessage({id:m.id,data,ret})
}catch(err){postMessage({id:m.id,error:String(err)})}};`;
const RED="#B00000",AMBER="#B06000",GREEN="#004000";
const WORDS={active:"ACTIVE",approaching:"APPROACHING",away:"MOVING AWAY",ltr:"LEFT TO RIGHT",rtl:"RIGHT TO LEFT",delayed:"DELAYED"};
const NAMES=[["label","Name (first line)"],["active","Active"],["approaching","Approaching"],["away","Moving away"],["ltr","Left to right"],["rtl","Right to left"],["delayed","Delayed (last line)"]];
let encoder=null,jobs=new Map(),nextJob=1;
function talk(w,msg,transfer){return new Promise((ok,bad)=>{const id=nextJob++;jobs.set(id,{ok,bad});w.postMessage({...msg,id},transfer||[])})}
function getEncoder(){
  if(!encoder)encoder=(async()=>{
    const w=new Worker(URL.createObjectURL(new Blob([WORKER],{type:"text/javascript"})));
    w.onmessage=e=>{const j=jobs.get(e.data.id);jobs.delete(e.data.id);if(e.data.error)j.bad(new Error(e.data.error));else j.ok(e.data)};
    await talk(w,{load:CDN});return w})().catch(e=>{encoder=null;throw new Error("Could not load the encoder from the internet: "+e.message)});
  return encoder}
function paint(c,colour,lines){
  const g=c.getContext("2d");g.fillStyle=colour;g.fillRect(0,0,640,360);g.fillStyle="#fff";g.textAlign="center";g.textBaseline="top";
  const used=lines.filter(l=>l),top=Math.floor((360-70*used.length)/2)+10;
  used.forEach((l,i)=>{let size=48;g.font=size+"px Arial,sans-serif";const w=g.measureText(l).width;
    if(w>600){size=Math.floor(size*600/w);g.font=size+"px Arial,sans-serif"}
    g.fillText(l,320,top+70*i)})}
// Keeps only SPS, PPS and IDR, in that order, as tools/make_frames.py does.
function keepPicture(d){
  const units={};let starts=[];
  for(let i=0;i+3<=d.length;i++)if(d[i]==0&&d[i+1]==0&&d[i+2]==1){starts.push(i);i+=2}
  starts.forEach((s,k)=>{let end=k+1<starts.length?starts[k+1]:d.length;
    if(k+1<starts.length)while(end>s+3&&d[end-1]==0)end--;
    units[d[s+3]&31]=d.subarray(s+3,end)});
  if(!units[7]||!units[8]||!units[5])throw new Error("The encoder did not produce a complete picture");
  const parts=[units[7],units[8],units[5]],out=new Uint8Array(parts.reduce((n,p)=>n+p.length+4,0));
  let at=0;for(const p of parts){out[at+3]=1;out.set(p,at+4);at+=p.length+4}
  return out}
async function encode(colour,lines){
  const w=await getEncoder(),c=el("canvas",{width:640,height:360});paint(c,colour,lines);
  const blob=await new Promise(r=>c.toBlob(r,"image/png")),png=new Uint8Array(await blob.arrayBuffer());
  const r=await talk(w,{png},[png.buffer]);
  if(r.ret!==0)throw new Error("The encoder failed");
  const pic=keepPicture(r.data);
  if(pic.length>MAX)throw new Error("That text makes a picture over 8 KB. Try fewer words.");
  return pic}
function header(p){let h="",n=4;while(n<p.length&&!(p[n]==0&&p[n+1]==0&&p[n+2]==0))h+=p[n++].toString(16).padStart(2,"0");return h}
async function save(path,pic){
  for(let tries=0;;tries++){
    try{return await call("PUT","/api/file?path="+path,pic)}
    catch(e){
      if(e.message.includes("short of memory")&&tries<4){await new Promise(r=>setTimeout(r,1500));continue}
      if(e.message.includes("wrong video format"))throw new Error("The hub refused this browser's video format. Please report this header: "+header(pic));
      throw e}}}
async function readText(p){try{const r=await fetch("/api/file?path="+p);if(r.ok)return await r.json()}catch(e){}return {}}
let busy=false;
async function makeAll(jobsList,textPath,text){
  if(busy)return;busy=true;
  try{
    say("Getting the encoder ready. The first time, this downloads about 30 MB.");await getEncoder();
    for(let i=0;i<jobsList.length;i++){const[path,colour,lines]=jobsList[i];
      say("Making picture "+(i+1)+" of "+jobsList.length);await save(path,await encode(colour,lines))}
    await call("PUT","/api/file?path="+textPath,JSON.stringify(text));
    say("Saved "+jobsList.length+" picture"+(jobsList.length>1?"s":""));await load()
  }catch(e){say(e.message,true)}finally{busy=false}}
function field(v){return el("input",{type:"text",value:v,maxLength:24,size:24})}
function textPanel(base,number){
  const d=el("details",null,el("summary",{textContent:"Change the text"}));
  let built=false;
  d.ontoggle=async()=>{if(!d.open||built)return;built=true;
    const t=Object.assign({},WORDS,{label:"SENSOR "+number},await readText(base+"/text.json")),f={},body=el("tbody");
    for(const[k,n]of NAMES){f[k]=field(t[k]);body.append(el("tr",null,el("td",{textContent:n}),el("td",null,f[k])))}
    const c=el("canvas",{width:640,height:360,style:"width:240px;display:block;margin:8px 0;border-radius:4px"});
    const show=()=>paint(c,RED,[f.label.value,f.active.value]);
    for(const k in f)f[k].oninput=show;show();
    d.append(el("table",null,body),c,el("button",{textContent:"Make and save 10 pictures",onclick:()=>{
      const v={};for(const k in f)v[k]=f[k].value.trim();
      if(!v.label){say("Enter a name for the first line",true);return}
      const list=[];for(const s of STATES){
        list.push([base+"/"+s+".h264",RED,[v.label,v[s]]]);
        list.push([base+"/"+s+"-delayed.h264",AMBER,[v.label,v[s],v.delayed]])}
      makeAll(list,base+"/text.json",v)}}))};
  return d}
function clearPanel(){
  const d=el("details",null,el("summary",{textContent:"Change the text"}));let built=false;
  d.ontoggle=async()=>{if(!d.open||built)return;built=true;
    const t=Object.assign({clear:"ALL CLEAR"},await readText("text.json")),f=field(t.clear);
    d.append(" ",f," ",el("button",{textContent:"Make and save",onclick:()=>{
      const v=f.value.trim();if(!v){say("Enter the text to show",true);return}
      makeAll([["clear.h264",GREEN,[v]]],"text.json",{clear:v})}}))};
  return d}
function render(t){
  $("usage").textContent=t.total?((t.used/1024).toFixed(0)+" KB of "+(t.total/1024).toFixed(0)+" KB used"):"";
  $("fill").style.width=t.total?(100*t.used/t.total)+"%":"0";
  const c=$("clear");c.textContent="";
  c.append("clear.h264 "+(t.clear==null?"(built-in) ":((t.clear/1024).toFixed(1)+" KB ")),
    el("button",{textContent:t.clear==null?"Upload":"Replace",onclick:()=>pick("clear.h264")}));
  if(t.clear!=null)c.append(" ",el("button",{className:"del",textContent:"Delete",onclick:()=>del("clear.h264","clear.h264")}));
  c.append(clearPanel());
  const tree=$("tree");tree.textContent="";
  if(!t.slaves.length)tree.append(el("p",{className:"missing",textContent:"No slaves yet. Add one by its MAC address."}));
  for(const s of t.slaves){
    const num=el("input",{type:"text",placeholder:"sensor no.",size:9});
    const card=el("div",{className:"card"},el("div",{className:"head"},el("h2",{textContent:s.mac}),
      el("span",null,num," ",el("button",{textContent:"Add sensor",onclick:()=>{
        if(!/^[1-9][0-9]{0,2}$/.test(num.value)||+num.value>255){say("Sensor number must be 1 to 255",true);return}
        act(()=>call("POST","/api/mkdir?path="+s.mac+"/"+num.value),"Added sensor "+num.value)}}),
      " ",el("button",{className:"del",textContent:"Delete slave",onclick:()=>del(s.mac,"slave "+s.mac+" and all its pictures")}))));
    for(const n of s.sensors){
      const sizes=Object.fromEntries(n.files.map(f=>[f.name,f.size]));
      const base=s.mac+"/"+n.input;
      card.append(el("div",{className:"sensor"},el("div",{className:"head"},el("strong",{textContent:"Sensor "+n.input}),
        el("button",{className:"del",textContent:"Delete sensor",onclick:()=>del(base,"sensor "+base+" and its pictures")})),
        el("table",null,el("tbody",null,...FILES.map(f=>fileRow(base+"/"+f,f,sizes[f])))),textPanel(base,n.input)))}
    tree.append(card)}}
async function load(){try{render(await call("GET","/api/tree"))}catch(e){say(e.message,true)}}
$("addSlave").onclick=()=>{const m=$("mac").value.trim().toUpperCase().replace(/:/g,"-");
  if(!/^([0-9A-F]{2}-){5}[0-9A-F]{2}$/.test(m)){say("Enter a MAC address like AA:BB:CC:DD:EE:01",true);return}
  act(()=>call("POST","/api/mkdir?path="+m),"Added slave "+m).then(()=>$("mac").value="")};
load();
</script></body></html>
)HTML";
