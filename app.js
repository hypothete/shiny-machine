const MIN_SHINY_TIME = 1080;
const MAX_SHINY_TIME = 1280;

let minVal = 30000;
let maxVal = 0;
let maxValTally = 10;
let resetCount = 0;
let lastMeasuredLoop = 0;
let isHunting = true;
let huntmode = "ambush";

const loading = document.getElementById("loading");
const loaded = document.getElementById("loaded");
const can = document.querySelector("canvas");
const ctx = can.getContext("2d");

const version = document.getElementById("version");
const resetHeader = document.getElementById("reset-count");
const loopHeader = document.getElementById("last-loop");
const huntSelect = document.getElementById("hunt-mode");
const resetForm = document.getElementById("reset-form");
const continueForm = document.getElementById("continue-form");

const rootURL = "http://192.168.1.47";

ctx.strokeStyle = "black";
ctx.fillStyle = "rgba(0, 192, 255, 0.2)";
const w = can.width;
const h = can.height;
const margin = 20;

init();

function init() {
  loading.style.transition = '0.25s ease-out opacity';
  loaded.style.transition = '0.25s ease-in opacity';
  loading.style.opacity = 1.0;
  loaded.style.opacity = 0.0;
  getJson()
    .then(data => {
      version.textContent = data.version || "???";
      loading.style.opacity = 0.0;
      loaded.style.opacity = 1.0;
      // update bounds
      data.values.forEach(tuple => {
        if (tuple[0] < minVal) {
          minVal = tuple[0];
        }
        if (tuple[0] > maxVal) {
          maxVal = tuple[0];
        }
        if (tuple[1] > maxValTally) {
          maxValTally = tuple[1] * 2;
        }
      });
      resetCount = data.resetCount;
      lastMeasuredLoop = data.lastMeasuredLoop;
      isHunting = data.isHunting;
      huntmode = data.huntmode;

      // update DOM
      resetHeader.textContent = `Reset #${resetCount}, ${
        isHunting ? "still hunting" : "found shiny!"
      }`;
      loopHeader.textContent = `Last measured loop took ${lastMeasuredLoop} ms`;
      huntSelect.value = huntmode;
      resetForm.setAttribute('action', rootURL + '/reset');
      continueForm.setAttribute('action', rootURL + '/continue');
      // update canvas
      drawGraph(data.values);
    })
    .catch(err => {
      throw err;
    });
}

async function getJson() {
  const response = await fetch(rootURL);
  return await response.json();
}

// dlLink.setAttribute('href', 'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify({ values: vals, tallies: valTallies })));

function drawGraph(data) {
  // build scaling functions
  const scaleX = item => {
    return (
      ((w - margin) * (item - minVal + 100)) /
        (maxVal + MAX_SHINY_TIME - minVal + 100) +
      margin
    );
  };
  const scaleY = item => {
    return (
      -(h - margin) * (Math.log(item + 1) / Math.log(maxValTally)) + h - margin
    );
  };

  // draw axes
  ctx.beginPath();
  ctx.moveTo(margin, 0);
  ctx.lineTo(margin, h - margin);
  ctx.lineTo(w, h - margin);

  // draw x-axis labels
  for (let i = 0; i <= 10; i++) {
    const tickVal = minVal + (i * (maxVal + MAX_SHINY_TIME - minVal)) / 10;
    const tx = scaleX(tickVal);
    ctx.moveTo(tx, h - margin);
    ctx.lineTo(tx, h - margin + 10);
    ctx.strokeText(tickVal, tx + 1, h);
  }

  // draw y-axis labels
  const mlt = Math.floor(Math.log(maxValTally));
  for (let i = 0; i <= mlt; i++) {
    const tickVal = Math.pow(mlt, i);
    const tx = scaleY(tickVal);
    ctx.moveTo(margin - 10, tx);
    ctx.lineTo(margin, tx);
    ctx.strokeText(tickVal, 1, tx + 10);
  }

  // draw bars for values
  data.forEach((tuple, index) => {
    const scaledVal = scaleX(tuple[0]);
    const scaledTal = scaleY(tuple[1]);
    const scaledMinTal = scaleY(0);
    const shinyStart = scaleX(tuple[0] + MIN_SHINY_TIME);
    const shinyEnd = scaleX(tuple[0] + MAX_SHINY_TIME);
    ctx.moveTo(scaledVal, scaledMinTal);
    ctx.lineTo(scaledVal, scaledTal);
    ctx.fillRect(
      shinyStart,
      scaledTal,
      shinyEnd - shinyStart,
      scaledMinTal - scaledTal
    );
  });
  ctx.stroke();
}
