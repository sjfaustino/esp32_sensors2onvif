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
