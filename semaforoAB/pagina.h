// =====================================================================
//  pagina.h - PÁGINA WEB DEL SISTEMA (HTML + CSS + JavaScript)
//
//  Va en un archivo aparte para que semaforoAB.ino quede solo con la lógica.
//  Debe estar en la MISMA carpeta que semaforoAB.ino.
//
//  Estructura de la página:
//    <style>   estilos: tema de cada pestaña, tarjetas, semáforos, botón
//    <body>    encabezado, pestañas, tarjeta del cruce, tarjeta de distancia,
//              banner con la ilustración y pie de página
//    <script>  pestañas, consulta periódica a /estado, dibujo de las luces
//              y envío de la solicitud a /prioridad
//
//  Nota: dentro del texto de la página no se ponen comentarios línea a línea,
//  porque todo lo que se escribe aquí viaja por Wi-Fi hasta el celular.
// =====================================================================
#pragma once                                        // evita incluir este archivo dos veces
#include <Arduino.h>                                // define PROGMEM

const char PAGINA[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vía prioritaria</title>
<style>
:root{--bd:#e1e6ec;--mu:#6b7682;--tx:#14212b}
body{--ac:#0b7d70;--sf:#e4f3ef;--bg:#f2f8f6}
body[data-t=pol]{--ac:#2457b8;--sf:#e7eefb;--bg:#f2f6fc}
body[data-t=bom]{--ac:#b5401f;--sf:#fdebe4;--bg:#fcf6f2}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--bg);color:var(--tx);transition:background .3s}
.wrap{max-width:920px;margin:0 auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:center}
.brand{display:flex;align-items:center;gap:10px;font-size:12px;font-weight:700;letter-spacing:.5px}
.logo{background:#1b2630;border-radius:99px;padding:6px 9px;display:flex;gap:4px}
.logo i{width:7px;height:7px;border-radius:50%;display:block}
.logo i:nth-child(1){background:#ff5a4f}.logo i:nth-child(2){background:#ffc83d}.logo i:nth-child(3){background:#34c759}
.conn{background:#fff;border-radius:99px;padding:6px 12px;font-size:12px;display:flex;align-items:center;gap:7px}
.conn:before{content:"";width:7px;height:7px;border-radius:50%;background:#e5484d}
.conn.ok:before{background:#1f9d55}
h1{font-size:clamp(26px,5.5vw,42px);line-height:1.1;margin:30px 0 10px;max-width:640px}
.sub{color:var(--mu);margin:0}
.tabs{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:22px 0 16px}
.tab{text-align:left;padding:13px 15px;border:1px solid var(--bd);border-radius:12px;background:#fff;cursor:pointer;font:inherit;color:var(--tx);transition:background .2s}
.tab b{display:block;font-size:15px;margin-bottom:3px}
.tab small{color:var(--mu);font-size:11px}
.tab.on{background:var(--ac);border-color:var(--ac);color:#fff}
.tab.on small{color:rgba(255,255,255,.85)}
.grid{display:grid;grid-template-columns:1.55fr 1fr;gap:16px}
.card{background:#fff;border:1px solid var(--bd);border-radius:16px;padding:20px}
.ch{display:flex;justify-content:space-between;align-items:center}
.ch h2{font-size:17px;margin:0}
.badge{font-size:11px;background:#eef1f4;color:var(--mu);padding:5px 11px;border-radius:99px}
.badge.prio{background:var(--sf);color:var(--ac);font-weight:700}
.luces{display:flex;justify-content:space-around;margin:20px 0 14px}
.col{text-align:center;font-size:16px}
.col small{display:block;font-size:13px;margin-top:6px}
.sem{background:#252a31;border-radius:14px;padding:10px 12px;width:58px;margin:0 auto 12px}
.l{width:34px;height:34px;border-radius:50%;background:#3b4149;margin:8px auto;transition:background .15s,box-shadow .15s}
.r.on{background:#ff3b30;box-shadow:0 0 16px 3px rgba(255,59,48,.6)}
.y.on{background:#ffcc00;box-shadow:0 0 16px 3px rgba(255,204,0,.6)}
.g.on{background:#30d158;box-shadow:0 0 16px 3px rgba(48,209,88,.6)}
.aviso{background:var(--sf);color:var(--ac);border-radius:8px;padding:11px 14px;font-size:13px;transition:background .3s}
.k{font-size:11px;letter-spacing:.6px;color:var(--mu);font-weight:600}
.big{font-size:46px;font-weight:600;margin:24px 0 8px}
.big small{font-size:16px;color:var(--mu);font-weight:400}
.dt{font-size:14px;margin:0 0 6px}
.dh{font-size:12px;color:var(--mu);margin:0 0 22px;min-height:30px}
button.sol{width:100%;border:0;border-radius:10px;padding:16px;background:var(--ac);color:#fff;font:inherit;font-size:16px;font-weight:700;cursor:pointer;transition:background .3s,transform .1s}
button.sol:active:not(:disabled){transform:scale(.98)}
button.sol:disabled{background:#c9d0d7;color:#6f7a85;cursor:not-allowed}
.nt{text-align:center;font-size:12px;color:var(--mu);margin:12px 0 0}
.banner{display:flex;align-items:center;justify-content:space-between;gap:12px;background:var(--sf);border-radius:16px;padding:22px 26px;margin-top:16px;transition:background .3s}
.banner .t{font-size:10px;font-weight:700;letter-spacing:.6px;color:var(--ac)}
.banner h3{font-size:22px;margin:6px 0}
.banner p{margin:0;color:var(--mu);font-size:14px}
.v{display:none;width:190px;flex:none}
body[data-t=amb] .v1,body[data-t=pol] .v2,body[data-t=bom] .v3{display:block}
footer{display:flex;justify-content:space-between;gap:10px;font-size:11px;color:var(--mu);margin-top:16px;flex-wrap:wrap}
@media(max-width:720px){.grid{grid-template-columns:1fr}.v{width:120px}.banner h3{font-size:18px}}
@media(max-width:480px){.tab small{display:none}.tab{padding:12px}.tab b{margin:0;font-size:14px}.luces{margin-top:14px}}
</style></head><body data-t="amb">
<svg width="0" height="0" style="position:absolute"><symbol id="wh" viewBox="0 0 24 24"><circle cx="12" cy="12" r="11" fill="#26313b"/><circle cx="12" cy="12" r="5" fill="#cbd3da"/></symbol></svg>
<div class="wrap">
<header><div class="brand"><span class="logo"><i></i><i></i><i></i></span>VÍA PRIORITARIA</div><div class="conn" id="cn"><span id="ct">Conectando</span></div></header>
<h1>Semáforos inteligentes con prioridad para vehículos de emergencia.</h1>
<p class="sub">Selecciona el servicio de emergencia y gestiona tu paso por el próximo cruce.</p>

<div class="tabs">
<button class="tab on" data-k="amb"><b>Ambulancia</b><small>Emergencia médica</small></button>
<button class="tab" data-k="pol"><b>Policía</b><small>Seguridad y respuesta</small></button>
<button class="tab" data-k="bom"><b>Bomberos</b><small>Rescate y extinción</small></button>
</div>

<div class="grid">
<div class="card">
<div class="ch"><h2>Próximo cruce</h2><span class="badge" id="bg">Modo normal</span></div>
<div class="luces">
<div class="col"><div class="sem" id="sa"><div class="l r"></div><div class="l y"></div><div class="l g"></div></div>Semáforo A<small id="va">vía de la ambulancia</small></div>
<div class="col"><div class="sem" id="sb"><div class="l r"></div><div class="l y"></div><div class="l g"></div></div>Semáforo B<small>vía transversal</small></div>
</div>
<div class="aviso" id="av">Conectando con el semáforo…</div>
</div>
<div class="card">
<div class="k">DISTANCIA AL SEMÁFORO</div>
<div class="big"><span id="d">—</span> <small>m</small></div>
<p class="dt" id="dt">Esperando ubicación del vehículo</p>
<p class="dh" id="dh">La distancia se actualizará cuando se reciba la ubicación.</p>
<button class="sol" id="b" disabled>Solicitar prioridad</button>
<p class="nt" id="nt" role="status">Sin solicitudes pendientes</p>
</div>
</div>

<div class="banner">
<div><div class="t" id="bt">ATENCIÓN MÉDICA DE EMERGENCIA</div><h3 id="bh">Cada segundo cuenta.</h3><p id="bs">Facilita el paso de quienes cuidan la vida.</p></div>

<svg class="v v1" viewBox="0 0 180 80"><ellipse cx="86" cy="72" rx="80" ry="4" fill="#00000012"/><rect x="8" y="16" width="110" height="40" rx="6" fill="#fff" stroke="#d5dde3"/><path d="M118 26h28l14 14v16h-42z" fill="#fff" stroke="#d5dde3"/><path d="M126 30h16l9 10h-25z" fill="#9fc7dc"/><rect x="8" y="40" width="152" height="7" fill="#ef6f65"/><rect x="60" y="18" width="8" height="18" fill="#e5484d"/><rect x="55" y="23" width="18" height="8" fill="#e5484d"/><rect x="130" y="11" width="12" height="6" rx="2" fill="#e5484d"/><use href="#wh" x="28" y="47" width="24" height="24"/><use href="#wh" x="120" y="47" width="24" height="24"/></svg>

<svg class="v v2" viewBox="0 0 150 80"><ellipse cx="73" cy="72" rx="68" ry="4" fill="#00000012"/><path d="M12 54v-8q0-5 6-6l14-3 14-12q3-3 8-3h36q5 0 8 3l16 13 14 3q6 1 6 6v7z" fill="#fff" stroke="#d5dde3"/><path d="M48 28l-9 10h27V25h-14q-3 0-4 3z" fill="#a9d0e6"/><path d="M72 25v13h32L92 26q-1-1-3-1z" fill="#a9d0e6"/><rect x="12" y="43" width="122" height="6" fill="#2457b8"/><rect x="62" y="17" width="16" height="5" rx="2" fill="#e5484d"/><rect x="78" y="17" width="16" height="5" rx="2" fill="#2f6df6"/><use href="#wh" x="28" y="44" width="24" height="24"/><use href="#wh" x="96" y="44" width="24" height="24"/></svg>

<svg class="v v3" viewBox="0 0 160 80"><ellipse cx="78" cy="72" rx="74" ry="4" fill="#00000012"/><path d="M14 16h92M14 21h92M24 16v5M36 16v5M48 16v5M60 16v5M72 16v5M84 16v5M96 16v5" stroke="#cdd3d9" stroke-width="2"/><rect x="8" y="24" width="100" height="34" rx="4" fill="#e5543f"/><path d="M108 32h22l14 14v12h-36z" fill="#e5543f"/><path d="M114 36h12l8 9h-20z" fill="#cfe6f2"/><rect x="8" y="46" width="136" height="4" fill="#fff"/><rect x="16" y="30" width="18" height="12" rx="2" fill="#f27a67"/><rect x="38" y="30" width="18" height="12" rx="2" fill="#f27a67"/><rect x="60" y="30" width="18" height="12" rx="2" fill="#f27a67"/><rect x="82" y="30" width="18" height="12" rx="2" fill="#f27a67"/><rect x="116" y="26" width="10" height="5" rx="2" fill="#f5a524"/><use href="#wh" x="26" y="47" width="24" height="24"/><use href="#wh" x="110" y="47" width="24" height="24"/></svg>
</div>

<footer><span>Comprueba la señalización y que el cruce esté despejado antes de avanzar.</span><span>Fotomulta: <b id="fm">activa</b> &middot; Eventos: <b id="ev">0</b></span></footer>
</div>

<script>
const $=id=>document.getElementById(id);
const T={
 amb:{v:'la ambulancia',t:'ATENCIÓN MÉDICA DE EMERGENCIA',h:'Cada segundo cuenta.',s:'Facilita el paso de quienes cuidan la vida.'},
 pol:{v:'la policía',t:'SEGURIDAD Y RESPUESTA',h:'Respuesta rápida, cruce seguro.',s:'Prioridad para llegar donde más se necesita.'},
 bom:{v:'bomberos',t:'RESCATE Y EXTINCIÓN',h:'Abre paso a quienes protegen.',s:'Un camino despejado para atender la emergencia.'}};
let aviso='Tu vía está en rojo. Solicita prioridad antes de avanzar.',nota='',notaHasta=0;
function setTab(k){
 document.body.dataset.t=k;
 document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('on',b.dataset.k==k));
 const c=T[k];
 $('va').textContent='vía de '+c.v;$('bt').textContent=c.t;$('bh').textContent=c.h;$('bs').textContent=c.s;
}
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>setTab(b.dataset.k));
function pinta(id,c){const l=$(id).children;
 l[0].className='l r'+(c=='R'?' on':'');
 l[1].className='l y'+(c=='Y'?' on':'');
 l[2].className='l g'+(c=='G'?' on':'');}
let ref=null,tm;
function luces(s,t){
 const k=Math.floor(t/300),on=k%2==1,ti=k%2==0;
 switch(s){
  case 0:return['G','R'];
  case 10:return[ti?'G':'N','R'];
  case 1:return[on?'G':'N','R'];
  case 2:case 11:return['Y','R'];
  case 4:return['R','G'];
  case 5:return['R',on?'G':'N'];
  case 6:case 8:return['R','Y'];
  default:return['R','R'];}
}
function dibuja(){
 if(!ref)return;
 const L=luces(ref.s,ref.t+performance.now()-ref.at),a=L[0];
 pinta('sa',a);pinta('sb',L[1]);
 if(ref.prio)aviso=ref.est+' · '+ref.rest+' s restantes.';
 else if(a=='R')aviso='Tu vía está en rojo. Solicita prioridad antes de avanzar.';
 else if(a=='G')aviso='Tu vía está en verde. Avanza con precaución.';
 else if(a=='Y')aviso='Tu vía está en amarillo. Prepárate para detenerte.';
 $('av').textContent=aviso;
}
let ocupado=false,prioridadPendiente=false,fallos=0;
async function consultar(ruta,method){
 const ac=new AbortController(),to=setTimeout(()=>ac.abort(),2500);
 try{
  const respuesta=await fetch(ruta,{method,signal:ac.signal,cache:'no-store'});
  return {ok:respuesta.ok,texto:await respuesta.text()};
 }finally{clearTimeout(to);}
}
async function actualizarEstado(){
 const t0=performance.now();
 try{
  const respuesta=await consultar('/estado','GET');
  if(!respuesta.ok)throw new Error('Estado no disponible');
  const r=JSON.parse(respuesta.texto);
  ref={s:r.s,t:r.t,at:(t0+performance.now())/2,prio:r.prio,est:r.est,rest:r.rest};
  $('cn').className='conn ok';$('ct').textContent='Sistema conectado';
  $('bg').textContent=r.prio?'Prioridad activa · '+r.rest+' s':'Modo normal';
  $('bg').className='badge'+(r.prio?' prio':'');
  const loc=r.rssi>-99;
  $('d').textContent=loc?r.dist.toFixed(1):'—';
  $('dt').textContent=loc?'Distancia aproximada ('+r.rssi+' dBm)':'Esperando ubicación del vehículo';
  $('dh').textContent=!loc?'La distancia se actualizará cuando se reciba la ubicación.':(r.v?'Tu vía ya tiene verde: no necesitas solicitar prioridad.':(r.hab?'Estás en rango: puedes solicitar prioridad.':'Acércate al semáforo para habilitar el botón.'));
  if(!prioridadPendiente){
   $('b').disabled=!(r.hab&&!r.prio&&!r.v);
   $('b').textContent=r.prio?'Prioridad en curso':(r.v?'Ya tienes verde':'Solicitar prioridad');
   $('nt').textContent=r.prio?'Prioridad en curso · '+r.rest+' s':(Date.now()<notaHasta?nota:'Sin solicitudes pendientes');
  }
  $('fm').textContent=r.prio?'suspendida':'activa';
  $('ev').textContent=r.ev;
  dibuja();
  fallos=0;
 }catch(e){
  fallos++;ref=null;
  $('cn').className='conn';$('ct').textContent=fallos<2?'Verificando conexión':'Sin conexión';
  $('d').textContent='—';$('b').disabled=true;
  $('bg').className='badge';$('bg').textContent='Sin datos actuales';
  $('dt').textContent='Distancia no disponible';$('dh').textContent='';
  $('fm').textContent='—';pinta('sa','N');pinta('sb','N');
  $('av').textContent='Esperando el estado del semáforo…';
  if(!prioridadPendiente){
   $('b').textContent='Solicitar prioridad';
   $('nt').textContent=Date.now()<notaHasta?nota:'Esperando confirmación del estado';
  }
 }
}
async function atenderWeb(){
 if(ocupado)return;
 ocupado=true;clearTimeout(tm);
 try{
  if(prioridadPendiente){
   try{
    const r=await consultar('/prioridad','POST');
    nota=r.ok?'Solicitud aceptada':'Solicitud rechazada: '+r.texto;
   }catch(e){nota='No se recibió confirmación de la solicitud';}
   notaHasta=Date.now()+4000;$('nt').textContent=nota;
   prioridadPendiente=false;
  }
  await actualizarEstado();
 }finally{
  ocupado=false;
  tm=setTimeout(atenderWeb,prioridadPendiente?0:(fallos?500:300));
 }
}
$('b').onclick=()=>{
 if($('b').disabled||prioridadPendiente)return;
 prioridadPendiente=true;$('b').disabled=true;
 $('b').textContent='Enviando…';$('nt').textContent='Enviando solicitud…';
 atenderWeb();
};
setInterval(dibuja,100);atenderWeb();
</script></body></html>
)rawliteral";
