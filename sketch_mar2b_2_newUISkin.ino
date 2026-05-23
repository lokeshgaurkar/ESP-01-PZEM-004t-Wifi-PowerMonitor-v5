#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PZEM004Tv30.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <Updater.h>

const char* ap_ssid = "PowerMonitor";
const char* ap_password = "11223344";
const char* hostname = "powermonitor";

ESP8266WebServer server(80);

PZEM004Tv30 pzem(Serial);

float voltage, current, power, pf, freq;
double energy_kwh = 0;
double energy_offset_kwh = 0;

unsigned long lastSave = 0;
unsigned long lastReadMs = 0;

unsigned long perfLastSampleMs = 0;
unsigned long perfLastLoopUs = 0;
unsigned long perfBusyUsAcc = 0;
unsigned long perfTotalUsAcc = 0;
float cpuLoadPct = 0;
uint32_t heapBootBytes = 0;

#define EEPROM_SIZE 512
#define ENERGY_ADDR 0
#define TARIFF_ADDR 32
#define ENERGY_OFFSET_ADDR 80

struct Tariff {
  float meter;
  float ship;
  float tax;
  float r1;
  float r2;
  float r3;
};

Tariff tariff;

// Smoothing factor for displayed live values (0..1). Higher = faster response.
const float EMA_ALPHA = 0.75f;

void updatePerfStats(unsigned long loopStartUs) {
  unsigned long nowUs = micros();
  unsigned long busyUs = nowUs - loopStartUs;

  if (perfLastLoopUs != 0) {
    unsigned long periodUs = loopStartUs - perfLastLoopUs;
    if (periodUs > 0) {
      perfBusyUsAcc += busyUs;
      perfTotalUsAcc += periodUs;
    }
  }
  perfLastLoopUs = loopStartUs;

  unsigned long nowMs = millis();
  if ((unsigned long)(nowMs - perfLastSampleMs) >= 1000UL) {
    if (perfTotalUsAcc > 0) {
      float inst = (100.0f * (float)perfBusyUsAcc) / (float)perfTotalUsAcc;
      if (inst < 0) inst = 0;
      if (inst > 100) inst = 100;
      cpuLoadPct = (cpuLoadPct == 0) ? inst : (0.65f * cpuLoadPct + 0.35f * inst);
    }
    perfBusyUsAcc = 0;
    perfTotalUsAcc = 0;
    perfLastSampleMs = nowMs;
  }
}

void updateMeasurements() {
  unsigned long now = millis();
  if ((unsigned long)(now - lastReadMs) < 250) return;  // faster ~4 Hz sensor update
  lastReadMs = now;

  float rv = pzem.voltage();
  float rc = pzem.current();
  float rp = pzem.power();
  float rf = pzem.frequency();
  float rpf = pzem.pf();
  float re = pzem.energy();  // PZEM hardware cumulative energy (kWh)

  if (!isnan(rv) && rv >= 0) {
    if (voltage == 0) voltage = rv;
    else voltage = (EMA_ALPHA * rv) + ((1.0f - EMA_ALPHA) * voltage);
  }

  if (!isnan(rc) && rc >= 0) {
    if (current == 0) current = rc;
    else current = (EMA_ALPHA * rc) + ((1.0f - EMA_ALPHA) * current);
  }

  if (!isnan(rp) && rp >= 0) {
    if (power == 0) power = rp;
    else power = (EMA_ALPHA * rp) + ((1.0f - EMA_ALPHA) * power);
  }

  if (!isnan(rf) && rf > 0) {
    if (freq == 0) freq = rf;
    else freq = (EMA_ALPHA * rf) + ((1.0f - EMA_ALPHA) * freq);
  }

  if (!isnan(rpf) && rpf >= 0 && rpf <= 1.0f) {
    if (pf == 0) pf = rpf;
    else pf = (EMA_ALPHA * rpf) + ((1.0f - EMA_ALPHA) * pf);
  }

  // Use hardware energy counter as source of truth for best long-term accuracy.
  if (!isnan(re) && re >= 0) {
    energy_kwh = re - energy_offset_kwh;
    if (energy_kwh < 0) energy_kwh = 0;
  }

  if ((unsigned long)(now - lastSave) > 300000UL) {
    EEPROM.put(ENERGY_ADDR, energy_kwh);
    EEPROM.put(ENERGY_OFFSET_ADDR, energy_offset_kwh);
    EEPROM.commit();
    lastSave = now;
  }
}

const char webpage[] PROGMEM = R"=====(

<!DOCTYPE html>
<html lang="en">

<head>

<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">


<title>PowerMonitor</title>
<style>

@font-face{
font-family:"Sora";
src:url("/fonts/Sora-Regular.woff2") format("woff2");
font-weight:400;
font-style:normal;
font-display:swap;
}

@font-face{
font-family:"Sora";
src:url("/fonts/Sora-SemiBold.woff2") format("woff2");
font-weight:600;
font-style:normal;
font-display:swap;
}

@font-face{
font-family:"Sora";
src:url("/fonts/Sora-Bold.woff2") format("woff2");
font-weight:700;
font-style:normal;
font-display:swap;
}

@font-face{
font-family:"JetBrains Mono";
src:url("/fonts/JetBrainsMono-SemiBold.woff2") format("woff2");
font-weight:600;
font-style:normal;
font-display:swap;
}

:root{
--bg:#0b1020;
--panel:#131a30;
--panel2:#1a2342;
--text:#eaf2ff;
--muted:#99a8c8;
--line:#2c3a66;
--accent:#27d3a5;
--danger:#ff5f70;
--blue:#5d8dff;
--radius:16px;
}

*{
box-sizing:border-box;
}

body{
font-family:"Sora","Segoe UI",Tahoma,sans-serif;
background:
radial-gradient(70vw 70vw at 10% -20%, #263774 0%, transparent 60%),
radial-gradient(80vw 80vw at 110% 10%, #1b755d 0%, transparent 60%),
var(--bg);
color:var(--text);
margin:0;
min-height:100vh;
}

.header{
font-size:clamp(24px,4vw,34px);
font-weight:700;
letter-spacing:.4px;
padding:24px 18px 10px;
text-align:center;
display:flex;
align-items:center;
justify-content:center;
gap:10px;
}

.liveDot{
width:10px;
height:10px;
border-radius:50%;
background:#27d3a5;
box-shadow:0 0 0 0 rgba(39,211,165,.65);
animation:pulse 1.8s infinite;
}

.grid{
display:grid;
grid-template-columns:repeat(auto-fit,minmax(170px,1fr));
gap:14px;
padding:16px;
max-width:1040px;
margin:0 auto;
}

.card{
background:linear-gradient(160deg,rgba(255,255,255,.05),rgba(255,255,255,.01));
border:1px solid rgba(149,170,220,.22);
backdrop-filter:blur(8px);
padding:16px;
border-radius:var(--radius);
text-align:left;
box-shadow:0 14px 34px rgba(0,0,0,.25);
animation:rise .35s ease both;
transition:transform .2s ease, box-shadow .2s ease, border-color .2s ease;
}
.card:hover{
transform:translateY(-3px);
border-color:rgba(149,170,220,.38);
box-shadow:0 18px 36px rgba(0,0,0,.32);
}

.card:nth-child(1){animation-delay:.04s}
.card:nth-child(2){animation-delay:.08s}
.card:nth-child(3){animation-delay:.12s}
.card:nth-child(4){animation-delay:.16s}
.card:nth-child(5){animation-delay:.20s}
.card:nth-child(6){animation-delay:.24s}
.card:nth-child(7){animation-delay:.28s}
.card:nth-child(8){animation-delay:.32s}

.usageBarTrack{
margin-top:10px;
width:100%;
height:9px;
border-radius:999px;
background:#101836;
overflow:hidden;
border:1px solid rgba(149,170,220,.22);
}

.usageBarFill{
height:100%;
width:0%;
border-radius:999px;
transition:width .35s ease;
}

.cpuFill{background:linear-gradient(90deg,#5d8dff,#8ca7ff)}
.ramFill{background:linear-gradient(90deg,#27d3a5,#5de0ff)}

.cardLabel{
font-size:12px;
color:var(--muted);
text-transform:uppercase;
letter-spacing:.08em;
}

.value{
font-family:"JetBrains Mono","Consolas","Courier New",monospace;
font-size:clamp(24px,4vw,30px);
margin-top:10px;
font-weight:700;
letter-spacing:.2px;
transition:color .25s ease, transform .25s ease;
}

.value.pop{
animation:valuePop .38s ease;
}

.unit{
font-size:12px;
margin-top:6px;
color:var(--muted);
font-weight:600;
}

button{
cursor:pointer;
-webkit-tap-highlight-color:transparent;
font-family:"Sora",sans-serif;
font-weight:600;
transition:transform .15s ease, box-shadow .2s ease, filter .2s ease;
}

button:hover{
filter:brightness(1.06);
}

button:active{
transform:translateY(1px) scale(.995);
}

.resetBtn{
padding:12px 16px;
border:none;
border-radius:12px;
background:linear-gradient(145deg,#ff6a7f,#f1445d);
color:var(--text);
font-size:15px;
box-shadow:0 10px 24px rgba(239,68,68,.28);
}

.billbox{
background:linear-gradient(160deg,var(--panel),var(--panel2));
margin:10px 16px 22px;
padding:20px;
border-radius:var(--radius);
max-width:1040px;
margin-left:auto;
margin-right:auto;
border:1px solid rgba(149,170,220,.18);
box-shadow:0 20px 50px rgba(0,0,0,.3);
}

.billbox h2{
margin:0 0 16px;
font-size:20px;
text-align:left;
}

.inputs{
display:grid;
grid-template-columns:repeat(auto-fit,minmax(160px,1fr));
gap:12px;
}

.inputgroup{
display:flex;
flex-direction:column;
text-align:left;
}

.inputgroup label{
font-size:12px;
color:var(--muted);
margin-bottom:4px;
}

.inputgroup input{
padding:11px;
border-radius:12px;
border:1px solid var(--line);
background:#0e152a;
color:var(--text);
font-size:14px;
outline:none;
}

.inputgroup input:focus{
border-color:#65b2ff;
box-shadow:0 0 0 3px rgba(101,178,255,.2);
}

.saveBtn{
width:100%;
padding:12px;
border:none;
border-radius:12px;
background:linear-gradient(145deg,#27d3a5,#1fb889);
color:#05261e;
font-size:15px;
margin-top:14px;
box-shadow:0 10px 24px rgba(39,211,165,.25);
}

.otaBtn{
padding:12px 16px;
border:none;
border-radius:12px;
background:linear-gradient(145deg,#6ca1ff,#4977ff);
color:#eaf2ff;
font-size:15px;
box-shadow:0 10px 24px rgba(93,141,255,.24);
}

.billtable{
width:100%;
margin-top:16px;
border-collapse:collapse;
font-size:14px;
}

.billtable td{
padding:11px 4px;
border-bottom:1px solid rgba(88,109,162,.35);
}

.billtable td:last-child{
text-align:right;
font-family:"JetBrains Mono","Consolas","Courier New",monospace;
font-weight:600;
}

.totalrow{
font-size:19px;
font-weight:bold;
color:var(--accent);
}

#Volt{
color:#ff8e9b;
}
#Amp{
color:#54ef9e;
}
#Watt{
color:#ffe76d;
}
#kWh{
color:#62eaff;
}
#Hz{
color:#8ca7ff;
}
#PF{
color:#ffa8f2;
}

.actions{
display:flex;
gap:10px;
justify-content:center;
flex-wrap:wrap;
padding:6px 16px 16px;
}

.footer{
margin:8px 0 24px;
font-size:12px;
color:var(--muted);
text-align:center;
cursor:pointer;
user-select:none;
transition:color .2s ease, transform .15s ease;
}

.footer:hover{
color:#c6d5f7;
}

.footer:active{
transform:translateY(1px);
}

.toast{
position:fixed;
left:50%;
bottom:20px;
transform:translateX(-50%) translateY(20px);
background:rgba(10,16,34,.95);
color:var(--text);
border:1px solid rgba(149,170,220,.35);
border-radius:12px;
padding:12px 14px;
font-size:13px;
line-height:1.45;
width:min(92vw,420px);
box-shadow:0 14px 34px rgba(0,0,0,.35);
opacity:0;
pointer-events:none;
transition:opacity .25s ease, transform .25s ease;
z-index:20;
}

.toast.show{
opacity:1;
transform:translateX(-50%) translateY(0);
}

@keyframes rise{
from{opacity:0;transform:translateY(12px)}
to{opacity:1;transform:translateY(0)}
}

@keyframes pulse{
0%{box-shadow:0 0 0 0 rgba(39,211,165,.65)}
70%{box-shadow:0 0 0 12px rgba(39,211,165,0)}
100%{box-shadow:0 0 0 0 rgba(39,211,165,0)}
}

@keyframes valuePop{
0%{transform:translateY(0) scale(1)}
35%{transform:translateY(-2px) scale(1.035)}
100%{transform:translateY(0) scale(1)}
}

@media (max-width:640px){
.billbox h2{font-size:18px}
.grid{grid-template-columns:repeat(2,minmax(0,1fr))}
.value{font-size:23px}
}

</style>

</head>

<body>

<div class="header"><span class="liveDot"></span>Power ⚡︎ Monitor</div>

<div class="grid">
<div class="card"><div class="cardLabel">Voltage</div><div class="value" id="v">--</div><div class="unit" id="Volt">Volt</div></div>
<div class="card"><div class="cardLabel">Current</div><div class="value" id="c">--</div><div class="unit"id="Amp">Amp</div></div>
<div class="card"><div class="cardLabel">Power</div><div class="value" id="p">--</div><div class="unit"id="Watt">Watt</div></div>
<div class="card"><div class="cardLabel">Energy</div><div class="value" id="e">--</div><div class="unit"id="kWh">kWh</div></div>
<div class="card"><div class="cardLabel">Frequency</div><div class="value" id="f">--</div><div class="unit"id="Hz">Hz</div></div>
<div class="card"><div class="cardLabel">Power Factor</div><div class="value" id="pf">--</div><div class="unit"id="PF">PF</div></div>
<div class="card"><div class="cardLabel">CPU Usage</div><div class="value" id="cpu">--</div><div class="usageBarTrack"><div class="usageBarFill cpuFill" id="cpuBar"></div></div><div class="unit">Loop load</div></div>
<div class="card"><div class="cardLabel">RAM Usage</div><div class="value" id="ram">--</div><div class="usageBarTrack"><div class="usageBarFill ramFill" id="ramBar"></div></div><div class="unit" id="ramFree">Free: --</div></div>
</div>

<div class="actions">
<button id="resetBtn" class="resetBtn">Reset Energy</button>
</div>

<div class="billbox">

<h2>Estimate Electricity Bill</h2>

<div class="inputs">
<div class="inputgroup">
<label>Meter Charge</label>
<input id="meter" type="number">
</div>

<div class="inputgroup">
<label>Shipping / Unit</label>
<input id="ship" type="number">
</div>

<div class="inputgroup">
<label>Tax % (on total bill)</label>
<input id="tax" type="number">
</div>

<div class="inputgroup">
<label>0-100 Unit Price</label>
<input id="rate1" type="number">
</div>

<div class="inputgroup">
<label>101-300 Unit Price</label>
<input id="rate2" type="number">
</div>

<div class="inputgroup">
<label>300+ Unit Price</label>
<input id="rate3" type="number">
</div>
</div>

<button id="saveBtn" class="saveBtn">Save Calculator Settings</button>

<table class="billtable">

<tr><td>Energy Charges</td><td id="energyCost">0</td></tr>
<tr><td>Meter Charges</td><td id="meterCost">0</td></tr>
<tr><td>Shipping Charges</td><td id="shipCost">0</td></tr>
<tr><td>Electricity Tax</td><td id="taxCost">0</td></tr>
<tr class="totalrow"><td>Total Bill</td><td id="bill">0</td></tr>

</table>

</div>

<div class="actions">
<a href="/update">
<button id="otaBtn" class="otaBtn">Firmware Update</button>
</a>
</div>

<div class="footer" id="devFooter">Made with ❤️ by Lokesh</div>
<div class="toast" id="devToast">
<strong>Project: </strong>PowerMonitor<br>
<strong>Developer:</strong> Lokesh Gaurkar<br>
<strong>Email: </strong>Lokeshgaurkar444@gmail.com
</div>

<script>

function vibrate(){
if(navigator.vibrate){
navigator.vibrate(40);
}
}

/* Notification permission */

function requestNotificationPermission(){
if("Notification" in window){
if(Notification.permission!=="granted"){
Notification.requestPermission();
}
}
}

let lastNotify=0
const lastValues={}

function setMetric(id,nextValue){
const el=document.getElementById(id)
if(!el) return
const nextText=String(nextValue)
if(lastValues[id]!==nextText){
el.classList.remove("pop")
void el.offsetWidth
el.classList.add("pop")
lastValues[id]=nextText
}
el.innerHTML=nextText
}

function powerNotification(data){

if(Notification.permission==="granted"){

let now=Date.now()

if(now-lastNotify>60000){

new Notification("Power ⚡︎ Monitor",{
body:"Voltage: "+data.v+" V\nPower: "+data.p+" W\nEnergy: "+data.e+" kWh"
})

lastNotify=now

}

}

}

let confirmMode=false
let countdown=5
let timer
let toastTimer

function resetStep(){

let btn=document.getElementById("resetBtn")

if(!confirmMode){

confirmMode=true
countdown=5
btn.innerHTML="Confirm Reset (5)"

timer=setInterval(()=>{

countdown--
btn.innerHTML="Confirm Reset ("+countdown+")"

if(countdown==0){
clearInterval(timer)
confirmMode=false
btn.innerHTML="Reset Energy"
}

},1000)

}else{

fetch("/reset")
clearInterval(timer)
confirmMode=false
btn.innerHTML="Reset Energy"

}

}

function showDevToast(){
const t=document.getElementById("devToast")
if(!t) return
t.classList.add("show")
clearTimeout(toastTimer)
toastTimer=setTimeout(()=>{
t.classList.remove("show")
},3200)
}

function calcBill(){

let units=parseFloat(e.innerText)

let meterCharge=parseFloat(meter.value)
let shipCharge=parseFloat(ship.value)
let taxPercent=parseFloat(tax.value)

let r1=parseFloat(rate1.value)
let r2=parseFloat(rate2.value)
let r3=parseFloat(rate3.value)

let energyCharge=0

if(units<=100)
energyCharge=units*r1
else if(units<=300)
energyCharge=(100*r1)+((units-100)*r2)
else
energyCharge=(100*r1)+(200*r2)+((units-300)*r3)

let shippingCost=units*shipCharge

let subtotal=energyCharge+meterCharge+shippingCost
let taxAmount=subtotal*(taxPercent/100)

let total=subtotal+taxAmount

energyCost.innerHTML=energyCharge.toFixed(2)+" ₹"
meterCost.innerHTML=meterCharge.toFixed(2)+" ₹"
shipCost.innerHTML=shippingCost.toFixed(2)+" ₹"
taxCost.innerHTML=taxAmount.toFixed(2)+" ₹"
bill.innerHTML=total.toFixed(2)+" ₹"

}

function saveTariff(){

fetch(`/tariff?meter=${meter.value}&ship=${ship.value}&tax=${tax.value}&r1=${rate1.value}&r2=${rate2.value}&r3=${rate3.value}`)
.then(()=>alert("Calculator values saved"))

}

function update(){

fetch("/data")
.then(r=>r.json())
.then(data=>{

setMetric("v",data.v)
setMetric("c",data.c)
setMetric("p",data.p)
setMetric("e",data.e)
setMetric("f",data.f)
setMetric("pf",data.pf)
setMetric("cpu",data.cpu+" %")
setMetric("ram",data.ramu+" %")

const cpuBar=document.getElementById("cpuBar")
if(cpuBar) cpuBar.style.width=Math.min(100,Math.max(0,Number(data.cpu)||0))+"%"

const ramBar=document.getElementById("ramBar")
if(ramBar) ramBar.style.width=Math.min(100,Math.max(0,Number(data.ramu)||0))+"%"

const ramFree=document.getElementById("ramFree")
if(ramFree) ramFree.innerHTML="Free: "+data.ramf+" B"

if(!meter.value){
meter.value=data.meter
ship.value=data.ship
tax.value=data.tax
rate1.value=data.r1
rate2.value=data.r2
rate3.value=data.r3
}

calcBill()

if(data.p>5){
powerNotification(data)
}

})

}

document.getElementById("resetBtn").addEventListener("click",()=>{vibrate();resetStep();});
document.getElementById("saveBtn").addEventListener("click",()=>{vibrate();saveTariff();});
document.getElementById("otaBtn").addEventListener("click",()=>{vibrate();});
document.getElementById("devFooter").addEventListener("click",()=>{vibrate();showDevToast();});

requestNotificationPermission()

setInterval(update,500)
update()

</script>

</body>
</html>

)=====";

const char updatePage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PowerMonitor Firmware Update</title>
<link rel="icon" type="image/png" href="/favicon.png">
<style>
:root{
--bg:#0b1020;
--panel:#131a30;
--panel2:#1a2342;
--text:#eaf2ff;
--muted:#99a8c8;
--line:#2c3a66;
--accent:#27d3a5;
--blue:#5d8dff;
--radius:16px;
}

@font-face{
font-family:"Sora";
src:url("/fonts/Sora-Regular.woff2") format("woff2");
font-weight:400;
font-style:normal;
font-display:swap;
}

@font-face{
font-family:"Sora";
src:url("/fonts/Sora-Bold.woff2") format("woff2");
font-weight:700;
font-style:normal;
font-display:swap;
}

@font-face{
font-family:"JetBrains Mono";
src:url("/fonts/JetBrainsMono-SemiBold.woff2") format("woff2");
font-weight:600;
font-style:normal;
font-display:swap;
}

*{box-sizing:border-box}

body{
margin:0;
font-family:"Sora","Segoe UI",Tahoma,sans-serif;
background:
radial-gradient(70vw 70vw at 10% -20%, #263774 0%, transparent 60%),
radial-gradient(80vw 80vw at 110% 10%, #1b755d 0%, transparent 60%),
var(--bg);
color:var(--text);
min-height:100vh;
display:grid;
place-items:center;
padding:16px;
}

.box{
width:min(94vw,560px);
background:linear-gradient(160deg,var(--panel),var(--panel2));
border:1px solid rgba(149,170,220,.22);
border-radius:var(--radius);
padding:20px;
box-shadow:0 20px 50px rgba(0,0,0,.35);
animation:rise .35s ease both;
}

h1{
font-size:24px;
margin:0 0 8px;
letter-spacing:.3px;
}

p{
margin:0 0 14px;
color:var(--muted);
font-size:14px;
line-height:1.45;
}

input[type=file]{
width:100%;
padding:11px;
border-radius:12px;
border:1px solid var(--line);
background:#0e152a;
color:#dbe8ff;
outline:none;
}

input[type=file]:focus{
border-color:#65b2ff;
box-shadow:0 0 0 3px rgba(101,178,255,.2);
}

button{
margin-top:12px;
width:100%;
padding:12px;
border:none;
border-radius:12px;
background:linear-gradient(145deg,#6ca1ff,#4977ff);
color:#fff;
font-family:"Sora","Segoe UI",Tahoma,sans-serif;
font-weight:600;
font-size:15px;
cursor:pointer;
transition:transform .15s ease,filter .2s ease,box-shadow .2s ease;
box-shadow:0 10px 24px rgba(93,141,255,.24);
}

button:hover{filter:brightness(1.06)}
button:active{transform:translateY(1px) scale(.995)}

.progress{
margin-top:14px;
height:10px;
background:#101836;
border-radius:999px;
overflow:hidden;
border:1px solid var(--line);
}

.bar{
height:100%;
width:0%;
background:linear-gradient(90deg,#27d3a5,#5de0ff);
transition:width .2s ease;
}

.status{
margin-top:10px;
font-size:13px;
color:#b6c6ea;
min-height:20px;
font-family:"JetBrains Mono","Consolas","Courier New",monospace;
}

.nav{
display:flex;
justify-content:space-between;
align-items:center;
margin-top:14px;
gap:10px;
}

a{
color:#9ec4ff;
text-decoration:none;
font-size:14px;
}

a:hover{text-decoration:underline}

.badge{
font-size:12px;
color:#93ffd6;
background:rgba(39,211,165,.13);
border:1px solid rgba(39,211,165,.35);
padding:6px 10px;
border-radius:999px;
}

.footer{
margin-top:14px;
font-size:12px;
color:var(--muted);
text-align:center;
cursor:pointer;
user-select:none;
transition:color .2s ease, transform .15s ease;
}

.footer:hover{
color:#c6d5f7;
}

.footer:active{
transform:translateY(1px);
}

.toast{
position:fixed;
left:50%;
bottom:20px;
transform:translateX(-50%) translateY(20px);
background:rgba(10,16,34,.95);
color:var(--text);
border:1px solid rgba(149,170,220,.35);
border-radius:12px;
padding:12px 14px;
font-size:13px;
line-height:1.45;
width:min(92vw,420px);
box-shadow:0 14px 34px rgba(0,0,0,.35);
opacity:0;
pointer-events:none;
transition:opacity .25s ease, transform .25s ease;
z-index:20;
}

.toast.show{
opacity:1;
transform:translateX(-50%) translateY(0);
}

@keyframes rise{
from{opacity:0;transform:translateY(12px)}
to{opacity:1;transform:translateY(0)}
}
</style>
</head>
<body>
<div class="box">
<h1>Firmware Update</h1>
<p>Select a compiled <code>.bin</code> file and upload it securely over local Wi-Fi.</p>
<form id="fwForm">
<input type="file" id="fw" name="firmware" accept=".bin" required>
<button type="submit">Upload Firmware</button>
</form>
<div class="progress"><div class="bar" id="bar"></div></div>
<div class="status" id="status">Waiting for file...</div>
<div class="nav">
<a href="/">Back to Dashboard</a>
<span class="badge">Offline OTA</span>
</div>
<div class="footer" id="devFooter">Made with ❤️ by Lokesh</div>
</div>
<div class="toast" id="devToast">
<strong>Project: </strong>PowerMonitor<br>
<strong>Developer:</strong> Lokesh Gaurkar<br>
<strong>Email: </strong>Lokeshgaurkar444@gmail.com
</div>
<script>
const form=document.getElementById("fwForm")
const file=document.getElementById("fw")
const bar=document.getElementById("bar")
const statusEl=document.getElementById("status")
let toastTimer

function showDevToast(){
const t=document.getElementById("devToast")
if(!t) return
t.classList.add("show")
clearTimeout(toastTimer)
toastTimer=setTimeout(()=>{
t.classList.remove("show")
},3200)
}

form.addEventListener("submit",function(e){
e.preventDefault()
if(!file.files.length){statusEl.textContent="Please select a .bin file";return}
const data=new FormData()
data.append("firmware",file.files[0])
const xhr=new XMLHttpRequest()
xhr.open("POST","/updatefw",true)
xhr.upload.onprogress=function(ev){
if(ev.lengthComputable){
const pct=Math.round((ev.loaded/ev.total)*100)
bar.style.width=pct+"%"
statusEl.textContent="Uploading... "+pct+"%"
}
}
xhr.onload=function(){
if(xhr.status===200){
bar.style.width="100%"
statusEl.textContent="Update successful. Device restarting..."
}else{
statusEl.textContent="Update failed: "+xhr.responseText
}
}
xhr.onerror=function(){statusEl.textContent="Network error during upload"}
xhr.send(data)
})
document.getElementById("devFooter").addEventListener("click",showDevToast)
</script>
</body>
</html>
)=====";


void handleRoot() {
  server.send_P(200, "text/html", webpage);
}
void handleUpdatePage() {
  server.send_P(200, "text/html", updatePage);
}

void handleReset() {
  float re = pzem.energy();
  if (!isnan(re) && re >= 0) {
    energy_offset_kwh = re;
  }
  energy_kwh = 0;
  EEPROM.put(ENERGY_ADDR, energy_kwh);
  EEPROM.put(ENERGY_OFFSET_ADDR, energy_offset_kwh);
  EEPROM.commit();
  server.send(200, "text/plain", "reset");
}

void handleTariff() {

  tariff.meter = server.arg("meter").toFloat();
  tariff.ship = server.arg("ship").toFloat();
  tariff.tax = server.arg("tax").toFloat();
  tariff.r1 = server.arg("r1").toFloat();
  tariff.r2 = server.arg("r2").toFloat();
  tariff.r3 = server.arg("r3").toFloat();

  EEPROM.put(TARIFF_ADDR, tariff);
  EEPROM.commit();

  server.send(200, "text/plain", "saved");
}

void handleData() {
  updateMeasurements();

  String json = "{";

  json += "\"v\":" + String(voltage, 1) + ",";
  json += "\"c\":" + String(current, 3) + ",";
  json += "\"p\":" + String(power, 1) + ",";
  json += "\"e\":" + String(energy_kwh, 5) + ",";
  json += "\"f\":" + String(freq, 1) + ",";
  json += "\"pf\":" + String(pf, 2) + ",";

  json += "\"meter\":" + String(tariff.meter) + ",";
  json += "\"ship\":" + String(tariff.ship) + ",";
  json += "\"tax\":" + String(tariff.tax) + ",";
  json += "\"r1\":" + String(tariff.r1) + ",";
  json += "\"r2\":" + String(tariff.r2) + ",";
  json += "\"r3\":" + String(tariff.r3) + ",";

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t heapTotal = (heapBootBytes > 0) ? heapBootBytes : freeHeap;
  float ramUsedPct = 0;
  if (heapTotal > 0 && freeHeap <= heapTotal) {
    ramUsedPct = (100.0f * (float)(heapTotal - freeHeap)) / (float)heapTotal;
  }
  if (ramUsedPct < 0) ramUsedPct = 0;
  if (ramUsedPct > 100) ramUsedPct = 100;

  json += "\"cpu\":" + String(cpuLoadPct, 1) + ",";
  json += "\"ramu\":" + String(ramUsedPct, 1) + ",";
  json += "\"ramf\":" + String(freeHeap);

  json += "}";

  server.send(200, "application/json", json);
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    WiFiUDP::stopAll();
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.println("OTA update success");
    } else {
      Update.printError(Serial);
    }
  }
  yield();
}

void setup() {

  Serial.begin(9600);

  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(ENERGY_ADDR, energy_kwh);
  EEPROM.get(TARIFF_ADDR, tariff);
  EEPROM.get(ENERGY_OFFSET_ADDR, energy_offset_kwh);

  if (isnan(energy_kwh) || energy_kwh < 0) energy_kwh = 0;
  if (isnan(energy_offset_kwh) || energy_offset_kwh < 0) energy_offset_kwh = 0;

  heapBootBytes = ESP.getFreeHeap();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  MDNS.begin(hostname);
  LittleFS.begin();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/reset", handleReset);
  server.on("/tariff", handleTariff);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on(
    "/updatefw", HTTP_POST, []() {
      bool ok = !Update.hasError();
      server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
      delay(250);
      if (ok) {
        ESP.restart();
      }
    },
    handleUpdateUpload);
  server.serveStatic("/fonts/", LittleFS, "/fonts/");
  server.serveStatic("/favicon.png", LittleFS, "/favicon.png");
  server.serveStatic("/favicon.ico", LittleFS, "/favicon.png");

  server.begin();

  lastReadMs = 0;
}

void loop() {
  unsigned long loopStartUs = micros();

  updateMeasurements();
  server.handleClient();
  MDNS.update();
  yield();

  updatePerfStats(loopStartUs);
}

