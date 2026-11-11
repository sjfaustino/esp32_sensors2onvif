// The pages served on the web port: Wi-Fi setup at /setup and the file manager at /.
#pragma once

static const char CAM_SETUP_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Hub Wi-Fi setup</title>
<style>
body{font:16px/1.4 system-ui,sans-serif;margin:0 auto;max-width:26rem;padding:16px;color:#1b1f24;background:#f6f7f9}
h1{font-size:22px;margin:0 0 8px}label{display:block;margin-top:12px;font-size:14px}
input,select,button{font:inherit;width:100%;padding:10px;margin-top:4px;box-sizing:border-box;border:1px solid #b9c0c8;border-radius:6px;background:#fff;color:inherit}
button{margin-top:16px;cursor:pointer}#msg{min-height:24px;margin-top:12px;white-space:pre-wrap}#msg.err{color:#b3261e}
</style></head><body>
<h1>Hub Wi-Fi setup</h1>
<div id="state">Looking for networks...</div>
<label>Network<select id="list"></select></label>
<label>Or type its name<input id="name" maxlength="32" autocomplete="off" autocapitalize="none"></label>
<label>Password<input id="pass" type="password" maxlength="63" autocomplete="off" placeholder="Empty for an open or already saved network"></label>
<label>Use as<select id="role"><option value="main">Main network</option><option value="backup">Backup network</option></select></label>
<button id="save">Save</button>
<div id="msg"></div>
<script>
const $=i=>document.getElementById(i);let hub="";
async function scan(){try{const r=await fetch("/api/wifi/scan");const j=await r.json();hub=j.name||"";
if(j.scanning){setTimeout(scan,1500);return}
const seen=new Set();$("list").replaceChildren(...j.networks.filter(n=>n.ssid&&!seen.has(n.ssid)&&seen.add(n.ssid)).map(n=>{const o=document.createElement("option");o.value=n.ssid;o.textContent=n.ssid+(n.open?" (open)":"")+"  "+n.rssi+" dBm";return o}));
$("state").textContent=j.current?"Main network: "+j.current+". Backup: "+(j.backup||"none")+".":"No network saved yet.";
if(j.current)$("list").value=j.current}catch(e){setTimeout(scan,3000)}}
$("save").onclick=async()=>{const ssid=$("name").value||$("list").value;const m=$("msg");m.className="";
if(!ssid){m.className="err";m.textContent="Choose or type a network.";return}
try{const r=await fetch("/api/wifi",{method:"POST",body:"ssid="+encodeURIComponent(ssid)+"&password="+encodeURIComponent($("pass").value)+"&role="+$("role").value});const j=await r.json();
if(!r.ok){m.className="err";m.textContent=j.error;return}
m.textContent=$("role").value=="backup"?"Saved as the backup network.":"Saved. The hub is restarting to join "+ssid+". Reconnect this device to that network"+(hub?", then open http://"+hub:"")+". If neither saved network works, the setup network comes back in about a minute."}
catch(e){m.className="err";m.textContent="The hub did not answer. Try again."}};
scan();
</script></body></html>)HTML";

static const char CAM_WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SensorCam files</title>
<style>
body{font:15px/1.4 system-ui,sans-serif;color:#1b1f24;display:flex;min-height:100vh;margin:0;background:#fff}
h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:0;font-family:ui-monospace,monospace}
.card{background:#fff;border:1px solid #d8dde3;border-radius:8px;padding:12px;margin:12px 0}
.head{display:flex;gap:8px;align-items:center;justify-content:space-between;flex-wrap:wrap}
.sensor{border-top:1px solid #e6e9ed;margin-top:10px;padding-top:10px}
table{border-collapse:collapse;width:100%;margin-top:6px}td{padding:3px 6px 3px 0;border-top:1px solid #f0f2f4;font-size:14px}
td:first-child{font-family:ui-monospace,monospace;word-break:break-all}.missing{color:#8a929b}
td.acts{text-align:right}
dialog{border:1px solid #b9c0c8;border-radius:8px;padding:16px;max-width:min(92vw,560px)}textarea{font:inherit;padding:4px 8px;max-width:100%}
button,.btn{font:inherit;font-size:13px;padding:3px 9px;border:1px solid #b9c0c8;border-radius:5px;background:#fff;color:inherit;cursor:pointer;text-decoration:none;display:inline-block}
button.del{color:#b3261e;border-color:#e0b4b0}input[type=text]{font:inherit;padding:4px 8px;border:1px solid #b9c0c8;border-radius:5px}
details{margin-top:8px}summary{cursor:pointer;font-size:14px}
#msg{min-height:20px;font-size:14px;margin:8px 0;white-space:pre-wrap}#msg.err{color:#b3261e}
.bar{height:8px;background:#e6e9ed;border-radius:4px;overflow:hidden;margin-top:6px}.bar div{height:100%;background:#3b6fd4}
#menu{width:240px;min-width:240px;background:#f5f5f5;padding:15px 0}
.first{padding:10px 20px;cursor:pointer;display:flex;justify-content:space-between;align-items:center;font-size:16px}
.first::after{content:"\25BE";color:#666}.first.open::after{content:"\25B4"}
.subs{max-height:0;overflow:hidden;transition:max-height .3s}.subs.show{max-height:300px}
.item{position:relative;padding:5px 5px 5px 36px;font-size:14px;cursor:pointer}
.item::before{content:"\2022";position:absolute;left:24px;top:50%;transform:translateY(-50%);color:#666;font-size:10px}
.first:hover,.item:hover{background:#ddd}.item.active{background:#fff;color:blue}
main{flex:1;min-width:0;padding:16px}.view{max-width:860px}.view>h1{margin-bottom:12px}
.info td:first-child{font-family:inherit;color:#5a636d;width:40%}.info td{word-break:break-all}
#netFrame{width:100%;height:620px;border:0}
input[type=number]{font:inherit;padding:4px 8px;border:1px solid #b9c0c8;border-radius:5px;width:8em}
.netRow{display:flex;gap:8px;align-items:center;margin:6px 0}.netRow span{width:9em;color:#5a636d}.netRow input,.netRow select{font:inherit;padding:4px 8px;border:1px solid #b9c0c8;border-radius:5px;width:14em;box-sizing:border-box;background:#fff;color:inherit}.netRow input:disabled{background:#f0f2f4;color:#5a636d}
.portForm{margin-bottom:6px}.portMsg{min-height:20px;font-size:14px;margin-bottom:8px}.portMsg.err{color:#b3261e}
@media(max-width:640px){body{display:block}#menu{width:auto}}
</style></head><body>
<nav id="menu">
<div class="first">Network Manager</div>
<div class="subs"><div class="item" data-view="network">Network</div><div class="item" data-view="wifi">WiFi</div><div class="item" data-view="espnow">ESP-NOW</div><div class="item" data-view="onvif">ONVIF</div><div class="item" data-view="http">HTTP</div><div class="item" data-view="rtsp">RTSP</div></div>
<div class="first">Sensors Manager</div>
<div class="subs"><div class="item" data-view="sensors">Sensors</div><div class="item" data-view="events">Events</div><div class="item" data-view="notify">Notifications</div></div>
<div class="first">NVR Manager</div>
<div class="subs"><div class="item" data-view="nvr">NVR</div><div class="item" data-view="live">Live Video</div></div>
<div class="first">System Manager</div>
<div class="subs"><div class="item" data-view="maintenance">Maintenance</div><div class="item" data-view="time">Time</div><div class="item" data-view="device">Device Info</div></div>
</nav>
<main>
<p id="openWarn" hidden style="margin:0 0 12px;padding:8px 10px;border:1px solid #e0b4b0;border-radius:6px;color:#b3261e;font-size:14px">No hub password is set, so anyone on this network can change the hub and watch its video. Set one under Network Manager &gt; HTTP.</p>
<div class="view" id="view-network" hidden><h1>Network</h1>
<label class="netRow"><span>Connection</span><input id="netType" disabled></label>
<label class="netRow"><span>IP mode</span><select id="netMode"><option value="dhcp">DHCP</option><option value="static">Static</option></select></label>
<label class="netRow"><span>IP address</span><input id="netIp" maxlength="15" inputmode="decimal"></label>
<label class="netRow"><span>Subnet mask</span><input id="netMask" maxlength="15" inputmode="decimal"></label>
<label class="netRow"><span>Gateway</span><input id="netGw" maxlength="15" inputmode="decimal"></label>
<label class="netRow"><span>DNS server</span><input id="netDns" maxlength="15" inputmode="decimal"></label>
<label class="netRow"><span>MAC address</span><input id="netMac" disabled></label>
<div class="portForm"><button id="saveNet">Save</button></div>
<div class="portMsg" id="netMsg"></div></div>
<div class="view" id="view-wifi" hidden><h1>WiFi</h1><iframe id="netFrame" title="Wi-Fi settings"></iframe></div>
<div class="view" id="view-onvif" hidden><h1>ONVIF</h1><div class="portForm"><label>Port <input id="onvifPort" type="number" min="1" max="65535"></label> <button id="saveOnvif">Save</button></div><div class="portMsg" id="onvifMsg"></div><table class="info" id="onvifInfo"></table></div>
<div class="view" id="view-http" hidden><h1>HTTP</h1>
<div class="portForm"><label>Port <input id="webPort" type="number" min="1" max="65535"></label></div>
<div class="portForm"><label>Name <input id="hubName" type="text" maxlength="63" autocapitalize="none"></label> .local</div>
<div class="portForm"><label><input id="protect" type="checkbox"> Password protection (user admin)</label></div>
<div class="portForm" id="pwRow" hidden><label>Password <input id="webPw" type="password" maxlength="63" autocomplete="new-password" style="font:inherit;padding:4px 8px;border:1px solid #b9c0c8;border-radius:5px"></label></div>
<div class="portForm"><button id="saveWeb">Save</button></div>
<div class="portMsg" id="webMsg"></div><table class="info" id="httpInfo"></table></div>
<div class="view" id="view-rtsp" hidden><h1>RTSP</h1><div class="portForm"><label>Port <input id="rtspPort" type="number" min="1" max="65535"></label> <button id="saveRtsp">Save</button></div><div class="portMsg" id="rtspMsg"></div><table class="info" id="rtspInfo"></table></div>
<div class="view" id="view-espnow" hidden><h1>ESP-NOW</h1><table class="info" id="espInfo"></table>
<label class="portForm" style="display:block;margin-top:10px"><input id="espAlarm" type="checkbox"> Raise an NVR alarm when a node goes silent or its battery runs low</label>
<label class="portForm" style="display:block"><input id="espUnsigned" type="checkbox"> Also accept unsigned messages (version 1). Anyone in radio range could then send false alerts.</label>
<h2 style="font-family:inherit;margin-top:18px">Sensor nodes</h2><table id="espNodes"></table><div class="portMsg" id="espEmpty"></div>
<h2 style="font-family:inherit;margin-top:18px">Unknown senders</h2><table id="espUnknown"></table><div class="portMsg" id="espNone"></div></div>
<div class="view" id="view-nvr" hidden><h1>NVR</h1>
<label class="netRow"><span>IP address</span><input id="nvrIp" maxlength="15" inputmode="decimal"></label>
<div class="portForm"><button id="saveNvr">Save</button></div>
<div class="portMsg" id="nvrMsg"></div><table class="info" id="nvrInfo"></table></div>
<div class="view" id="view-live" hidden><h1>Live Video</h1><div class="portMsg" id="liveMsg"></div><img id="liveImg" alt="The picture the hub is sending" style="display:none;width:100%;max-width:640px;background:#000"><div class="portMsg" id="liveFile"></div></div>
<div class="view" id="view-events" hidden><h1>Events</h1><p style="margin:0 0 8px"><a class="btn" href="/api/events.csv" download="hub-events.csv">Download the full history (CSV)</a></p><table id="evTable"></table><div class="portMsg" id="evNone"></div></div>
<div class="view" id="view-notify" hidden><h1>Notifications</h1>
<p style="margin:0 0 6px;font-size:14px">The hub can send each alert to a web address on your network, such as a Home Assistant webhook, Node-RED or a self-hosted ntfy server. Use http:// and an IP address; https and names are not supported.</p>
<label class="netRow"><span>Address</span><input id="hookUrl" maxlength="200" placeholder="http://192.168.1.10:8123/api/webhook/hub"></label>
<label class="netRow"><span>Format</span><select id="hookText"><option value="0">JSON</option><option value="1">Plain text</option></select></label>
<label class="portForm" style="display:block"><input id="hookAlerts" type="checkbox"> Alerts</label>
<label class="portForm" style="display:block"><input id="hookProblems" type="checkbox"> Node problems: silent, low battery, weak signal</label>
<div class="portForm"><button id="saveHook">Save</button> <button id="testHook">Send a test</button></div>
<div class="portMsg" id="hookMsg"></div><table class="info" id="hookInfo"></table></div>
<div class="view" id="view-maintenance" hidden><h1>Maintenance</h1>
<h2 style="font-family:inherit;margin-bottom:6px">Scheduled restart</h2>
<label class="netRow"><span>Enabled</span><input id="rbOn" type="checkbox" style="width:auto"></label>
<label class="netRow"><span>Day</span><select id="rbDay"><option value="-1">Every day</option><option value="0">Sunday</option><option value="1">Monday</option><option value="2">Tuesday</option><option value="3">Wednesday</option><option value="4">Thursday</option><option value="5">Friday</option><option value="6">Saturday</option></select></label>
<label class="netRow"><span>Time</span><input id="rbTime" type="time"></label>
<div class="portForm"><button id="saveRb">Save</button></div><div class="portMsg" id="rbMsg"></div>
<h2 style="font-family:inherit;margin:18px 0 6px">Restart now</h2>
<div class="portForm"><button id="rebootNow">Restart the hub</button></div>
<h2 style="font-family:inherit;margin:18px 0 6px">Firmware update</h2>
<div class="portForm"><input id="fwFile" type="file" accept=".bin"> <button id="fwSend">Update firmware</button></div>
<div class="portMsg" id="fwMsg"></div>
<h2 style="font-family:inherit;margin:18px 0 6px">Backup and restore</h2>
<p style="margin:0 0 6px;font-size:14px">The backup file holds the WiFi and hub passwords in plain text, so keep it somewhere safe. Pictures are not included.</p>
<div class="portForm"><a class="btn" href="/api/backup" download="hub-settings.txt">Download backup</a> <input id="rsFile" type="file" accept=".txt"> <button id="rsSend">Restore</button></div>
<div class="portMsg" id="rsMsg"></div>
<h2 style="font-family:inherit;margin:18px 0 6px">Full backup</h2>
<p style="margin:0 0 6px;font-size:14px">The settings, every picture, and each sensor's name and alert settings, in one file made by this browser. Restoring adds the sensors and pictures in it, replacing any with the same name, then replaces the settings and restarts the hub.</p>
<div class="portForm"><button id="fbSave">Download full backup</button> <input id="fbFile" type="file" accept=".tar"> <button id="fbLoad">Restore full backup</button></div>
<div class="portMsg" id="fbMsg"></div>
<h2 style="font-family:inherit;margin:18px 0 6px">Reset settings</h2>
<p style="margin:0 0 6px;font-size:14px">Erases the saved WiFi networks, ports, name, web password, fixed address, time and notification settings. Pictures, sensor nodes, and the ESP-NOW key and token that sensors use, are kept.</p>
<div class="portForm"><button id="resetAll" class="del">Reset settings</button></div><div class="portMsg" id="sysMsg"></div></div>
<div class="view" id="view-time" hidden><h1>Time</h1>
<label class="netRow"><span>Current time</span><input id="tmNow" disabled></label>
<label class="netRow"><span>Timezone</span><select id="tmZone"></select></label>
<label class="netRow"><span>Daylight saving</span><select id="tmDst"><option value="0">None</option><option value="1">Europe</option><option value="2">United States</option></select></label>
<label class="netRow"><span>Set time by</span><select id="tmMode"><option value="ntp">NTP server</option><option value="manual">Manual</option></select></label>
<label class="netRow"><span>NTP server</span><input id="tmServer" maxlength="63" autocapitalize="none"></label>
<label class="netRow"><span>Date and time</span><input id="tmManual" type="datetime-local" step="1"></label>
<div class="portForm"><button id="tmFromPc">Read from this PC</button> <button id="saveTime">Save</button></div>
<div class="portMsg" id="tmMsg"></div>
<p style="margin:0;font-size:14px;color:#5a636d">The NVR always receives UTC, so these settings only change the time shown here and the scheduled restart. After a power cut, a manual time is read once from the NTP server if the hub can reach it; otherwise set it again.</p></div>
<div class="view" id="view-device" hidden><h1>Device Info</h1><table class="info" id="devInfo"></table><p id="crashActs"></p></div>
<div class="view" id="view-sensors">
<h1>Sensors</h1>
<div id="usage"></div><div class="bar"><div id="fill" style="width:0"></div></div>
<div id="msg"></div>
<div class="card head"><span><input id="mac" type="text" placeholder="AA:BB:CC:DD:EE:01" size="20"> <button id="addNode">Add node</button></span>
<span id="clear"></span></div>
<div id="tree"></div>
<p><button id="makeThumbs">Create missing thumbnails</button></p>
</div></main>
<script>
const STATES=["active","approaching","away","ltr","rtl"];
const FILES=STATES.flatMap(s=>[s+".h264",s+"-delayed.h264"]);
const MAX=32768, $=id=>document.getElementById(id);
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
    if(f.size>MAX){say(f.name+" is larger than 32 KB",true);return}
    act(async()=>{await call("PUT","/api/file?path="+path,f);await makePreview(path,new Uint8Array(await f.arrayBuffer())).catch(()=>{})},"Saved "+path)};
  i.click()}
function del(path,what){if(confirm("Delete "+what+"?"))act(()=>call("DELETE","/api/file?path="+path),"Deleted "+what)}
function previewButton(path,name){return el("button",{textContent:"\u{1F441}",title:"Preview",ariaLabel:"Preview "+name,onclick:()=>showPicture(path,name)})}
// The hub stores H.264 only, so the browser decodes the picture with the same ffmpeg.wasm that makes them.
async function showPicture(path,name){
  const img=el("img",{alt:name,style:"display:none;width:100%;max-width:640px"}),status=el("p",{textContent:"Loading the picture..."});
  const dlg=el("dialog",null,el("h2",{textContent:name}),status,img,el("p",null,el("button",{textContent:"Close",onclick:()=>dlg.close()})));
  dlg.onclose=()=>{if(img.src)URL.revokeObjectURL(img.src);dlg.remove()};
  document.body.append(dlg);dlg.showModal();
  try{const r=await fetch("/api/file?path="+path).catch(()=>{throw new Error("Could not reach the hub.")});
    if(!r.ok)throw new Error("The hub could not send this picture.");
    const pic=new Uint8Array(await r.arrayBuffer());
    status.textContent="Getting the decoder ready. The first time, this downloads about 30 MB.";
    img.src=await decoded(pic).catch(()=>{throw new Error("The decoder could not load or could not read this picture. It needs internet access the first time.")});
    img.style.display="block";status.remove()}
  catch(e){status.textContent="Could not show this picture. "+e.message}}
function fileRow(path,name,size){
  const acts=el("td",{className:"acts"});
  acts.append(el("button",{textContent:size==null?"Upload":"Replace",onclick:()=>pick(path)})," ",el("button",{textContent:"Use an image",onclick:()=>imageDialog(path)}));
  if(size!=null){acts.prepend(previewButton(path,name)," ");acts.append(" ",el("a",{className:"btn",href:"/api/file?path="+path,download:name,textContent:"Download"}),
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
if(m.decode){core.FS.writeFile("in.h264",m.decode);core.setTimeout(-1);
core.exec("-y","-i","in.h264","-frames:v","1","out.png");
const done=core.ret;core.reset();
const shown=core.FS.readFile("out.png");core.FS.unlink("in.h264");core.FS.unlink("out.png");
postMessage({id:m.id,data:shown,ret:done});return}
core.FS.writeFile("in.png",m.png);core.setTimeout(-1);
core.exec("-y","-framerate","1","-i","in.png","-frames:v","1","-c:v","libx264","-profile:v","baseline","-level","3.0","-crf",String(m.crf),
"-pix_fmt","yuv420p","-x264-params","scenecut=0:aud=0","-f","h264","out.h264");
const ret=core.ret;core.reset();
const data=core.FS.readFile("out.h264");core.FS.unlink("in.png");core.FS.unlink("out.h264");
postMessage({id:m.id,data,ret})
}catch(err){postMessage({id:m.id,error:String(err)})}};`;
const RED="#B00000",AMBER="#B06000",GREEN="#004000";
const WORDS={active:"ACTIVE",approaching:"APPROACHING",away:"MOVING AWAY",ltr:"LEFT TO RIGHT",rtl:"RIGHT TO LEFT",delayed:"PAST EVENT - CHECK LOGS"};
const NAMES=[["label","Name (first line)"],["active","Active"],["approaching","Approaching"],["away","Moving away"],["ltr","Left to right"],["rtl","Right to left"],["delayed","Past event (last line)"]];
let encoder=null,jobs=new Map(),nextJob=1;
function talk(w,msg,transfer){return new Promise((ok,bad)=>{const id=nextJob++;jobs.set(id,{ok,bad});w.postMessage({...msg,id},transfer||[])})}
function getEncoder(){
  if(!encoder)encoder=(async()=>{
    const w=new Worker(URL.createObjectURL(new Blob([WORKER],{type:"text/javascript"})));
    w.onmessage=e=>{const j=jobs.get(e.data.id);jobs.delete(e.data.id);if(e.data.error)j.bad(new Error(e.data.error));else j.ok(e.data)};
    await talk(w,{load:CDN});return w})().catch(e=>{encoder=null;throw new Error("Could not load the encoder from the internet: "+e.message)});
  return encoder}
function drawLines(g,lines,outline){
  g.fillStyle="#fff";g.textAlign="center";g.textBaseline="top";g.strokeStyle="#000";g.lineWidth=6;g.lineJoin="round";
  const used=lines.filter(l=>l),top=Math.floor((360-70*used.length)/2)+10;
  used.forEach((l,i)=>{let size=48;g.font=size+"px Arial,sans-serif";const w=g.measureText(l).width;
    if(w>600){size=Math.floor(size*600/w);g.font=size+"px Arial,sans-serif"}
    if(outline)g.strokeText(l,320,top+70*i);
    g.fillText(l,320,top+70*i)})}
function paint(c,colour,lines){const g=c.getContext("2d");g.fillStyle=colour;g.fillRect(0,0,640,360);drawLines(g,lines,false)}
// Fits the whole image into 640x360, with black bars if its shape differs.
function paintImage(c,image,lines){
  const g=c.getContext("2d");g.fillStyle="#000";g.fillRect(0,0,640,360);
  const s=Math.min(640/image.width,360/image.height),w=image.width*s,h=image.height*s;
  g.imageSmoothingQuality="high";g.drawImage(image,(640-w)/2,(360-h)/2,w,h);
  drawLines(g,lines,true)}
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
// Higher steps are smaller and softer. The first one that fits the hub's limit is used.
const QUALITY=[24,28,32,36,38,42,46,48,51];
async function encodeCanvas(c){
  const w=await getEncoder();
  const blob=await new Promise(r=>c.toBlob(r,"image/png")),png=new Uint8Array(await blob.arrayBuffer());
  for(let step=0;step<QUALITY.length;step++){
    const copy=png.slice(),r=await talk(w,{png:copy,crf:QUALITY[step]},[copy.buffer]);
    if(r.ret!==0)throw new Error("The encoder failed");
    const pic=keepPicture(r.data);
    if(pic.length<=MAX)return{pic,step}}
  throw new Error("Even at the lowest quality this picture is over 32 KB. Try a simpler image.")}
async function encode(colour,lines){const c=el("canvas",{width:640,height:360});paint(c,colour,lines);return(await encodeCanvas(c)).pic}
// Shows the picture exactly as it will be streamed, by decoding the encoded result.
async function decoded(pic){
  const w=await getEncoder(),copy=pic.slice(),r=await talk(w,{decode:copy},[copy.buffer]);
  if(r.ret!==0)throw new Error("preview failed");
  return URL.createObjectURL(new Blob([r.data],{type:"image/png"}))}
function imageDialog(path){
  const VIEW="width:100%;max-width:480px;display:block;margin:8px 0;border-radius:4px;background:#000";
  const chooser=el("input",{type:"file",accept:"image/*"});
  const words=el("textarea",{rows:3,cols:30,placeholder:"Optional text on top, up to 3 lines"});
  const source=el("canvas",{width:640,height:360,style:VIEW});
  const result=el("img",{alt:"Encoded picture",style:VIEW+";display:none"});
  const note=el("p",{textContent:"Choose an image. Any shape or size is fine."});
  const make=el("button",{textContent:"Encode and preview",disabled:true});
  const keep=el("button",{textContent:"Save to the hub",disabled:true});
  const dlg=el("dialog",null,el("strong",{textContent:path}),el("p",null,chooser),el("p",null,words),source,result,note,
    el("p",null,make," ",keep," ",el("button",{textContent:"Close",onclick:()=>dlg.close()})));
  let image=null,pic=null;
  const lines=()=>words.value.split("\n").map(l=>l.trim()).filter(l=>l).slice(0,3);
  const redraw=()=>{pic=null;keep.disabled=true;result.style.display="none";if(image)paintImage(source,image,lines())};
  words.oninput=redraw;
  chooser.onchange=async()=>{const f=chooser.files[0];if(!f)return;
    try{image=await createImageBitmap(f);make.disabled=false;note.textContent="Add text if you like, then encode.";redraw()}
    catch(e){image=null;make.disabled=true;note.textContent="This browser cannot open that file."}};
  make.onclick=async()=>{if(busy)return;busy=true;make.disabled=true;keep.disabled=true;
    try{
      note.textContent="Encoding. The first time, this downloads about 30 MB.";
      const r=await encodeCanvas(source);pic=r.pic;
      const size=(pic.length/1024).toFixed(1)+" KB";
      note.textContent=size+(r.step?". Quality was lowered "+r.step+" of "+(QUALITY.length-1)+" steps to fit 32 KB.":" at full quality.");
      try{if(result.src)URL.revokeObjectURL(result.src);result.src=await decoded(pic);result.style.display="block"}
      catch(e){note.textContent+=" The preview could not be shown, but the picture can still be saved."}
      keep.disabled=false
    }catch(e){note.textContent=e.message}finally{busy=false;make.disabled=!image}};
  keep.onclick=async()=>{if(!pic||busy)return;busy=true;keep.disabled=true;
    try{await save(path,pic);dlg.close();say("Saved "+path);busy=false;await load()}
    catch(e){note.textContent=e.message;keep.disabled=false}finally{busy=false}};
  dlg.onclose=()=>{if(result.src)URL.revokeObjectURL(result.src);dlg.remove()};
  document.body.append(dlg);dlg.showModal()}
// ONVIF clients ask for a JPEG snapshot, which the hub cannot make from H.264, so one is stored beside each picture.
async function makePreview(path,pic){
  const url=await decoded(pic),img=new Image();
  try{await new Promise((ok,bad)=>{img.onload=ok;img.onerror=bad;img.src=url});
    const c=el("canvas",{width:320,height:180});c.getContext("2d").drawImage(img,0,0,320,180);
    const jpeg=await new Promise(r=>c.toBlob(r,"image/jpeg",0.75));
    await call("PUT","/api/file?path="+path.replace(/\.h264$/,".jpg"),jpeg)}
  finally{URL.revokeObjectURL(url)}}
function header(p){let h="",n=4;while(n<p.length&&!(p[n]==0&&p[n+1]==0&&p[n+2]==0))h+=p[n++].toString(16).padStart(2,"0");return h}
async function save(path,pic){
  try{const done=await call("PUT","/api/file?path="+path,pic);await makePreview(path,pic).catch(()=>{});return done}
  catch(e){
    if(e.message.includes("wrong video format"))throw new Error("The hub refused this browser's video format. Please report this header: "+header(pic));
    throw e}}
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
const hm=m=>String(Math.floor(m/60)).padStart(2,"0")+":"+String(m%60).padStart(2,"0");
function settingsPanel(base,n){
  const hold=el("input",{type:"number",min:1,max:3600,value:n.hold||"",placeholder:"default",style:"width:6em"});
  const from=el("input",{type:"time",value:n.quiet?hm(n.quiet[0]):""}),to=el("input",{type:"time",value:n.quiet?hm(n.quiet[1]):""});
  const minutes=v=>v?Number(v.slice(0,2))*60+Number(v.slice(3,5)):"";
  return el("details",null,el("summary",{textContent:"Alert settings"+(n.hold||n.quiet?" (changed)":"")}),
    el("p",null,"An alert lasts ",hold," seconds. Empty uses the hub's default."),
    el("p",null,"Quiet hours from ",from," to ",to,". During quiet hours alerts are logged but not sent to the NVR or to Notifications. Leave both empty for none."),
    el("button",{textContent:"Save alert settings",onclick:()=>act(()=>call("POST","/api/sensor-settings?path="+base,"hold="+hold.value+"&from="+minutes(from.value)+"&to="+minutes(to.value)),"Saved the alert settings")}))}
function nodePanel(s){
  const late=el("input",{type:"number",min:1,max:86400,value:s.late||"",placeholder:"30",style:"width:7em"});
  return el("details",null,el("summary",{textContent:"Node settings"+(s.late?" (changed)":"")}),
    el("p",null,"An alert this node reports as having happened more than ",late," seconds ago is a past event: it shows the sensor's past-event picture, or the built-in \"PAST EVENT, CHECK LOGS\", and gets its own NVR record. Its true time is in the Events list. Empty uses 30 seconds. Set it to how long this node can take to deliver an alert in real time, including retries."),
    el("button",{textContent:"Save node settings",onclick:()=>act(()=>call("POST","/api/node-settings?path="+s.mac,"late="+late.value),"Saved the node settings")}))}
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
    el("button",{textContent:t.clear==null?"Upload":"Replace",onclick:()=>pick("clear.h264")})," ",el("button",{textContent:"Use an image",onclick:()=>imageDialog("clear.h264")}));
  if(t.clear!=null)c.append(" ",previewButton("clear.h264","clear.h264")," ",el("button",{className:"del",textContent:"Delete",onclick:()=>del("clear.h264","clear.h264")}));
  c.append(clearPanel());
  const tree=$("tree");tree.textContent="";
  if(!t.nodes.length)tree.append(el("p",{className:"missing",textContent:"No nodes yet. Add one by its MAC address."}));
  for(const s of t.nodes){
    const num=el("input",{type:"text",placeholder:"sensor no.",size:9});
    const card=el("div",{className:"card"},el("div",{className:"head"},el("h2",{textContent:s.mac}),
      el("span",null,num," ",el("button",{textContent:"Add sensor",onclick:()=>{
        if(!/^[1-9][0-9]{0,2}$/.test(num.value)||+num.value>255){say("Sensor number must be 1 to 255",true);return}
        act(()=>call("POST","/api/mkdir?path="+s.mac+"/"+num.value),"Added sensor "+num.value)}}),
      " ",el("button",{className:"del",textContent:"Delete node",onclick:()=>del(s.mac,"node "+s.mac+" and all its pictures")}))),
      nodePanel(s));
    for(const n of s.sensors){
      const sizes=Object.fromEntries(n.files.map(f=>[f.name,f.size]));
      const base=s.mac+"/"+n.input;
      const nameField=el("input",{type:"text",value:n.name||"",maxLength:24,size:18,placeholder:"Name"});
      card.append(el("div",{className:"sensor"},el("div",{className:"head"},el("span",null,el("strong",{textContent:"Sensor "+n.input})," ",nameField," ",el("button",{textContent:"Save name",onclick:()=>act(()=>call("PUT","/api/file?path="+base+"/name.txt",nameField.value),"Saved the name")})),
        el("button",{className:"del",textContent:"Delete sensor",onclick:()=>del(base,"sensor "+base+" and its pictures")})),
        el("table",null,el("tbody",null,...FILES.map(f=>fileRow(base+"/"+f,f,sizes[f])))),textPanel(base,n.input),settingsPanel(base,n)))}
    tree.append(card)}}
async function load(){try{render(await call("GET","/api/tree"))}catch(e){say(e.message,true)}}
$("makeThumbs").onclick=()=>act(async()=>{const t=await call("GET","/api/tree"),list=[];
  if(t.clear!=null&&!t.clearPreview)list.push("clear.h264");
  for(const s of t.nodes)for(const n of s.sensors)for(const f of n.files)if(!f.preview)list.push(s.mac+"/"+n.input+"/"+f.name);
  for(let i=0;i<list.length;i++){say("Making thumbnail "+(i+1)+" of "+list.length+". The first time, this downloads about 30 MB.");
    const r=await fetch("/api/file?path="+list[i]);if(!r.ok)throw new Error("The hub could not send "+list[i]);
    await makePreview(list[i],new Uint8Array(await r.arrayBuffer()))}},"Thumbnails are up to date");
$("addNode").onclick=()=>{const m=$("mac").value.trim().toUpperCase().replace(/:/g,"-");
  if(!/^([0-9A-F]{2}-){5}[0-9A-F]{2}$/.test(m)){say("Enter a MAC address like AA:BB:CC:DD:EE:01",true);return}
  act(()=>call("POST","/api/mkdir?path="+m),"Added node "+m).then(()=>$("mac").value="")};
load();
</script>
<script>
(()=>{
const firsts=[...document.querySelectorAll(".first")],items=[...document.querySelectorAll(".item")];
function fill(id,rows){const t=$(id);t.replaceChildren(...rows.map(([k,v])=>{const r=t.insertRow(),a=r.insertCell(),b=r.insertCell();a.textContent=k;b.textContent=v;return r}))}
// Sends a change to the hub and reports the outcome in m. True when the hub accepted it.
async function send(m,url,body,done,method,failed){m.className="portMsg";
try{const r=await fetch(url,{method:method||"POST",body});const j=await r.json();if(!r.ok){m.className="portMsg err";m.textContent=j.error;return false}
if(done)m.textContent=done;return true}catch(e){m.className="portMsg err";m.textContent=failed||"The hub did not answer. Try again.";return false}}
const headRow=names=>el("tr",null,...names.map(h=>el("th",{textContent:h,style:"text-align:left;font-size:13px;padding-right:6px"})));
async function info(view){try{const r=await fetch("/api/info");const i=await r.json();
const at=i.webPort==80?"":":"+i.webPort;
$("onvifPort").value=i.onvifPort;$("webPort").value=i.webPort;
$("rtspPort").value=i.rtspPort;
if(view=="rtsp")fill("rtspInfo",[["Video address","rtsp://"+i.ip+":"+i.rtspPort+"/stream"],["Video streams open",i.streams]]);
$("hubName").value=i.name.replace(/\.local$/,"");$("protect").checked=i.password;
$("pwRow").hidden=!i.password;$("webPw").placeholder=i.password?"Leave empty to keep it":"";
if(view=="onvif")fill("onvifInfo",[["Login",i.onvifLogin?"Required: user admin and the hub password":"None"],["Device address","http://"+i.ip+":"+i.onvifPort+"/onvif/device_service"],["Event subscribers",i.subscribers],["Serial number",i.serial],["Requests not supported",i.unsupported.join(", ")||"None"]]);
if(view=="http")fill("httpInfo",[["Address","http://"+i.ip+at+"/"],["Name","http://"+i.name+at+"/"],["Sensor token","add &token="+i.token+" to /sensor requests"]])}catch(e){}}
// Asks once a second which picture is being sent, and fetches and decodes one only when it changes.
let liveRun=0,liveSeq=null,liveUrl=null;
async function livePoll(run){if(run!=liveRun)return;let wait=1000;const m=$("liveMsg");
const get=u=>fetch(u,{cache:"no-store"}).catch(()=>{throw new Error("Could not reach the hub. Trying again.")});
try{const j=await(await get("/api/live")).json();if(run!=liveRun)return;$("liveFile").textContent=j.file;
if(j.seq!==liveSeq){if(liveSeq===null)m.textContent="Getting the decoder ready. The first time, this downloads about 30 MB.";
const r=await get("/api/live/frame");if(!r.ok)throw new Error(r.status==507?"Both video connections are in use, or the hub is sending another file. Trying again.":"The hub could not send the picture.");
const seq=Number(r.headers.get("X-Frame-Seq")),data=new Uint8Array(await r.arrayBuffer());let url;
try{url=await decoded(data)}catch(e){throw new Error("Could not show the picture. The decoder needs internet access the first time. "+(e.message||""))}
if(run!=liveRun){URL.revokeObjectURL(url);return}
if(liveUrl)URL.revokeObjectURL(liveUrl);liveUrl=url;const img=$("liveImg");img.src=url;img.style.display="block";liveSeq=seq;m.className="portMsg";m.textContent=""}}
catch(e){wait=5000;m.className="portMsg err";m.textContent=e.message||"Could not show the picture."}
setTimeout(()=>livePoll(run),wait)}
function setLive(on){liveRun++;if(on){liveSeq=null;livePoll(liveRun)}}
function netState(){const off=$("netMode").value=="dhcp";["netIp","netMask","netGw","netDns"].forEach(i=>$(i).disabled=off)}
let sysTimer=0;
const ZONES=[-720,-660,-600,-540,-480,-420,-360,-300,-270,-240,-210,-180,-120,-60,0,60,120,180,210,240,270,300,330,345,360,390,420,480,540,570,600,660,720,780];
const two=n=>String(n).padStart(2,"0");
$("tmZone").replaceChildren(...ZONES.map(z=>el("option",{value:z,textContent:"UTC"+(z<0?"-":"+")+two(Math.floor(Math.abs(z)/60))+":"+two(Math.abs(z)%60)})));
function timeState(){const manual=$("tmMode").value=="manual";$("tmServer").disabled=manual;$("tmManual").disabled=!manual}
$("tmMode").onchange=timeState;
async function tickTime(){try{const t=await(await fetch("/api/time")).json();$("tmNow").value=t.synced?t.local:"Not set yet";return t}catch(e){return null}}
async function loadTime(){const t=await tickTime();if(!t)return;$("tmZone").value=t.offset;$("tmDst").value=t.dst;$("tmMode").value=t.ntp?"ntp":"manual";$("tmServer").value=t.server;timeState()}
$("tmFromPc").onclick=()=>{const d=new Date();$("tmMode").value="manual";timeState();
$("tmManual").value=d.getFullYear()+"-"+two(d.getMonth()+1)+"-"+two(d.getDate())+"T"+two(d.getHours())+":"+two(d.getMinutes())+":"+two(d.getSeconds());
const m=$("tmMsg");m.className="portMsg";m.textContent="Read from this PC. Press Save to set the hub to this time."};
$("saveTime").onclick=async()=>{const m=$("tmMsg"),manual=$("tmMode").value=="manual",v=$("tmManual").value;m.className="portMsg";
if(manual&&!v){m.className="portMsg err";m.textContent="Enter the date and time, or use Read from this PC.";return}
let body="mode="+(manual?"manual":"ntp")+"&offset="+$("tmZone").value+"&dst="+$("tmDst").value+"&server="+encodeURIComponent($("tmServer").value);
if(manual)body+="&epoch="+Math.floor(new Date(v).getTime()/1000);
if(await send(m,"/api/time",body,"Saved."))tickTime()};
async function loadSchedule(){try{const s=await(await fetch("/api/schedule")).json();$("rbOn").checked=s.on;$("rbDay").value=s.day;$("rbTime").value=two(Math.floor(s.minute/60))+":"+two(s.minute%60)}catch(e){}}
$("saveRb").onclick=async()=>{const m=$("rbMsg"),v=$("rbTime").value;m.className="portMsg";
if(!v){m.className="portMsg err";m.textContent="Enter a time.";return}
const minute=Number(v.slice(0,2))*60+Number(v.slice(3,5));
send(m,"/api/schedule","on="+($("rbOn").checked?1:0)+"&day="+$("rbDay").value+"&minute="+minute,"Saved.")};
async function sysAction(url,body,done,after){if(await send($("sysMsg"),url,body,done)&&after)setTimeout(after,12000)}
$("rebootNow").onclick=()=>{if(confirm("Restart the hub now? The video stops for a few seconds."))sysAction("/api/reboot","","The hub is restarting.",()=>location.reload())};
$("resetAll").onclick=()=>{if(confirm("Erase the saved WiFi networks, ports, name, web password, fixed address and time settings? The hub will restart and may move to another address."))sysAction("/api/reset","confirm=yes","Settings erased. The hub is restarting. If it cannot join a network, it opens its setup network.")};
async function loadDevice(){try{const j=await(await fetch("/api/device")).json();fill("devInfo",j.rows);
  if(!j.crash){$("crashActs").replaceChildren();return}
  if($("crashActs").childElementCount)return;  // refreshed every 5 s; leave the buttons alone
  $("crashActs").replaceChildren(el("a",{className:"btn",href:"/api/crash",download:"hub-crash.bin",textContent:"Download crash report"})," ",
    el("button",{className:"del",textContent:"Delete crash report",onclick:()=>{if(confirm("Delete the crash report?"))act(()=>call("POST","/api/crash/delete","x=1"),"Deleted the crash report").then(loadDevice)}}),
    el("br"),"For diagnosis with the firmware it came from. It holds a copy of the hub's memory, which can include passwords.")}catch(e){}}
function setSys(view){clearInterval(sysTimer);
if(view=="maintenance")loadSchedule();
if(view=="events"){loadEvents();sysTimer=setInterval(loadEvents,3000)}
if(view=="time"){loadTime();sysTimer=setInterval(tickTime,1000)}
if(view=="device"){loadDevice();sysTimer=setInterval(loadDevice,5000)}
if(view=="notify"){loadHook(true);sysTimer=setInterval(()=>loadHook(false),3000)}}
async function loadHook(all){try{const h=await call("GET","/api/webhook");
if(all){$("hookUrl").value=h.url;$("hookText").value=h.text?"1":"0";$("hookAlerts").checked=h.alerts;$("hookProblems").checked=h.problems}
fill("hookInfo",[["Last message",h.last?h.last+(h.lastAt?", "+new Date(h.lastAt).toLocaleString():""):"None sent yet"],["Waiting to send",h.waiting],["Dropped, too many waiting",h.dropped]])}catch(e){}}
$("saveHook").onclick=async()=>{if(await send($("hookMsg"),"/api/webhook","url="+encodeURIComponent($("hookUrl").value.trim())+"&text="+$("hookText").value+"&alerts="+($("hookAlerts").checked?1:0)+"&problems="+($("hookProblems").checked?1:0),"Saved."))loadHook(false)};
$("testHook").onclick=async()=>{if(await send($("hookMsg"),"/api/webhook/test","x=1","Test sent. The result shows below within a few seconds."))setTimeout(()=>loadHook(false),3000)};
// A full backup is a tar file made and read here, so the hub never holds more than one picture at a time.
function tarHeader(name,size){const h=new Uint8Array(512),put=(s,at)=>{for(let i=0;i<s.length;i++)h[at+i]=s.charCodeAt(i)};
put(name,0);put("0000644",100);put("0000000",108);put("0000000",116);put(size.toString(8).padStart(11,"0"),124);
put(Math.floor(Date.now()/1000).toString(8).padStart(11,"0"),136);put("        ",148);h[156]=48;put("ustar",257);put("00",263);
let sum=0;for(const b of h)sum+=b;put(sum.toString(8).padStart(6,"0"),148);h[154]=0;h[155]=32;return h}
function tarBuild(files){const parts=[];for(const[name,data]of files){parts.push(tarHeader(name,data.length),data);const pad=(512-data.length%512)%512;if(pad)parts.push(new Uint8Array(pad))}
parts.push(new Uint8Array(1024));return new Blob(parts,{type:"application/x-tar"})}
function tarRead(bytes){let ended=false;const out=[],text=(a,b)=>String.fromCharCode(...bytes.subarray(a,b)).replace(/\0[\s\S]*$/,"");
for(let at=0;at+512<=bytes.length;){if(bytes.subarray(at,at+512).every(b=>b==0)){ended=true;break}
const name=text(at,at+100),size=parseInt(text(at+124,at+136).trim(),8);
if(!name||isNaN(size)||at+512+size>bytes.length)throw new Error("This file is damaged, or is not a full backup from the hub.");
const type=bytes[at+156];if(type==48||type==0)out.push([name,bytes.slice(at+512,at+512+size)]);at+=512+Math.ceil(size/512)*512}
if(!ended)throw new Error("This file is cut short, or is not a full backup from the hub.");
return out}
// The hub answers 507 while its one spare buffer is in use, so those requests are tried again.
async function fetchRetry(url,opts){for(let i=0;;i++){const r=await fetch(url,opts);if(r.status!=507||i==5)return r;await new Promise(z=>setTimeout(z,1000))}}
$("fbSave").onclick=async()=>{const m=$("fbMsg");m.className="portMsg";try{
const files=[],add=async path=>{const r=await fetchRetry("/api/file?path="+path);if(r.status==404)return;if(!r.ok)throw new Error("The hub could not send "+path);
  files.push([path,new Uint8Array(await r.arrayBuffer())]);m.textContent="Collected "+files.length+" files"};
const b=await fetch("/api/backup");if(!b.ok)throw new Error("The hub could not send its settings.");
files.push(["hub-settings.txt",new Uint8Array(await b.arrayBuffer())]);
const t=await call("GET","/api/tree");
if(t.clear!=null)await add("clear.h264");if(t.clearPreview)await add("clear.jpg");await add("text.json");
for(const s of t.nodes){files.push([s.mac+"/.node",new Uint8Array(0)]);if(s.late)await add(s.mac+"/settings.txt");
  for(const n of s.sensors){const base=s.mac+"/"+n.input;files.push([base+"/.sensor",new Uint8Array(0)]);
    for(const f of n.files){await add(base+"/"+f.name);if(f.preview)await add(base+"/"+f.name.replace(/\.h264$/,".jpg"))}
    if(n.name)await add(base+"/name.txt");await add(base+"/text.json");if(n.hold||n.quiet)await add(base+"/settings.txt")}}
const a=el("a",{href:URL.createObjectURL(tarBuild(files)),download:"hub-backup-"+new Date().toISOString().slice(0,10)+".tar"});
document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(a.href),10000);
m.textContent="Saved "+files.length+" files. The file holds the WiFi and hub passwords, so keep it somewhere safe."}catch(e){m.className="portMsg err";m.textContent=e.message}};
$("fbLoad").onclick=async()=>{const m=$("fbMsg"),f=$("fbFile").files[0];m.className="portMsg";
if(!f){m.className="portMsg err";m.textContent="Choose a full backup .tar file first.";return}
if(!confirm("Restore this backup? Its sensors, pictures and names are added, replacing any with the same name, then the settings are replaced and the hub restarts. It may move to another address."))return;
try{const list=[];let settings=null;
for(const[n,d]of tarRead(new Uint8Array(await f.arrayBuffer()))){if(n=="hub-settings.txt")settings=d;else list.push([n,d])}
if(!settings)throw new Error("This is not a full backup from the hub.");
// Folders first. A new picture removes its thumbnail, so thumbnails go after pictures.
const rank=p=>/\/\.(node|sensor)$/.test(p)?0:p.endsWith(".h264")?1:p.endsWith(".jpg")?2:3;
list.sort((a,b)=>rank(a[0])-rank(b[0]));
const failed=[];
for(let i=0;i<list.length;i++){const[path,data]=list[i],folder=/\/\.(node|sensor)$/.test(path);m.textContent="Restoring "+(i+1)+" of "+list.length+": "+path;
  const r=await fetchRetry(folder?"/api/mkdir?path="+path.replace(/\/\.(node|sensor)$/,""):"/api/file?path="+path,{method:folder?"POST":"PUT",body:folder?null:data});
  if(!r.ok){let j={};try{j=await r.json()}catch(e){}failed.push(path+": "+(j.error||"HTTP "+r.status))}}
m.textContent="Restoring the settings";
const r=await fetch("/api/restore",{method:"POST",body:settings});
if(!r.ok)throw new Error("The sensors and pictures were restored, but the hub refused the settings in this backup.");
m.textContent="Restored "+(list.length-failed.length)+" of "+list.length+" items and the settings. The hub is restarting."+(failed.length?" Not restored: "+failed.join("; "):"");
if(failed.length)m.className="portMsg err";setTimeout(()=>location.reload(),15000)}catch(e){m.className="portMsg err";m.textContent=e.message}};
let espTimer=0;
function heardAgo(s){return s==null?"Never":s<60?s+" s ago":s<3600?Math.floor(s/60)+" min ago":s<86400?Math.floor(s/3600)+" h ago":Math.floor(s/86400)+" d ago"}
async function loadEsp(){try{const j=await(await fetch("/api/espnow")).json();
fill("espInfo",[["Hub MAC address",j.mac],["WiFi channel",j.channel],["ESP-NOW key",j.key]]);$("espUnsigned").checked=j.unsigned;
const STATES={active:"Active",idle:"Idle",silent:"Silent (no report for 3 hours)",never:"Not heard yet"};
$("espNodes").replaceChildren(...(j.nodes.length?[headRow(["Name","Node","Sensor","State","Last heard","Battery","Signal","Report no.","Refused"])]:[]),
...j.nodes.map(n=>el("tr",null,...[n.name||"-",n.node,n.input,STATES[n.state],heardAgo(n.ago),n.battery==null?"-":n.battery+"%"+(n.low?" (low)":""),n.rssi==null?"-":n.rssi+" dBm"+(n.weak?" (weak)":""),n.counter==null?"-":n.counter,n.refused?n.refused+" ("+n.refusal+")":"-"].map(v=>el("td",{textContent:v})))));
$("espEmpty").textContent=j.nodes.length?"":"No sensor nodes yet. Add one under Sensors Manager > Sensors.";
$("espUnknown").replaceChildren(...(j.unknown.length?[headRow(["Node","Sensor","Last heard","Signal","Messages",""])]:[]),
...j.unknown.map(n=>el("tr",null,...[n.mac,n.input,heardAgo(n.ago),n.rssi==null?"-":n.rssi+" dBm",n.messages].map(v=>el("td",{textContent:v})),el("td",null,el("button",{textContent:"Add node",onclick:async()=>{try{await call("POST","/api/mkdir?path="+n.mac+"/"+n.input);loadEsp();load()}catch(e){$("espNone").textContent=e.message}}})))));
$("espNone").textContent=j.unknown.length?"These are ignored until added under Sensors Manager > Sensors.":"None since the hub last started."}catch(e){}}
function setEsp(on){clearInterval(espTimer);if(on){loadEsp();call("GET","/api/alerts").then(a=>$("espAlarm").checked=a.alarm).catch(()=>{});espTimer=setInterval(loadEsp,3000)}}
$("espAlarm").onchange=e=>{call("POST","/api/alerts","alarm="+(e.target.checked?1:0)).catch(()=>{})};
$("espUnsigned").onchange=e=>{call("POST","/api/espnow","unsigned="+(e.target.checked?1:0)).catch(()=>{})};
const DIRECTIONS=["","approaching","moving away","left to right","right to left"];
async function loadEvents(){try{const list=await call("GET","/api/events");
$("evTable").replaceChildren(...(list.length?[headRow(["Time","Sensor","Event","Details","Battery"])]:[]),
...list.map(e=>{const when=e.happened||e.received,what=e.kind==1?"Went silent":e.kind==2?"Low battery":e.kind==3?"Alert in quiet hours":e.kind==4?"Weak signal":e.active?"Active":"Clear";
const details=[e.delayed?"delayed":""].concat(e.targets.map(t=>[t.distance_cm==null?"":(t.distance_cm/100).toFixed(2)+" m",DIRECTIONS[t.direction]||""].filter(Boolean).join(" "))).filter(Boolean).join(", ");
return el("tr",null,...[when?new Date(when).toLocaleString():"Clock not set",e.name||e.sensor,what,details||"-",e.battery==null?"-":e.battery+"%"].map(v=>el("td",{textContent:v})))}));
$("evNone").textContent=list.length?"":"No events yet."}catch(e){}}
$("fwSend").onclick=async()=>{const m=$("fwMsg"),f=$("fwFile").files[0];m.className="portMsg";
if(!f){m.className="portMsg err";m.textContent="Choose a firmware .bin file first.";return}
if(!confirm("Update the firmware? The hub restarts when it finishes, and the video stops until then."))return;
m.textContent="Uploading. Keep this page open.";
if(await send(m,"/api/firmware",f,"Done. The hub is restarting.","PUT","The upload failed. The hub keeps its current firmware."))setTimeout(()=>location.reload(),25000)};
$("rsSend").onclick=async()=>{const m=$("rsMsg"),f=$("rsFile").files[0];m.className="portMsg";
if(!f){m.className="portMsg err";m.textContent="Choose a backup file first.";return}
if(!confirm("Replace the hub's settings with this backup? It will restart and may move to another address."))return;
send(m,"/api/restore",await f.text(),"Restored. The hub is restarting.")};
async function loadNvr(){try{const n=await(await fetch("/api/nvr")).json();$("nvrIp").value=n.ip;
const state=on=>!n.ip?"Enter the NVR's IP address":on?"Connected":"Not connected";
fill("nvrInfo",[["Video",state(n.video)],["Motion events",state(n.events)],["Viewers connected now",n.viewers.join(", ")||"None"]])}catch(e){}}
$("saveNvr").onclick=async()=>{if(await send($("nvrMsg"),"/api/nvr","ip="+encodeURIComponent($("nvrIp").value),"Saved."))loadNvr()};
async function loadNet(){try{const n=await(await fetch("/api/network")).json();
$("netType").value=n.type;$("netMode").value=n.dhcp?"dhcp":"static";$("netIp").value=n.ip;$("netMask").value=n.mask;$("netGw").value=n.gateway;$("netDns").value=n.dns;$("netMac").value=n.mac;netState();
$("netMsg").className="portMsg";$("netMsg").textContent=n.dhcp?"":"This fixed address is used only on "+n.staticFor+". Other WiFi networks use DHCP."}catch(e){}}
$("netMode").onchange=netState;
$("saveNet").onclick=async()=>{const m=$("netMsg"),mode=$("netMode").value,ip=$("netIp").value;
if(!confirm("The hub will restart to apply this. A fixed address is used only on the WiFi network it is connected to now. Continue?"))return;
if(!await send(m,"/api/network","mode="+mode+"&ip="+encodeURIComponent(ip)+"&mask="+encodeURIComponent($("netMask").value)+"&gateway="+encodeURIComponent($("netGw").value)+"&dns="+encodeURIComponent($("netDns").value)))return;
const named=location.hostname.endsWith(".local");
m.textContent=mode=="static"||named?"Saved. The hub is restarting.":"Saved. The hub is restarting and will get its address from the router. Open it by name, or find the new address in the router.";
if(mode=="static"||named)setTimeout(()=>{location.href=location.protocol+"//"+(named?location.hostname:ip)+(location.port?":"+location.port:"")+"/"+location.hash},12000)};
async function savePort(field,input,msg,warning){const v=$(input).value,m=$(msg);
if(!confirm(warning))return;
if(await send(m,"/api/settings",field+"="+encodeURIComponent(v),"Saved. The hub is restarting."))setTimeout(()=>location.reload(),10000)}
$("protect").onchange=e=>{$("pwRow").hidden=!e.target.checked};
$("saveRtsp").onclick=()=>savePort("rtsp","rtspPort","rtspMsg","The hub will restart and the video will stop briefly. A viewer such as VLC will need the new address. Continue?");
$("saveOnvif").onclick=()=>savePort("onvif","onvifPort","onvifMsg","The hub will restart, and the NVR will lose this camera until its channel is set to the new port. Continue?");
$("saveWeb").onclick=async()=>{const m=$("webMsg"),port=$("webPort").value,name=$("hubName").value,on=$("protect").checked;
if(!confirm("The hub will restart to apply these settings. Continue?"))return;
if(!await send(m,"/api/settings","web="+encodeURIComponent(port)+"&name="+encodeURIComponent(name)+"&protect="+(on?"on":"off")+"&password="+encodeURIComponent($("webPw").value),"Saved. The hub is restarting."))return;
const host=location.hostname.endsWith(".local")?name+".local":location.hostname;
setTimeout(()=>{location.href=location.protocol+"//"+host+":"+port+"/"+location.hash},10000)};
function expand(f,on){f.classList.toggle("open",on);f.nextElementSibling.classList.toggle("show",on)}
function show(view){if(!$("view-"+view))view="sensors";
document.querySelectorAll(".view").forEach(v=>v.hidden=v.id!="view-"+view);
items.forEach(i=>{const on=i.dataset.view==view;i.classList.toggle("active",on);if(on){firsts.forEach(f=>expand(f,false));expand(i.parentElement.previousElementSibling,true)}});
if(view=="wifi"){const n=$("netFrame");if(!n.getAttribute("src"))n.src="/setup"}
if(view=="network")loadNet();
if(view=="nvr")loadNvr();
setLive(view=="live");
setEsp(view=="espnow");
setSys(view);
if(view!="sensors")info(view)}
firsts.forEach(f=>f.onclick=()=>{const on=!f.classList.contains("open");firsts.forEach(o=>expand(o,false));expand(f,on)});
items.forEach(i=>i.onclick=()=>{location.hash=i.dataset.view});
addEventListener("hashchange",()=>show(location.hash.slice(1)));
show(location.hash.slice(1));
fetch("/api/info").then(r=>r.json()).then(i=>{$("openWarn").hidden=i.password}).catch(()=>{});
})();
</script></body></html>
)HTML";
