const MIN_SHINY_TIME = 1080;
const MAX_SHINY_TIME = 1280;

let vals = [];
let valTallies = [];
let minVal = 30000;
let maxVal = 0;
let maxValTally = 2000;

const margin = 20;
const can = document.querySelector('canvas');
const ctx = can.getContext('2d');

ctx.strokeStyle = 'black';
ctx.fillStyle = 'rgba(0, 192, 255, 0.2)';
const w = can.width;
const h = can.height;

const scaleX = item => {
  return (w - margin) * (item - minVal)/((maxVal + MAX_SHINY_TIME) - minVal) + margin;
};
const scaleY = item => {
  return -(h - margin) * (Math.log(item + 1) / Math.log(maxValTally)) + h - margin;
};

ctx.beginPath();
ctx.moveTo(margin, 0);
ctx.lineTo(margin, h - margin);
ctx.lineTo(w, h - margin);
for(let i=0; i<=10; i++) {
  const tickVal = minVal + (i * (maxVal + MAX_SHINY_TIME - minVal)/10);
  const tx = scaleX(tickVal);
  ctx.moveTo(tx, h - margin);
  ctx.lineTo(tx, h - margin + 10);
  ctx.strokeText(tickVal,tx + 1, h);
}
const mlt = Math.floor(Math.log(maxValTally));
for(let i=0; i<=mlt; i++){
  const tickVal = Math.pow(mlt, i);
  const tx = scaleY(tickVal);
  ctx.moveTo(margin - 10, tx);
  ctx.lineTo(margin, tx);
  ctx.strokeText(tickVal, 1, tx + 10);
}
vals.forEach((val, index) => {
  const valTal = valTallies[index];
  const scaledVal = scaleX(val);
  const scaledTal = scaleY(valTal);
  const scaledMinTal = scaleY(0);
  const shinyStart = scaleX(val + MIN_SHINY_TIME);
  const shinyEnd = scaleX(val + MAX_SHINY_TIME);
  ctx.moveTo(scaledVal, scaledMinTal);
  ctx.lineTo(scaledVal, scaledTal);
  ctx.fillRect(shinyStart, scaledTal, shinyEnd - shinyStart, scaledMinTal - scaledTal);
});
ctx.stroke();

const dlLink = document.querySelector('#dl-link');
dlLink.setAttribute('href', 'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify({ values: vals, tallies: valTallies })));