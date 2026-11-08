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
function render(t){
  $("usage").textContent=t.total?((t.used/1024).toFixed(0)+" KB of "+(t.total/1024).toFixed(0)+" KB used"):"";
  $("fill").style.width=t.total?(100*t.used/t.total)+"%":"0";
  const c=$("clear");c.textContent="";
  c.append("clear.h264 "+(t.clear==null?"(built-in) ":((t.clear/1024).toFixed(1)+" KB ")),
    el("button",{textContent:t.clear==null?"Upload":"Replace",onclick:()=>pick("clear.h264")}));
  if(t.clear!=null)c.append(" ",el("button",{className:"del",textContent:"Delete",onclick:()=>del("clear.h264","clear.h264")}));
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
        el("table",null,el("tbody",null,...FILES.map(f=>fileRow(base+"/"+f,f,sizes[f]))))))}
    tree.append(card)}}
async function load(){try{render(await call("GET","/api/tree"))}catch(e){say(e.message,true)}}
$("addSlave").onclick=()=>{const m=$("mac").value.trim().toUpperCase().replace(/:/g,"-");
  if(!/^([0-9A-F]{2}-){5}[0-9A-F]{2}$/.test(m)){say("Enter a MAC address like AA:BB:CC:DD:EE:01",true);return}
  act(()=>call("POST","/api/mkdir?path="+m),"Added slave "+m).then(()=>$("mac").value="")};
load();
</script></body></html>
)HTML";
