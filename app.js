const MIN_SHINY_TIME = 1080;
const MAX_SHINY_TIME = 1280;

let values = [];
let minVal = 30000;
let maxVal = 0;
let maxValTally = 10;
let resetCount = 0;
let timeoutCount = 0;
let lastMeasuredLoop = 0;
let isHunting = true;
let huntmode = "ambush";
let useWindow = false;
let windowStart = 0;
let windowEnd = 30000;

const loading = document.getElementById("loading");
const loaded = document.getElementById("loaded");
const can = document.querySelector("canvas");
const ctx = can.getContext("2d");

const version = document.getElementById("version");
const resetHeader = document.getElementById("reset-count");
const loopHeader = document.getElementById("last-loop");
const timeoutHeader = document.getElementById("timeouts");
const huntSelect = document.getElementById("hunt-mode");
const resetForm = document.getElementById("reset-form");
const continueForm = document.getElementById("continue-form");
const pauseForm = document.getElementById("pause-form");
const windowToggle = document.getElementById("use-window");
const windowCtrls = document.getElementById('window-ctrls');
const windowStartInput = document.getElementById("window-start");
const windowEndInput = document.getElementById("window-end");
const errorText = document.getElementById("error-text");

windowToggle.onclick = (evt) => {
  windowCtrls.style.display = evt.target.checked ? 'block' : 'none';
  drawGraph();
}

windowStartInput.onchange = () => {
  drawGraph();
};

windowEndInput.onchange = () => {
  drawGraph();
};

const rootURL = "http://192.168.43.194";

const w = can.width;
const h = can.height;
const margin = 60;

init();

function init() {
  console.log(`Looking for the Shiny Machine at ${rootURL}`)
  loading.style.display = 'block';
  loaded.style.display = 'none';
  // set reload so we can leave the page open
  setTimeout(() => {
    location.reload(true);
  }, 60000);
  // fetch the data
  getJson()
    .then(data => {
      version.textContent = 'v' + (data.version || "???");
      loading.style.display = 'none';
      loaded.style.display = 'block';
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
      values = data.values;
      resetCount = data.resetCount;
      timeoutCount = data.timeoutCount;
      lastMeasuredLoop = data.lastMeasuredLoop;
      isHunting = data.isHunting;
      huntmode = data.huntmode;
      useWindow = data.useWindow || useWindow;
      windowStart = data.windowStart || windowStart;
      windowEnd = data.windowEnd;

      // update DOM
      resetHeader.textContent = `Reset #${resetCount}, ${
        isHunting ? "still hunting" : "paused"
      }`;
      loopHeader.textContent = `Last measured loop took ${lastMeasuredLoop} ms`;
      timeoutHeader.textContent = `${timeoutCount} timeouts in this hunt`;
      huntSelect.value = huntmode;

      // window ctrls
      if (useWindow) {
        windowToggle.setAttribute('checked', 'checked');
      }
      windowCtrls.style.display = useWindow ? 'block' : 'none';
      windowStartInput.value = windowStart;
      windowEndInput.value = windowEnd;

      resetForm.setAttribute('action', rootURL + '/reset');
      continueForm.setAttribute('action', rootURL + '/continue');
      pauseForm.setAttribute('action', rootURL + '/pause');
      // update canvas
      drawGraph();
    })
    .catch(err => {
      errorText.textContent = err;
      throw err;
    });
}

async function getJson() {
  const response = await fetch(rootURL);
  return await response.json();
}

// dlLink.setAttribute('href', 'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify({ values: vals, tallies: valTallies })));

function drawGraph() {
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

  ctx.strokeStyle = "black";
  ctx.fillStyle = "black";
  ctx.font = "bold 20px sans-serif";

  // draw axes
  ctx.beginPath();
  ctx.moveTo(margin, 0);
  ctx.lineTo(margin, h - margin);
  ctx.lineTo(w, h - margin);

  // draw x-axis labels
  ctx.textAlign = 'start';
  for (let i = 0; i <= 5; i++) {
    const tickVal = minVal + (i * (maxVal + MAX_SHINY_TIME - minVal)) / 5;
    const tx = scaleX(tickVal);
    ctx.moveTo(tx, h - margin);
    ctx.lineTo(tx, h - margin + 10);
    ctx.fillText(tickVal, tx + 1, h - margin + 20);
  }

  // draw y-axis labels
  ctx.textAlign = 'end';
  const mlt = Math.floor(Math.log(maxValTally));
  for (let i = 0; i <= mlt; i++) {
    const tickVal = Math.pow(mlt, i);
    const tx = scaleY(tickVal);
    ctx.moveTo(margin - 10, tx);
    ctx.lineTo(margin, tx);
    ctx.fillText(tickVal, margin - 10, tx + 10);
  }

  // draw bars for values
  values.forEach((tuple, index) => {
    const scaledVal = scaleX(tuple[0]);
    const scaledTal = scaleY(tuple[1]);
    const scaledMinTal = scaleY(0);
    const shinyStart = scaleX(tuple[0] + MIN_SHINY_TIME);
    const shinyEnd = scaleX(tuple[0] + MAX_SHINY_TIME);
    ctx.moveTo(scaledVal, scaledMinTal);
    ctx.lineTo(scaledVal, scaledTal);
    // get opacity from distribution of hits
    const opacity = Math.max(0.1, tuple[1] / maxValTally);
    ctx.fillStyle = `rgba(0, 192, 255, ${opacity})`;
    ctx.fillRect(
      shinyStart,
      scaledTal,
      shinyEnd - shinyStart,
      scaledMinTal - scaledTal
    );
  });
  ctx.stroke();

  if (useWindow) {
    // draw window
    ctx.fillStyle = 'rgba(32, 224, 0, 0.5)';
    ctx.fillRect(scaleX(windowStart),  0, scaleX(windowEnd) - scaleX(windowStart), can.height - margin - 1);
  }
}
