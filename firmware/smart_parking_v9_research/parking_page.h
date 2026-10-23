// עמוד האינטרנט שה-ESP32 מגיש - התמונה הגרפית של החניון. חייב להישאר באותה תיקייה של ה-.ino
// מגישים: Aseel Kurdy, Nayef Habiballah, Osama Abdulhalim
//
// >>> כאן קורה הציור. הקוד ב-<script> רץ בדפדפן של מי שפתח את העמוד, לא ב-ESP32. <<<
//   1. כל שנייה הדפדפן שואל את ה-ESP32: GET /state  (JSON שנבנה ב-buildStateJson בקובץ ה-.ino)
//   2. buildBoard() מצייר פעם אחת את הדגם עצמו במבט מלמעלה, בס"מ ובפרופורציות של הדגם האמיתי:
//      הכביש הראשי והכניסה, קומה 0 עם החניות לאורך הקיר הימני, הרמפה וה-U אל המשטח המוגבה של
//      קומה 1 (חניה 2.5x5 ס"מ, רכב 4x2 ס"מ - המידות מגיעות מ-geo ב-JSON)
//   3. draw() מעדכן בכל שינוי: מצב כל חניה (רכב חונה ירוק/אדום, רכב בדרך מקווקו, חסומה, פנויה),
//      המסלול לרכב הבא, רכב חדש שנוסע על המסלול אל החניה שלו, הסיכום, ורשימת הרכבים שמחכים
//   4. פקודת status ב-Serial מגדילה את "seq" ב-JSON. כשהמספר משתנה העמוד מהבהב ומרענן מיד.
//   5. לחיצה על חניה מציגה את המסלול אליה ואת המרחק שנמדד; כפתור 3D מרים את קומה 1 מעל קומה 0.
// מיקום הרכיבים בציור מחקה את תמונת הדגם (קבוע M למטה); המרחקים המוצגים הם המדודים מה-ESP32.
// הכול בתוך הקובץ (אין CDN, אין פונטים): רשת ה-WiFi של הלוח לא מחוברת לאינטרנט.

const char PARKING_PAGE[] PROGMEM = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Parking</title>
<style>
:root{--bg:#f3f4f6;--fg:#15181d;--mut:#586272;--card:#fff;--line:#d8dce2}
@media (prefers-color-scheme:dark){:root{--bg:#101316;--fg:#eceff3;--mut:#9aa5b4;--card:#1a1e23;--line:#2c333b}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
main{max-width:1180px;margin:0 auto;padding:16px}
header{display:flex;align-items:baseline;justify-content:space-between;gap:4px 16px;flex-wrap:wrap}
h1{margin:0;font-size:21px}
h1 small{font-size:13px;font-weight:400;color:var(--mut);margin-left:8px}
h2{margin:0;font-size:16px;display:flex;align-items:center;justify-content:space-between;gap:8px}
.live{font-size:13px;color:var(--mut);display:flex;align-items:center;gap:6px}
.dot{width:9px;height:9px;border-radius:50%;background:#9aa5b4}
.live.on .dot{background:#2fb26b}
.live.off .dot{background:#e5484d}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0 10px}
.chip{background:var(--card);border:1px solid var(--line);border-radius:999px;padding:4px 12px;font-size:13px;display:flex;align-items:center;gap:7px}
.chip b{font-size:15px}
.sw{width:10px;height:10px;border-radius:3px;display:inline-block}
.sw.free{border:2px solid #2fb26b;background:transparent}
.next{background:var(--card);border:1px solid var(--line);border-left:4px solid #3b9eff;border-radius:10px;padding:10px 14px;margin-bottom:14px;font-size:16px}
.next.full{border-left-color:#e5484d}
.board{display:grid;grid-template-columns:minmax(0,1fr) 270px;gap:14px;align-items:start}
.stagecard{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:10px;transition:opacity .2s}
.stagecard.stale{opacity:.45}
.stagecard.flash{animation:flash .9s ease-out}
.tools{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:0 2px 8px}
.tools button{font:inherit;font-size:13px;border:1px solid var(--line);background:var(--bg);color:var(--fg);border-radius:8px;padding:4px 10px;cursor:pointer}
.tools button[aria-pressed=true]{background:#3b9eff;border-color:#3b9eff;color:#fff}
.tools .hint{font-size:12px;color:var(--mut);margin-left:auto}
.stage{perspective:1600px;overflow:hidden;border-radius:10px}
#model{position:relative;width:100%;aspect-ratio:58/48;transform-style:preserve-3d;transition:transform .7s ease}
#model svg{position:absolute;inset:0;width:100%;height:100%;display:block;transition:transform .7s ease}
#model.v3d{transform:rotateX(48deg) rotateZ(-16deg) scale(.86) translateY(-4%)}
#model.v3d #lyDeck{transform:translateZ(var(--lift,40px))}
#model.v3d #lyRamp{transform:rotateX(calc(-1 * var(--tilt,20deg)))}
#lyRamp{transform-origin:50% calc(30.5 / 48 * 100%)}
#lyOver{pointer-events:none}
#model.v3d #lyOver{display:none}
.deckshadow{opacity:0;transition:opacity .7s}
#model.v3d .deckshadow{opacity:1}
.hit{fill:transparent;cursor:pointer}
.hit:hover{fill:rgba(59,158,255,.18)}
.sel{fill:none;stroke:#3b9eff;stroke-width:.22}
.wait{padding:40px 0;text-align:center;color:var(--mut)}
.aside{display:flex;flex-direction:column;gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}
.card .count{background:var(--bg);border:1px solid var(--line);border-radius:999px;font-size:13px;padding:0 9px;min-width:26px;text-align:center}
.card .hint{margin:4px 0 10px;font-size:12px;color:var(--mut)}
.wcar{display:grid;grid-template-columns:62px 1fr;gap:2px 10px;align-items:center;background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:8px 10px;margin-bottom:8px;cursor:pointer}
.wcar svg.mini{width:62px}
.wtxt{display:flex;flex-direction:column;line-height:1.3}
.wtxt span{font-size:13px}
.wtxt small{font-size:12px;color:var(--mut)}
.wcar .bar{grid-column:1/-1;height:5px;margin-top:6px;border-radius:3px;background:var(--line);overflow:hidden}
.wcar .bar i{display:block;height:100%;background:#f5a524}
.wcar.gone{opacity:.7}
.empty{margin:6px 0 0;font-size:13px;color:var(--mut);text-align:center}
#info{font-size:13px}
#info b{font-size:15px}
table.dims{width:100%;border-collapse:collapse;font-size:12.5px}
table.dims td{padding:3px 0;border-bottom:1px solid var(--line)}
table.dims td:last-child{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
.legend{display:flex;flex-wrap:wrap;gap:6px 18px;margin-top:14px;font-size:13px;color:var(--mut)}
.legend span{display:flex;align-items:center;gap:6px}
footer{margin-top:18px;font-size:12px;color:var(--mut);text-align:center}
.toast{position:fixed;left:50%;bottom:18px;transform:translate(-50%,20px);background:#15181d;color:#fff;border-radius:999px;padding:8px 18px;font-size:14px;opacity:0;pointer-events:none;transition:opacity .25s,transform .25s}
.toast.show{opacity:1;transform:translate(-50%,0)}
.pulse{animation:pulse 1.6s ease-in-out infinite}
.march{animation:march 1.2s linear infinite}
@keyframes pulse{50%{opacity:.3}}
@keyframes march{to{stroke-dashoffset:-1.2}}
@keyframes flash{0%{box-shadow:0 0 0 5px rgba(59,158,255,.8)}100%{box-shadow:0 0 0 5px rgba(59,158,255,0)}}
@media (max-width:860px){.board{grid-template-columns:1fr}.tools .hint{display:none}}
@media (prefers-reduced-motion:reduce){.pulse,.march,.stagecard.flash{animation:none}#model,#model svg{transition:none}}
</style>
</head>
<body>
<main>
<header>
  <h1>Smart Parking <small>live view of the 1:100 model</small></h1>
  <div class="live" id="live"><i class="dot"></i><span id="livetxt">connecting&hellip;</span></div>
</header>
<div class="chips" id="chips"></div>
<div class="next" id="next">Waiting for the parking board&hellip;</div>
<div class="board">
  <div class="stagecard" id="stagecard">
    <div class="tools">
      <button id="b2d" aria-pressed="true">2D</button>
      <button id="b3d" aria-pressed="false">3D</button>
      <button id="bdim" aria-pressed="true">Dimensions</button>
      <span class="hint">Tap a stall to see its route and distance</span>
    </div>
    <div class="stage"><div id="model"><div class="wait">Connecting to the parking board&hellip;</div></div></div>
  </div>
  <div class="aside">
    <section class="card" aria-label="Cars waiting to park">
      <h2>Waiting to park <span class="count" id="wcount">0</span></h2>
      <p class="hint">Cars that entered the lot and are driving to their assigned stall.</p>
      <div id="wlist"></div>
    </section>
    <section class="card" aria-label="Selected stall"><div id="info">Tap a stall on the model to see its route and its distance from the entrance.</div></section>
    <section class="card" aria-label="Model dimensions">
      <h2>Measured on the model</h2>
      <table class="dims" id="dims"></table>
    </section>
  </div>
</div>
<div class="legend">
  <span><i class="sw free"></i>free stall</span>
  <span><i class="sw" style="background:#3b9eff"></i>next car goes here (route dashed)</span>
  <span><i class="sw" style="background:#f5a524"></i>reserved, car on its way</span>
  <span><i class="sw" style="background:#2fb26b"></i>parked in the right stall</span>
  <span><i class="sw" style="background:#e5484d"></i>wrong stall</span>
  <span><i class="sw" style="background:#8b94a1"></i>blocked (object on the sensor)</span>
</div>
<footer>Smart Parking &middot; Aseel Kurdy &middot; Nayef Habiballah &middot; Osama Abdulhalim</footer>
</main>
<div class="toast" id="toast" role="status"></div>
<script>
const $=id=>document.getElementById(id);
const COL={ok:'#2fb26b',bad:'#e5484d',res:'#f5a524',blk:'#8b94a1',nxt:'#3b9eff',free:'#2fb26b'};
// Layout of the physical model, top view, in centimetres (traced from the photo of the model).
// Stall and car sizes come from the board (geo); these numbers only place things where they are on the model.
const M={w:58,h:48,
  road:{x1:26.5,x2:33.5,cx:30},            // main road and ramp, two-way
  entY:48, gateY:45, rampStartY:30.5, rampTopY:12,
  ut:{cx:24.5,cy:12,rIn:2,rOut:9},          // U-turn from the ramp onto the deck
  deck:{x1:4,x2:25,y1:12,y2:37},            // floor 1: raised deck on the left
  deckLane:{x1:15.5,x2:22.5,cx:19},
  f1:{xMouth:15.5,y0:14},                   // floor-1 stalls: open to the lane on their right, spot 7 at the top
  f0area:{x1:33.5,x2:54,y1:21,y2:48},       // floor 0: open area right of the road
  f0:{xMouth:49,y0:41},                     // floor-0 stalls along the right wall, spot 0 at the bottom
  f0LaneX:45.5, tiles:{x1:54,x2:56.5}, liftCm:8};
let geo={sw:2.5,sl:5,car:[4,2,2.5]}, unit='cm';
let S=null, built=false, lastKey='', lastSeq=null, prevWait=null, selected=-1, view3d=false, showDims=true, toastTimer=0;
view3d=new URLSearchParams(location.search).get('view')==='3d';     // http://192.168.4.1/?view=3d opens in 3D

// keeps a looping animation in phase when its element is re-created on every refresh
const phase=(cls,ms)=>`class="${cls}" style="animation-delay:-${Date.now()%ms}ms"`;
function svgEl(id,inner,extra){return `<svg id="${id}" viewBox="0 0 ${M.w} ${M.h}" ${extra||''}>${inner}</svg>`;}
function t(x,y,s,size,fill,anchor,extra){return `<text x="${x}" y="${y}" font-size="${size}" fill="${fill}" text-anchor="${anchor||'middle'}" ${extra||''}>${s}</text>`;}
function stall(p){                                   // rectangle of a stall on the drawing
  if(p.f===0) return {x:M.f0.xMouth, y:M.f0.y0-geo.sw*(p.k+1), w:geo.sl, h:geo.sw, dir:1};
  return {x:M.f1.xMouth-geo.sl, y:M.f1.y0+geo.sw*p.k, w:geo.sl, h:geo.sw, dir:-1};
}
function carAt(r){return {cx:r.dir>0? r.x+r.w-0.4-geo.car[0]/2 : r.x+0.4+geo.car[0]/2, cy:r.y+r.h/2};}
function arc(cx,cy,r,a0,a1,n){const p=[];for(let i=0;i<=n;i++){const a=a0+(a1-a0)*i/n;p.push([cx+r*Math.cos(a),cy-r*Math.sin(a)]);}return p;}
function route(i){                                  // driving path entrance -> inside stall i (drawing coordinates)
  const p=S.sp[i], r=stall(p), c=carAt(r);
  if(p.f===0) return [[M.road.cx,M.entY],[M.road.cx,43.5],[M.f0LaneX,43.5],[M.f0LaneX,c.cy],[c.cx,c.cy]];
  const rm=(M.ut.rIn+M.ut.rOut)/2;
  return [[M.road.cx,M.entY],[M.road.cx,M.rampStartY],[M.road.cx,M.rampTopY]].concat(arc(M.ut.cx,M.ut.cy,rm,0,Math.PI,18))
         .concat([[M.deckLane.cx,c.cy],[c.cx,c.cy]]);
}
const pts=a=>a.map(q=>q[0].toFixed(2)+','+q[1].toFixed(2)).join(' ');

// a car seen from above, length along x, nose toward +x (dir=-1 mirrors it). Sizes in cm.
function car(cx,cy,dir,o){
  const L=geo.car[0],W=geo.car[1],x=-L/2,y=-W/2;
  return `<g transform="translate(${cx.toFixed(2)} ${cy.toFixed(2)}) scale(${dir} 1)">`+
    `<rect x="${x}" y="${y}" width="${L}" height="${W}" rx="${(W*.3).toFixed(2)}" fill="${o.fill}" stroke="${o.stroke||'rgba(0,0,0,.45)'}" stroke-width="${o.dash?.12:.06}"${o.dash?' stroke-dasharray=".35 .25" '+phase('march',1200):''}/>`+
    `<rect x="${(L*.1).toFixed(2)}" y="${(-W*.36).toFixed(2)}" width="${(L*.17).toFixed(2)}" height="${(W*.72).toFixed(2)}" rx=".14" fill="${o.win}"/>`+
    `<rect x="${(-L*.38).toFixed(2)}" y="${(-W*.33).toFixed(2)}" width="${(L*.12).toFixed(2)}" height="${(W*.66).toFixed(2)}" rx=".12" fill="${o.win}"/>`+
    (o.dash?'':`<circle cx="${(L/2-.18).toFixed(2)}" cy="${(y+.32).toFixed(2)}" r=".14" fill="#ffe9a8"/><circle cx="${(L/2-.18).toFixed(2)}" cy="${(-y-.32).toFixed(2)}" r=".14" fill="#ffe9a8"/>`)+
    `</g>`;
}

// ---- the model itself: drawn once, in three layers so that the 3D view can lift floor 1 ----
function buildBoard(){
  const g=geo, R=M.road;
  const defs=`<defs>
    <pattern id="grass" width="1.4" height="1.4" patternUnits="userSpaceOnUse"><rect width="1.4" height="1.4" fill="#3d7434"/><path d="M.2 1.2l.12-.55M.6 1.3l-.08-.6M1 1.1l.14-.5M.4 .5l.1-.4M1.2 .6l-.1-.45" stroke="#5c9a48" stroke-width=".09" stroke-linecap="round"/></pattern>
    <pattern id="tiles" width="1.2" height="1.2" patternUnits="userSpaceOnUse"><rect width="1.2" height="1.2" fill="#b9a688"/><rect x=".05" y=".05" width=".5" height=".5" fill="#d8b98f"/><rect x=".65" y=".05" width=".5" height=".5" fill="#c98f6a"/><rect x=".05" y=".65" width=".5" height=".5" fill="#a9a58e"/><rect x=".65" y=".65" width=".5" height=".5" fill="#e0c9a6"/></pattern>
    <linearGradient id="rampg" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#17181b"/><stop offset="1" stop-color="#2a2c31"/></linearGradient>
  </defs>`;
  // base layer: board, grass, road, floor 0, entrance, sensors
  let b=defs+`<rect width="${M.w}" height="${M.h}" fill="url(#grass)"/>`;
  b+=`<rect class="deckshadow" x="${M.deck.x1+1.6}" y="${M.deck.y1+2}" width="${M.deck.x2-M.deck.x1}" height="${M.deck.y2-M.deck.y1}" rx=".6" fill="rgba(0,0,0,.45)"/>`;
  b+=`<rect x="${M.f0area.x1}" y="${M.f0area.y1}" width="${M.f0area.x2-M.f0area.x1}" height="${M.f0area.y2-M.f0area.y1}" fill="#1b1c1f"/>`;
  b+=`<rect x="${R.x1}" y="${M.rampStartY}" width="${R.x2-R.x1}" height="${M.entY-M.rampStartY}" fill="#1b1c1f"/>`;
  b+=`<line x1="${R.cx}" y1="${M.entY}" x2="${R.cx}" y2="${M.rampStartY}" stroke="#e9e9e2" stroke-width=".22" stroke-dasharray=".9 .7"/>`;
  b+=`<rect x="${M.tiles.x1}" y="${M.f0area.y1}" width="${M.tiles.x2-M.tiles.x1}" height="${M.f0area.y2-M.f0area.y1}" fill="url(#tiles)"/>`;
  b+=`<rect x="3" y="${M.deck.y2}" width="4" height="9" fill="#c9a57a" stroke="#9b7a50" stroke-width=".12"/><rect x="20" y="${M.deck.y2}" width="4" height="9" fill="#c9a57a" stroke="#9b7a50" stroke-width=".12"/>`;
  // floor-0 stall lines (white, like the painted lines) and stall numbers
  for(let k=0;k<=S.per;k++){const y=M.f0.y0-g.sw*k; b+=`<line x1="${M.f0.xMouth-.3}" y1="${y}" x2="${M.f0.xMouth+g.sl}" y2="${y}" stroke="#ecece6" stroke-width=".2"/>`;}
  b+=`<g id="dyn0"></g>`;
  // lane arrows on floor 0 and the entrance gate with the side-facing sensor
  b+=`<path d="M${M.f0LaneX} 36l-.9 1.6h1.8z M${M.f0LaneX} 28l-.9 1.6h1.8z" fill="rgba(255,255,255,.35)"/>`;
  b+=`<rect x="${R.x1-1}" y="${M.gateY-.5}" width="1" height="1.6" fill="#c9a57a"/><rect x="${R.x2}" y="${M.gateY-.5}" width="1" height="1.6" fill="#c9a57a"/>`;
  b+=`<rect x="${R.x1-.2}" y="${M.gateY-.15}" width="${R.x2-R.x1+.4}" height=".3" fill="#c9a57a" opacity=".8"/>`;
  b+=`<path d="M${R.x2+.2} ${M.gateY+.3} L${R.x1+.4} ${M.gateY-1.2} L${R.x1+.4} ${M.gateY+1.8}Z" fill="rgba(80,170,255,.13)" stroke="rgba(80,170,255,.5)" stroke-width=".06" stroke-dasharray=".3 .2"/>`;
  b+=`<rect x="${R.x2+.15}" y="${M.gateY-.25}" width=".9" height="1.1" rx=".15" fill="#1f5fa8" stroke="#9cc4ff" stroke-width=".06"/>`;
  b+=t(R.cx,M.entY-.6,'ENTRANCE',1,'#fff','middle','font-weight="700"');
  b+=t(R.x2+1.3,M.gateY+2.4,'entrance sensor',.7,'#bcdcff','start');
  // spot-0 sensor: post at the bottom-right corner, sensor head above spot 0
  const s0=stall({f:0,k:0});
  b+=`<rect x="${M.tiles.x1+.3}" y="${s0.y+s0.h+.3}" width="1.6" height="1.6" fill="#c9a57a" stroke="#9b7a50" stroke-width=".1"/>`;
  b+=`<rect x="${s0.x+s0.w-1.5}" y="${s0.y+s0.h/2-.45}" width="2.6" height=".9" rx=".15" fill="#1f5fa8" stroke="#9cc4ff" stroke-width=".06"/>`;
  b+=t(M.tiles.x1+1.1,s0.y+s0.h+3.2,'spot sensor',.7,'#bcdcff','middle');
  b+=t((M.f0area.x1+M.f0.xMouth)/2+1,M.f0area.y1+1.6,'FLOOR 0',1.15,'#fff','middle','font-weight="700"');
  b+=`<g id="dims0" class="dims"></g>`;

  // ramp layer: the inclined two-way section (lifted at its top end in 3D)
  let r=`<rect id="rampRect" x="${R.x1}" y="${M.rampTopY}" width="${R.x2-R.x1}" height="${M.rampStartY-M.rampTopY}" fill="url(#rampg)"/>`;
  r+=`<line x1="${R.cx}" y1="${M.rampStartY}" x2="${R.cx}" y2="${M.rampTopY}" stroke="#e9e9e2" stroke-width=".2" stroke-dasharray=".9 .7"/>`;
  r+=`<path d="M${R.cx-1.8} ${M.rampTopY+6}v5 M${R.cx-2.6} ${M.rampTopY+10}l.8 1.2.8-1.2 M${R.cx+1.8} ${M.rampTopY+11}v-5 M${R.cx+1} ${M.rampTopY+7.2}l.8-1.2.8 1.2" stroke="#f2f2ea" stroke-width=".3" fill="none" stroke-linecap="round"/>`;
  r+=t(R.cx,M.rampTopY+3.6,'RAMP',.95,'#d9d9d0','middle','font-weight="700"');
  r+=`<line x1="${R.x1}" y1="${M.rampStartY}" x2="${R.x2}" y2="${M.rampStartY}" stroke="#f5a524" stroke-width=".15" stroke-dasharray=".4 .3"/>`;

  // deck layer: floor 1 (raised deck with wooden rails), the U-turn, floor-1 stalls
  const U=M.ut, D=M.deck, L=M.deckLane;
  let d=`<path d="M${U.cx+U.rOut} ${U.cy} A${U.rOut} ${U.rOut} 0 0 0 ${U.cx-U.rOut} ${U.cy} L${U.cx-U.rIn} ${U.cy} A${U.rIn} ${U.rIn} 0 0 1 ${U.cx+U.rIn} ${U.cy}Z" fill="#1b1c1f"/>`;
  d+=`<path d="M${U.cx+(U.rIn+U.rOut)/2} ${U.cy} A${(U.rIn+U.rOut)/2} ${(U.rIn+U.rOut)/2} 0 0 0 ${U.cx-(U.rIn+U.rOut)/2} ${U.cy}" fill="none" stroke="#e9e9e2" stroke-width=".2" stroke-dasharray=".9 .7"/>`;
  d+=`<rect x="${D.x1}" y="${D.y1}" width="${D.x2-D.x1}" height="${D.y2-D.y1}" fill="#c9a57a"/>`;
  d+=`<rect x="${D.x1+1}" y="${D.y1}" width="${L.x2-D.x1-1}" height="${D.y2-D.y1-1}" fill="#1b1c1f"/>`;
  d+=`<rect x="${D.x1+1}" y="${D.y1}" width="${L.x1-D.x1-1}" height="1" fill="#c9a57a"/>`;
  d+=`<line x1="${L.cx}" y1="${D.y1}" x2="${L.cx}" y2="${D.y2-1}" stroke="#e9e9e2" stroke-width=".2" stroke-dasharray=".9 .7"/>`;
  for(let k=0;k<=S.per;k++){const y=M.f1.y0+g.sw*k; d+=`<line x1="${M.f1.xMouth-g.sl}" y1="${y}" x2="${M.f1.xMouth+.3}" y2="${y}" stroke="#ecece6" stroke-width=".2"/>`;}
  d+=`<path d="M${L.cx} 31.5l-.9-1.6h1.8z M${L.cx} 23.5l-.9-1.6h1.8z" fill="rgba(255,255,255,.35)"/>`;
  d+=t((D.x1+L.x1)/2,D.y2-2.2,'FLOOR 1',1.15,'#fff','middle','font-weight="700"');
  d+=t((D.x1+L.x1)/2,D.y2-1.2,'raised deck',.7,'#bdbdb5');
  d+=`<g id="dyn1"></g><g id="dims1" class="dims"></g>`;

  $('model').innerHTML=svgEl('lyBase',b,'role="img" aria-label="Floor 0, the main road and the entrance"')+
    svgEl('lyRamp',r,'aria-hidden="true"')+svgEl('lyDeck',d,'role="img" aria-label="Floor 1 on the raised deck"')+
    svgEl('lyOver','<g id="ovl"></g>','aria-hidden="true"');
  built=true; drawDims(); setView(view3d);
}

// measured lengths written on the drawing (from the board, so they always match what it uses)
function drawDims(){
  if(!built) return;
  const R=M.road, c='#ffd166', sz=.78;
  const line=(x1,y1,x2,y2)=>`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${c}" stroke-width=".08"/>`;
  let a='', b='';
  if(showDims){
    a+=line(R.x2+.8,M.entY-1.4,R.x2+.8,M.rampStartY)+t(R.x2+1.3,M.rampStartY+3.2,geo.er+' '+unit+' to the ramp',sz,c,'start');
    a+=t(R.x2+1.3,M.rampStartY-2.2,'ramp '+geo.ramp+' '+unit,sz,c,'start');
    a+=line(R.cx,43.5,M.f0LaneX,43.5)+t((R.cx+M.f0LaneX)/2,42.9,geo.e0+' '+unit+' to the first stall',sz,c);
    const top=stall({f:0,k:S.per-1});
    a+=line(top.x,top.y-.7,top.x+top.w,top.y-.7)+t(top.x+top.w/2,top.y-1,'stall '+geo.sl+' '+unit,sz,c);
    const f1=stall({f:1,k:0});
    b+=line(f1.x-.6,f1.y,f1.x-.6,f1.y+f1.h)+t(f1.x-1,f1.y+f1.h/2+.3,geo.sw+' '+unit,sz,c,'end');
    b+=t(M.deckLane.cx+.6,M.f1.y0-.4,geo.f1+' '+unit,sz,c,'start');
  }
  $('dims0').innerHTML=a; $('dims1').innerHTML=b;
}

function setView(v){
  view3d=v; $('b3d').setAttribute('aria-pressed',v); $('b2d').setAttribute('aria-pressed',!v);
  const m=$('model'); if(!m) return;
  m.classList.toggle('v3d',v);
  const pxPerCm=m.clientWidth/M.w, lift=M.liftCm*pxPerCm, run=M.rampStartY-M.rampTopY;
  m.style.setProperty('--lift',lift.toFixed(1)+'px');
  m.style.setProperty('--tilt',(Math.atan(M.liftCm/run)*180/Math.PI).toFixed(1)+'deg');
  const rr=$('rampRect');                 // in 3D the ramp is tilted, so it needs its true (longer) length
  if(rr){const len=v?Math.hypot(run,M.liftCm):run; rr.setAttribute('y',(M.rampStartY-len).toFixed(2)); rr.setAttribute('height',len.toFixed(2));}
}

// ---- state of each stall (redrawn when something changes) ----
function drawSpots(s){
  let o=['',''];
  s.sp.forEach((p,i)=>{
    const r=stall(p), c=carAt(r), isNext=(i===s.next&&p.s===0), L=p.f;
    let g=`<g><title>Spot ${i} - floor ${p.f} - ${p.d.toFixed(1)} ${unit} from the entrance${i===s.real?' - the real stall with the sensor and LED':''}</title>`;
    if(p.s===2) g+=`<rect x="${r.x}" y="${r.y}" width="${r.w}" height="${r.h}" fill="${p.ok?COL.ok:COL.bad}" opacity=".22"/>`;
    if(isNext) g+=`<rect x="${r.x+.15}" y="${r.y+.15}" width="${r.w-.3}" height="${r.h-.3}" rx=".3" fill="${COL.nxt}" fill-opacity=".25" stroke="${COL.nxt}" stroke-width=".18" ${phase('pulse',1600)}/>`;
    else if(p.s===0) g+=`<rect x="${r.x+.25}" y="${r.y+.25}" width="${r.w-.5}" height="${r.h-.5}" rx=".25" fill="none" stroke="${COL.free}" stroke-opacity=".75" stroke-width=".1" stroke-dasharray=".35 .25"/>`;
    if(p.s===1){ g+=car(c.cx,c.cy,r.dir,{fill:'rgba(245,165,36,.18)',stroke:COL.res,dash:1,win:'rgba(245,165,36,.4)'}); }
    else if(p.s===2){ g+=car(c.cx,c.cy,r.dir,{fill:p.ok?COL.ok:COL.bad,win:'rgba(0,0,0,.4)'}); }
    else if(p.s===3){ g+=`<rect x="${r.x+.5}" y="${r.y+.35}" width="${r.w-1}" height="${r.h-.7}" rx=".2" fill="${COL.blk}" fill-opacity=".55" stroke="#d3d8df" stroke-width=".08"/><path d="M${r.x+.7} ${r.y+.5}L${r.x+r.w-.7} ${r.y+r.h-.5}M${r.x+r.w-.7} ${r.y+.5}L${r.x+.7} ${r.y+r.h-.5}" stroke="#eef1f4" stroke-width=".12"/>`; }
    // label: stall number on the lane side; car id on the car
    const lx=r.dir>0? r.x-.7 : r.x+r.w+.7;
    g+=t(lx,r.y+r.h/2+.38,i,1.05,i===s.real?'#9cc4ff':'#fff',r.dir>0?'end':'start','font-weight="700"');
    if(p.s===1||p.s===2) g+=t(c.cx,c.cy+.33,'#'+p.car+(p.s===2?(p.ok?' ✓':' ✗'):''),.9,p.s===1?'#ffd27a':'#fff','middle','font-weight="700"');
    if(i===selected) g+=`<rect class="sel" x="${r.x}" y="${r.y}" width="${r.w}" height="${r.h}" rx=".2"/>`;
    g+=`<rect class="hit" data-i="${i}" x="${r.x-1.8}" y="${r.y}" width="${r.w+1.8}" height="${r.h}"/></g>`;
    o[L]+=g;
  });
  $('dyn0').innerHTML=o[0]; $('dyn1').innerHTML=o[1];
}

// routes: dashed blue for the next car, orange for the stall the user tapped; plus cars driving in
function drawRoutes(s){
  let h='';
  if(s.next>=0) h+=`<polyline points="${pts(route(s.next))}" fill="none" stroke="${COL.nxt}" stroke-width=".32" stroke-dasharray=".7 .5" stroke-linecap="round" stroke-linejoin="round" ${phase('march',1200)} opacity=".85"/>`;
  if(selected>=0) h+=`<polyline points="${pts(route(selected))}" fill="none" stroke="#ffb020" stroke-width=".42" stroke-linecap="round" stroke-linejoin="round" opacity=".9"/>`;
  h+=`<g id="movers"></g>`;
  $('ovl').innerHTML=h;
}

const movers=[];
function animateArrival(carId,spot){                    // a new car drives from the entrance to its stall
  const path=route(spot), seg=[]; let total=0;
  for(let i=1;i<path.length;i++){const l=Math.hypot(path[i][0]-path[i-1][0],path[i][1]-path[i-1][1]); seg.push(l); total+=l;}
  movers.push({carId,path,seg,total,t0:performance.now(),dur:Math.min(5000,900+total*45)});
  if(movers.length===1) requestAnimationFrame(stepMovers);
}
function stepMovers(now){
  const host=$('movers'); if(!host){movers.length=0;return;}
  let h='';
  for(let m=movers.length-1;m>=0;m--){
    const mv=movers[m], f=Math.min(1,(now-mv.t0)/mv.dur), e=f<.5?2*f*f:1-Math.pow(-2*f+2,2)/2;
    let d=e*mv.total, i=0; while(i<mv.seg.length-1&&d>mv.seg[i]){d-=mv.seg[i];i++;}
    const a=mv.path[i], b=mv.path[i+1], u=mv.seg[i]?Math.min(1,d/mv.seg[i]):1;
    const x=a[0]+(b[0]-a[0])*u, y=a[1]+(b[1]-a[1])*u, ang=Math.atan2(b[1]-a[1],b[0]-a[0])*180/Math.PI;
    h+=`<g transform="translate(${x.toFixed(2)} ${y.toFixed(2)}) rotate(${ang.toFixed(1)})">${car(0,0,1,{fill:'#f5a524',win:'rgba(0,0,0,.4)'})}</g>`+
       t(x,y-1.5,'#'+mv.carId,.9,'#ffd27a','middle','font-weight="700"');
    if(f>=1) movers.splice(m,1);
  }
  host.innerHTML=h;
  if(movers.length) requestAnimationFrame(stepMovers);
}

// ---- side cards ----
function miniCar(color,fill){
  const L=geo.car[0],W=geo.car[1];
  return `<svg class="mini" viewBox="${-L/2-.3} ${-W/2-.3} ${L+.6} ${W+.6}" aria-hidden="true">${car(0,0,1,{fill:fill,stroke:color,dash:1,win:'rgba(128,128,128,.35)'})}</svg>`;
}
function drawWaiting(s){
  const w=s.wait||[];
  $('wcount').textContent=s.waitN;
  if(!w.length){ $('wlist').innerHTML='<p class="empty">No cars waiting.<br>A new car shows up here until it parks.</p>'; return; }
  let h='';
  w.forEach(c=>{
    if(c.spot>=0){
      const p=s.sp[c.spot], pct=Math.max(0,Math.min(100,100*c.left/s.ttl));
      h+=`<div class="wcar" data-i="${c.spot}">${miniCar(COL.res,'rgba(245,165,36,.14)')}<div class="wtxt"><b>Car #${c.car}</b><span>go to <b>spot ${c.spot}</b> &middot; floor ${p.f}</span><small>${p.d.toFixed(1)} ${unit} &middot; ${c.left} s left</small></div><div class="bar"><i style="width:${pct}%"></i></div></div>`;
    }else{
      h+=`<div class="wcar gone">${miniCar(COL.blk,'rgba(139,148,161,.14)')}<div class="wtxt"><b>Car #${c.car}</b><span>no spot assigned</span><small>reservation expired</small></div></div>`;
    }
  });
  if(s.waitN>w.length) h+=`<p class="empty">+ ${s.waitN-w.length} more waiting</p>`;
  $('wlist').innerHTML=h;
}
const stateName=['free','reserved','occupied','blocked'];
function drawInfo(s){
  if(selected<0||selected>=s.sp.length){ $('info').innerHTML='Tap a stall on the model to see its route and its distance from the entrance.'; return; }
  const p=s.sp[selected];
  let who='';
  if(p.s===1) who=`<br>Reserved for car #${p.car} &middot; ${p.left} s left`;
  if(p.s===2) who=`<br>Car #${p.car} parked here &middot; ${p.ok?'right stall':'<b style="color:#e5484d">wrong stall</b>'}`;
  if(p.s===3) who='<br>The sensor sees an object but no car is registered';
  const via=p.f===0? `entrance &rarr; floor-0 lane (${s.geo.e0}) &rarr; ${p.k} &times; ${s.geo.sw} along the lane &rarr; ${s.geo.sl} into the stall`
                   : `entrance &rarr; ramp (${s.geo.er}) &rarr; ramp ${s.geo.ramp} &rarr; floor-1 lane (${s.geo.f1}) &rarr; ${p.k} &times; ${s.geo.sw} &rarr; ${s.geo.sl} into the stall`;
  $('info').innerHTML=`<b>Spot ${selected}</b> &middot; floor ${p.f}${selected===s.real?' &middot; real stall (sensor + LED)':''}<br>`+
    `<b>${p.d.toFixed(1)} ${unit}</b> from the entrance${unit==='cm'?' &middot; about '+p.d.toFixed(1)+' m at 1:100':''}<br>`+
    `<small style="color:var(--mut)">${via}</small><br>State: ${stateName[p.s]}${who}`;
}
function drawDimsTable(s){
  const g=s.geo, u=unit, r=(a,b)=>`<tr><td>${a}</td><td>${b}</td></tr>`;
  $('dims').innerHTML=r('Entrance &rarr; first floor-0 stall',g.e0+' '+u)+r('Entrance &rarr; start of the ramp',g.er+' '+u)+r('Ramp length',g.ramp+' '+u)+
    r('Floor-1 entrance &rarr; first stall',g.f1+' '+u)+r('Stall (width &times; length)',g.sw+' &times; '+g.sl+' '+u)+
    r('Car (L &times; W &times; H)',g.car.join(' &times; ')+' '+u)+
    r('Floor 0 stalls 0&ndash;6',s.sp[0].d.toFixed(1)+' &ndash; '+s.sp[s.per-1].d.toFixed(1)+' '+u)+
    r('Floor 1 stalls 7&ndash;13',s.sp[s.per].d.toFixed(1)+' &ndash; '+s.sp[s.sp.length-1].d.toFixed(1)+' '+u)+r('Scale','1 : 100');
}

const chip=(sw,txt,n)=>`<span class="chip">${sw}${txt} <b>${n}</b></span>`;
const dotSw=col=>`<i class="sw" style="background:${col}"></i>`;
function toast(m){const el=$('toast');el.textContent=m;el.classList.add('show');clearTimeout(toastTimer);toastTimer=setTimeout(()=>el.classList.remove('show'),2200);}

function draw(s){
  S=s; geo=s.geo||geo; unit=s.unit||unit;
  if(!built) buildBoard();
  if(lastSeq!==null&&s.seq>lastSeq){            // "status" typed in the Serial Monitor: refresh at once and flash
    lastKey=''; toast('Status refreshed');
    const sc=$('stagecard'); sc.classList.remove('flash'); void sc.offsetWidth; sc.classList.add('flash');
  }
  lastSeq=s.seq;
  const n=[0,0,0,0]; let wrong=0;
  s.sp.forEach(p=>{n[p.s]++; if(p.s===2&&!p.ok) wrong++;});
  $('chips').innerHTML=chip('<i class="sw free"></i>','free',n[0])+chip(dotSw(COL.res),'reserved',n[1])+chip(dotSw(COL.ok),'parked',n[2]-wrong)+
    (wrong?chip(dotSw(COL.bad),'wrong spot',wrong):'')+(n[3]?chip(dotSw(COL.blk),'blocked',n[3]):'');
  const nb=$('next');
  if(s.next>=0){ nb.className='next'; nb.innerHTML=`Next car &rarr; <b>spot ${s.next}</b> &middot; floor ${s.sp[s.next].f} &middot; ${s.nextd.toFixed(1)} ${unit} from the entrance`; }
  else { nb.className='next full'; nb.textContent='Lot full – the next car will be turned away'; }
  drawWaiting(s); drawInfo(s); drawDimsTable(s);
  const key=JSON.stringify([s.sp.map(p=>[p.s,p.car,p.ok]),s.next,selected]);
  if(key!==lastKey){ lastKey=key; drawSpots(s); drawRoutes(s); }
  else drawSpots(s);                              // countdowns only; routes untouched so animations keep running
  // cars that just entered drive from the entrance to their stall
  if(prevWait!==null){
    const before=new Set(prevWait.map(c=>c.car));
    (s.wait||[]).forEach(c=>{ if(c.spot>=0&&!before.has(c.car)) animateArrival(c.car,c.spot); });
  }
  prevWait=s.wait||[];
}

function pick(i){ selected=(selected===i)?-1:i; lastKey=''; if(S) draw(S); }
document.addEventListener('click',e=>{
  const h=e.target.closest('[data-i]'); if(h) pick(+h.getAttribute('data-i'));
});
$('b2d').onclick=()=>setView(false);
$('b3d').onclick=()=>setView(true);
$('bdim').onclick=()=>{showDims=!showDims; $('bdim').setAttribute('aria-pressed',showDims); drawDims();};
window.addEventListener('resize',()=>setView(view3d));

let busy=false;
async function poll(){
  if(busy) return; busy=true;
  const ctl=new AbortController(), to=setTimeout(()=>ctl.abort(),2500);
  try{
    const r=await fetch('/state',{cache:'no-store',signal:ctl.signal});
    const s=await r.json();
    draw(s);
    $('live').className='live on';
    $('livetxt').textContent='live · board up '+s.up+' s';
    $('stagecard').classList.remove('stale');
  }catch(e){
    $('live').className='live off';
    $('livetxt').textContent='connection lost – retrying';
    $('stagecard').classList.add('stale');
  }
  clearTimeout(to); busy=false;
}
poll(); setInterval(poll,1000);
</script>
</body>
</html>
)PAGE";
